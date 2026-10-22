#include "chimera/vm/hex_editor_vm.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "chimera/engine.h"
#include "chimera/vm/watch_values.hpp"

namespace chimera::vm {

namespace {
int32_t domainIndex(ce_session *s, const std::string &name)
{
	for (int32_t i = 0; i < ce_session_domain_count(s); i++)
		if (name == ce_session_domain_name(s, i)) return i;
	return -1;
}

/* "12 AB" or "12ab": the bytes, or false */
bool hexBytes(const std::string &text, std::vector<uint8_t> &out)
{
	std::string digits;
	for (char c : text)
		if (!std::isspace(static_cast<unsigned char>(c))) digits += c;
	if (digits.empty() || digits.size() % 2 != 0) return false;
	out.clear();
	for (size_t i = 0; i < digits.size(); i += 2)
	{
		const std::string two = digits.substr(i, 2);
		if (two.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
		out.push_back(static_cast<uint8_t>(std::strtoul(two.c_str(), nullptr, 16)));
	}
	return true;
}
} // namespace

HexEditorViewModel::HexEditorViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("hexEditor", dispatcher), session_(session)
{
	expose("domains", "the machine's memory domains, comma-separated", domains);
	expose("domain", "the domain shown", domain, true);
	expose("size", "bytes a value takes: 1, 2 or 4", size, true);
	expose("bigEndian", "values are stored most significant byte first", bigEndian, true);
	expose("lineCount", "lines of 16 bytes the domain has", lineCount);
	expose("topLine", "the first line shown", topLine, true);
	expose("visibleLines", "how many lines are shown", visibleLines, true);
	expose("picked", "the address picked, -1 for none", picked, true);
	expose("writable", "the domain can be written", writable);
	expose("status", "what the hex editor last had to say", status);
	rows("lines", "the lines shown: address, then a value and whether it changed for each place (v0, c0, ...), then the text", lines_);

	command("goTo", "shows an address and picks it", {"address"}, [this](const Args &a, std::string &error) -> Value {
		int64_t address = 0;
		if (!arg(a, 0, address, error)) return {};
		if (address < 0 || address >= domainSize_)
		{
			error = "the domain has no address " + watch::hex(address, 1);
			return {};
		}
		picked.set(address);
		const int64_t line = address / 16;
		if (line < topLine.get() || line >= topLine.get() + visibleLines.get())
			topLine.set(std::max<int64_t>(0, std::min(line - visibleLines.get() / 2, lineCount.get() - visibleLines.get())));
		return {};
	});
	command("poke", "writes a value (hex, of the size shown) into the machine's memory at an address", {"address", "value"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t address = 0;
			std::string text;
			if (!arg(a, 0, address, error) || !arg(a, 1, text, error)) return {};
			const int sz = static_cast<int>(size.get());
			uint32_t value = 0;
			if (text.size() > static_cast<size_t>(sz * 2) || !watch::parse(text, watch::Display::Hex, value))
			{
				error = "\"" + text + "\" is not a value of " + std::to_string(sz) + (sz == 1 ? " byte" : " bytes") + " in hex";
				return {};
			}
			if (!writable.get())
			{
				error = domain.get() + " cannot be written";
				return {};
			}
			if (address < 0 || address + sz > domainSize_)
			{
				error = "the domain has no address " + watch::hex(address, 1);
				return {};
			}
			const std::string name = domain.get();
			const bool be = bigEndian.get();
			session_.onMachine([this, name, address, sz, be, value](ce_session *s) {
				if (s == nullptr) return;
				const int32_t i = domainIndex(s, name);
				const uint64_t ptr = i >= 0 ? ce_session_domain_ptr(s, i) : 0;
				if (ptr == 0)
				{
					later([this, name] { status.set(name + " has no memory that can be written directly"); });
					return;
				}
				auto *bytes = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(ptr));
				for (int k = 0; k < sz; k++)
				{
					const int shift = be ? (sz - 1 - k) * 8 : k * 8;
					bytes[address + k] = static_cast<uint8_t>(value >> shift);
				}
				/* the states after this frame were made without it */
				session_.dropStatesAfterOnMachine(ce_session_frame(s));
				readOnMachine(s);
				later([this, address] { status.set("Wrote at " + watch::hex(address, watch::addressDigits(domainSize_ > 0 ? domainSize_ - 1 : 0))); });
			});
			return {};
		});
	command("find", "the next place these bytes (hex) are, after the address picked", {"bytes"},
		[this](const Args &a, std::string &error) -> Value {
			std::string text;
			if (!arg(a, 0, text, error)) return {};
			std::vector<uint8_t> want;
			if (!hexBytes(text, want))
			{
				error = "\"" + text + "\" is not bytes in hex";
				return {};
			}
			const std::string name = domain.get();
			const int64_t from = picked.get() < 0 ? 0 : picked.get() + 1;
			status.set("Looking...");
			session_.onMachine([this, name, want, from](ce_session *s) {
				int64_t found = -1;
				if (s != nullptr)
					if (const int32_t i = domainIndex(s, name); i >= 0)
					{
						const int64_t total = ce_session_domain_size(s, i);
						const int64_t n = static_cast<int64_t>(want.size());
						std::vector<uint8_t> chunk(1 << 20);
						/* chunks that overlap by the pattern's length less one */
						for (int64_t at = from; at + n <= total && found < 0;)
						{
							const int64_t len = std::min<int64_t>(static_cast<int64_t>(chunk.size()), total - at);
							if (ce_session_domain_read(s, i, at, chunk.data(), len) != len) break;
							const auto hit = std::search(chunk.begin(), chunk.begin() + len, want.begin(), want.end());
							if (hit != chunk.begin() + len) found = at + (hit - chunk.begin());
							if (len < n) break;
							at += len - n + 1;
						}
					}
				later([this, found] {
					if (found < 0)
					{
						status.set("Not found");
						return;
					}
					std::string e;
					run("goTo", {found}, e);
					status.set("Found at " + watch::hex(found, watch::addressDigits(domainSize_ > 0 ? domainSize_ - 1 : 0)));
				});
			});
			return {};
		});

