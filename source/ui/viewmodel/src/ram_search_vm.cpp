#include "chimera/vm/ram_search_vm.hpp"

#include <cstdio>
#include <cstring>

#include "chimera/engine.h"

namespace chimera::vm {

namespace {
/* a domain with no linear backing is read range by range */
struct DomainReader
{
	ce_session *s;
	int32_t index;
};
int64_t readDomainRange(void *user, int64_t offset, uint8_t *buf, int64_t len)
{
	const auto *r = static_cast<const DomainReader *>(user);
	return ce_session_domain_read(r->s, r->index, offset, buf, len);
}
DomainReader g_reader; /* the machine's thread's: one search at a time */
} // namespace

RamSearchViewModel::RamSearchViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("ramSearch", dispatcher), session_(session)
{
	expose("domains", "the machine's memory domains, comma-separated", domains);
	expose("domain", "the domain searched", domain, true);
	expose("size", "bytes a value takes: 1, 2 or 4", size, true);
	expose("bigEndian", "values are stored most significant byte first", bigEndian, true);
	expose("display", "how values read: 0 unsigned, 1 signed, 2 a real number", display, true);
	expose("compare", "a search compares with: 0 the previous value, 1 a value, 2 an address, 3 changes, 4 a difference", compare, true);
	expose("op", "how: 0 equal, 1 more, 2 at least, 3 less, 4 at most, 5 not equal, 6 different by", op, true);
	expose("value", "the value, address, count or difference compared with", value, true);
	expose("differentBy", "for \"different by\": by how much", differentBy, true);
	expose("count", "candidates left", count);
	expose("canUndo", "a search can be taken back", canUndo);
	expose("canRedo", "a search taken back can be done again", canRedo);
	expose("searching", "a search is open", searching);
	expose("status", "what the search last had to say", status);
	rows("results", "the candidates: address, value, previous value, changes", results_);

	command("start", "every address of the domain becomes a candidate", {},
		[this](const Args &, std::string &error) -> Value {
			const int32_t sz = static_cast<int32_t>(size.get());
			if (sz != 1 && sz != 2 && sz != 4)
			{
				error = "size is 1, 2 or 4";
				return {};
			}
			const std::string d = domain.get();
			const bool be = bigEndian.get();
			session_.onMachine([this, d, sz, be](ce_session *s) { startOnMachine(s, d, sz, be); });
			return {};
		}, &session_.isOpen);
	command("search", "keeps the candidates that compare as set", {},
		[this](const Args &, std::string &error) -> Value {
			if (!searching.get())
			{
				error = "no search is open: start one";
				return {};
			}
			const int32_t c = static_cast<int32_t>(compare.get()), o = static_cast<int32_t>(op.get()),
				dsp = static_cast<int32_t>(display.get());
			const uint32_t v = static_cast<uint32_t>(value.get()), by = static_cast<uint32_t>(differentBy.get());
			session_.onMachine([this, c, o, dsp, v, by](ce_session *) { searchOnMachine(c, o, dsp, v, by); });
			return {};
		}, &searching);
	command("undo", "takes the last search back", {}, [this](const Args &, std::string &) -> Value {
		session_.onMachine([this](ce_session *) {
			{
				std::lock_guard<std::mutex> lock(rsM_);
				if (rs_ != nullptr) ce_ramsearch_undo(rs_);
			}
			reportOnMachine("Taken back");
		});
		return {};
	}, &canUndo);
	command("redo", "does a search taken back again", {}, [this](const Args &, std::string &) -> Value {
		session_.onMachine([this](ce_session *) {
			{
				std::lock_guard<std::mutex> lock(rsM_);
				if (rs_ != nullptr) ce_ramsearch_redo(rs_);
			}
			reportOnMachine("Done again");
		});
		return {};
	}, &canRedo);
	command("clearChanges", "sets every candidate's change count to 0", {}, [this](const Args &, std::string &) -> Value {
		session_.onMachine([this](ce_session *) {
			{
				std::lock_guard<std::mutex> lock(rsM_);
				if (rs_ != nullptr) ce_ramsearch_clear_change_counts(rs_);
			}
			reportOnMachine("Changes cleared");
		});
		return {};
	}, &searching);

	hooks_ = session_.addMachineHooks([this](ce_session *) { afterFrameOnMachine(); }, [this](ce_session *) { dropOnMachine(); });
	/* each machine opened: its domains */
	opensWatch_ = session_.opens.changed.connect([this] {
		session_.onMachine([this](ce_session *s) {
			std::string names, first;
			if (s != nullptr)
				for (int32_t i = 0; i < ce_session_domain_count(s); i++)
				{
					if (ce_session_domain_hidden(s, i) != 0) continue;
					const std::string n = ce_session_domain_name(s, i);
					if (first.empty()) first = n;
					names += (names.empty() ? "" : ",") + n;
				}
			later([this, names, first] {
				domains.set(names);
				const std::string d = "," + domain.get() + ",";
				if (domain.get().empty() || ("," + names + ",").find(d) == std::string::npos) domain.set(first);
			});
		});
	});
}

RamSearchViewModel::~RamSearchViewModel()
{
	session_.removeMachineHooks(hooks_);
	/* the search goes on the machine's thread, which may be using it */
	session_.onMachineWait([this](ce_session *) { dropOnMachine(); });
}

void RamSearchViewModel::dropOnMachine()
{
	{
		std::lock_guard<std::mutex> lock(rsM_);
		if (rs_ == nullptr) return;
		ce_ramsearch_destroy(rs_);
		rs_ = nullptr;
	}
	later([this] {
		rows_ = 0;
		count.set(0);
		canUndo.set(false);
		canRedo.set(false);
		searching.set(false);
		status.set("No search: the machine closed");
		results_.reset.emit();
	});
}

void RamSearchViewModel::startOnMachine(ce_session *s, const std::string &name, int32_t sz, bool be)
{
	if (s == nullptr) return;
	int32_t index = -1;
	for (int32_t i = 0; i < ce_session_domain_count(s); i++)
		if (name == ce_session_domain_name(s, i)) index = i;
	if (index < 0)
	{
		later([this, name] { status.set("No domain called " + name); });
		return;
	}
	const int64_t bytes = ce_session_domain_size(s, index);
	const uint64_t ptr = ce_session_domain_read_ptr(s, index);
	ce_ramsearch *rs = nullptr;
	if (ptr != 0)
		rs = ce_ramsearch_create(reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(ptr)), nullptr, nullptr, bytes);
	else
	{
		g_reader = {s, index};
		rs = ce_ramsearch_create(nullptr, &readDomainRange, &g_reader, bytes);
	}
	if (rs == nullptr || ce_ramsearch_start(rs, sz, 0, be ? 1 : 0, 1) != 0)
	{
		if (rs != nullptr) ce_ramsearch_destroy(rs);
		later([this] { status.set("Not enough memory for that search"); });
		return;
	}
	ce_ramsearch_set_undo_enabled(rs, 1);
	{
		std::lock_guard<std::mutex> lock(rsM_);
		if (rs_ != nullptr) ce_ramsearch_destroy(rs_);
		rs_ = rs;
	}
	later([this, sz] { searching.set(true); size.set(sz); });
	reportOnMachine("Started on " + name);
}