	hooks_ = session_.addMachineHooks([this](ce_session *s) { readOnMachine(s); },
		[this](ce_session *) {
			lastM_.clear();
			lastFromM_ = -1;
		});
	auto moved = [this] {
		if (topLine.get() < 0) topLine.set(0);
		reread();
	};
	topWatch_ = topLine.changed.connect(moved);
	visibleWatch_ = visibleLines.changed.connect(moved);
	sizeWatch_ = size.changed.connect([this] {
		if (size.get() != 1 && size.get() != 2 && size.get() != 4) size.set(1);
		lines_.reset.emit();
		reread();
	});
	domainWatch_ = domain.changed.connect([this] {
		const auto w = canWrite_.find(domain.get());
		writable.set(w != canWrite_.end() && w->second);
		const auto it = sizes_.find(domain.get());
		domainSize_ = it == sizes_.end() ? 0 : it->second;
		lineCount.set((domainSize_ + 15) / 16);
		picked.set(-1);
		topLine.set(0);
		reread();
	});
	opensWatch_ = session_.opens.changed.connect([this] { refreshDomains(); });
	refreshDomains();
}

HexEditorViewModel::~HexEditorViewModel()
{
	session_.removeMachineHooks(hooks_);
	session_.onMachineWait([](ce_session *) {});
}

void HexEditorViewModel::refreshDomains()
{
	session_.onMachine([this](ce_session *s) {
		std::string names;
		std::map<std::string, int64_t> sizes;
		std::map<std::string, bool> canWrite;
		if (s != nullptr)
			for (int32_t i = 0; i < ce_session_domain_count(s); i++)
			{
				if (ce_session_domain_hidden(s, i) != 0) continue;
				const std::string n = ce_session_domain_name(s, i);
				names += (names.empty() ? "" : ",") + n;
				sizes[n] = ce_session_domain_size(s, i);
				canWrite[n] = ce_session_domain_writable(s, i) != 0 && ce_session_domain_ptr(s, i) != 0;
			}
		later([this, names, sizes, canWrite] {
			sizes_ = sizes;
			canWrite_ = canWrite;
			domains.set(names);
			const std::string first = names.substr(0, names.find(','));
			if (sizes.find(domain.get()) == sizes.end()) domain.set(first);
			const auto w = canWrite.find(domain.get());
			writable.set(w != canWrite.end() && w->second);
			const auto it = sizes_.find(domain.get());
			domainSize_ = it == sizes_.end() ? 0 : it->second;
			lineCount.set((domainSize_ + 15) / 16);
			reread();
		});
	});
}

void HexEditorViewModel::reread()
{
	{
		std::lock_guard<std::mutex> lock(wantM_);
		wantDomain_ = domain.get();
		wantFrom_ = topLine.get() * 16;
		wantBytes_ = std::max<int64_t>(0, std::min(visibleLines.get() * 16, domainSize_ - wantFrom_));
	}
	session_.onMachine([this](ce_session *s) { readOnMachine(s); });
}

void HexEditorViewModel::readOnMachine(ce_session *s)
{
	if (s == nullptr) return;
	std::string name;
	int64_t from = 0, bytes = 0;
	{
		std::lock_guard<std::mutex> lock(wantM_);
		name = wantDomain_;
		from = wantFrom_;
		bytes = wantBytes_;
	}
	const int32_t i = domainIndex(s, name);
	if (i < 0 || bytes <= 0) return;
	std::vector<uint8_t> now(static_cast<size_t>(bytes));
	if (ce_session_domain_read(s, i, from, now.data(), bytes) != bytes) return;
	/* what changed since the same bytes were last read */
	std::vector<uint8_t> changed(now.size(), 0);
	if (name == lastDomainM_ && from == lastFromM_ && lastM_.size() == now.size())
		for (size_t k = 0; k < now.size(); k++) changed[k] = now[k] != lastM_[k] ? 1 : 0;
	lastM_ = now;
	lastFromM_ = from;
	lastDomainM_ = name;
	{
		std::lock_guard<std::mutex> lock(outM_);
		out_ = std::move(now);
		outChanged_ = std::move(changed);
		outFrom_ = from;
		outSize_ = ce_session_domain_size(s, i);
	}
	if (!postedM_.exchange(true))
		later([this] {
			postedM_ = false;
			{
				std::lock_guard<std::mutex> lock(outM_);
				shown_ = out_;
				changedShown_ = outChanged_;
				shownFrom_ = outFrom_;
			}
			lines_.reset.emit();
		});
}

int64_t HexEditorViewModel::Lines::rowCount() const
{
	const int64_t left = vm_.lineCount.get() - vm_.topLine.get();
	return std::max<int64_t>(0, std::min(vm_.visibleLines.get(), left));
}

std::string HexEditorViewModel::Lines::columnName(int32_t column) const
{
	if (column == 0) return "address";
	if (column == 33) return "text";
	if (column < 0 || column > 33) return "";
	const int k = (column - 1) / 2;
	return ((column - 1) % 2 == 0 ? "v" : "c") + std::to_string(k);
}

Value HexEditorViewModel::Lines::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const int64_t address = (vm_.topLine.get() + row) * 16;
	const int64_t at = address - vm_.shownFrom_;
	auto byteAt = [&](int64_t offset, uint8_t &b) {
		const int64_t idx = at + offset;
		if (idx < 0 || idx >= static_cast<int64_t>(vm_.shown_.size())) return false;
		b = vm_.shown_[static_cast<size_t>(idx)];
		return true;
	};
	if (column == 0) return watch::hex(address, watch::addressDigits(vm_.domainSize_ > 0 ? vm_.domainSize_ - 1 : 0));
	if (column == 33)
	{
		std::string text;
		for (int k = 0; k < 16; k++)
		{
			uint8_t b = 0;
			if (!byteAt(k, b)) break;
			text += b >= 0x20 && b < 0x7F ? static_cast<char>(b) : '.';
		}
		return text;
	}
	if (column < 1 || column > 32) return {};
	const int place = (column - 1) / 2;
	const int sz = static_cast<int>(vm_.size.get());
	if (place >= 16 / sz) return (column - 1) % 2 == 0 ? Value(std::string()) : Value(false);
	if ((column - 1) % 2 == 1)
	{
		bool any = false;
		for (int k = 0; k < sz; k++)
		{
			const int64_t idx = at + place * sz + k;
			if (idx >= 0 && idx < static_cast<int64_t>(vm_.changedShown_.size())) any = any || vm_.changedShown_[static_cast<size_t>(idx)] != 0;
		}
		return any;
	}
	uint32_t value = 0;
	for (int k = 0; k < sz; k++)
	{
		uint8_t b = 0;
		if (!byteAt(place * sz + k, b)) return std::string();
		const int shift = vm_.bigEndian.get() ? (sz - 1 - k) * 8 : k * 8;
		value |= static_cast<uint32_t>(b) << shift;
	}
	return watch::format(value, sz, watch::Display::Hex);
}

} // namespace chimera::vm