void RamSearchViewModel::searchOnMachine(int32_t c, int32_t o, int32_t dsp, uint32_t v, uint32_t by)
{
	int64_t removed = 0;
	{
		std::lock_guard<std::mutex> lock(rsM_);
		if (rs_ == nullptr) return;
		/* the previous value is the one at the last search, as RAM Search's default */
		removed = ce_ramsearch_search(rs_, c, o, dsp, v, by, 1);
	}
	reportOnMachine(removed < 0 ? std::string("Not enough memory for that search")
								: std::to_string(removed) + " removed");
}

void RamSearchViewModel::afterFrameOnMachine()
{
	{
		std::lock_guard<std::mutex> lock(rsM_);
		if (rs_ == nullptr) return;
		ce_ramsearch_update(rs_, 1);
	}
	/* the values moved: the rows in view are read again, at most once a tick */
	if (!refreshPosted_.exchange(true))
		later([this] {
			refreshPosted_ = false;
			if (rows_ > 0) results_.changed.emit(0, rows_ - 1);
		});
}

void RamSearchViewModel::reportOnMachine(const std::string &text)
{
	int64_t n = 0;
	bool u = false, r = false;
	{
		std::lock_guard<std::mutex> lock(rsM_);
		if (rs_ != nullptr)
		{
			n = ce_ramsearch_count(rs_);
			u = ce_ramsearch_can_undo(rs_) != 0;
			r = ce_ramsearch_can_redo(rs_) != 0;
		}
	}
	later([this, n, u, r, text] {
		rows_ = n;
		count.set(n);
		canUndo.set(u);
		canRedo.set(r);
		status.set(text + ": " + std::to_string(n) + (n == 1 ? " candidate" : " candidates"));
		results_.reset.emit();
	});
}

Value RamSearchViewModel::shown(uint32_t raw) const
{
	const int64_t sz = size.get();
	switch (display.get())
	{
	case 1:
		if (sz == 1) return static_cast<int64_t>(static_cast<int8_t>(raw));
		if (sz == 2) return static_cast<int64_t>(static_cast<int16_t>(raw));
		return static_cast<int64_t>(static_cast<int32_t>(raw));
	case 2:
	{
		float f;
		std::memcpy(&f, &raw, sizeof f);
		return static_cast<double>(f);
	}
	default: return static_cast<int64_t>(raw);
	}
}

std::string RamSearchViewModel::Results::columnName(int32_t column) const
{
	static const char *const names[] = {"Address", "Value", "Previous", "Changes"};
	return column >= 0 && column < 4 ? names[column] : "";
}

Value RamSearchViewModel::Results::cell(int64_t row, int32_t column) const
{
	uint64_t address = 0;
	uint32_t current = 0, previous = 0, changes = 0;
	{
		std::lock_guard<std::mutex> lock(vm_.rsM_);
		if (vm_.rs_ == nullptr || ce_ramsearch_row(vm_.rs_, row, &address, &current, &previous, &changes) == 0) return {};
	}
	switch (column)
	{
	case 0:
	{
		char buf[24];
		std::snprintf(buf, sizeof buf, "%06llX", static_cast<unsigned long long>(address));
		return std::string(buf);
	}
	case 1: return vm_.shown(current);
	case 2: return vm_.shown(previous);
	case 3: return static_cast<int64_t>(changes);
	default: return {};
	}
}

} // namespace chimera::vm
