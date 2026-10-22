#include "chimera/vm/ram_watch_vm.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "chimera/engine.h"
#include "chimera/vm/json.hpp"

namespace chimera::vm {

namespace {

#ifdef _WIN32
constexpr const char *kNewLine = "\r\n";
#else
constexpr const char *kNewLine = "\n";
#endif
constexpr const char *kOnScreen = "OnScreen\t";

std::string trimLineEnds(std::string s)
{
	while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
	while (!s.empty() && (s.front() == '\r' || s.front() == '\n')) s.erase(0, 1);
	return s;
}

/* value minus previous, as Chimera writes a difference: +5, -3, 0 */
std::string difference(const RamWatchViewModel::Watch &w)
{
	const int64_t d = static_cast<int64_t>(w.value) - static_cast<int64_t>(w.previous);
	return d > 0 ? "+" + std::to_string(d) : std::to_string(d);
}

uint32_t readValue(ce_session *s, int32_t index, int64_t address, int size, bool bigEndian, bool &ok)
{
	uint8_t b[4] = {0, 0, 0, 0};
	ok = ce_session_domain_read(s, index, address, b, size) == size;
	uint32_t v = 0;
	for (int i = 0; i < size; i++)
	{
		const int at = bigEndian ? i : size - 1 - i;
		v = (v << 8) | b[at];
	}
	return v;
}

} // namespace

RamWatchViewModel::RamWatchViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("ramWatch", dispatcher), session_(session)
{
	expose("count", "how many watches the list has", count);
	expose("domains", "the machine's memory domains, comma-separated", domains);
	expose("previousType", "what the previous value is: 0 the first one seen, 2 the last frame's, 3 the one before the last change",
		previousType, true);
	expose("path", "the .wch file the list was opened from or saved to", path);
	expose("changed", "the list changed since it was opened or saved", changed);
	expose("status", "what RAM Watch last had to say", status);
	expose("showOnScreen", "the watches marked for it are shown over the picture", showOnScreen, true);
	expose("onScreenText", "what is shown over the picture: \"note: value\", a line each", onScreenText);
	expose("formSize", "the new watch's size: 1, 2 or 4 bytes", formSize, true);
	expose("formDisplays", "the ways a value of that size can be shown, a line each", formDisplays);
	expose("formDisplay", "the way chosen, by its place in formDisplays", formDisplay, true);
	rows("watches", "one row a watch: address, value, previous value, changes, difference, domain, note, size, how it is shown, on screen", rows_);

	command("add", "watches an address: domain, address, size (1, 2 or 4), display (0 unsigned, 1 signed, 2 hex, 3 binary, "
		"4 fixed point 12.4, 5 fixed point 20.12, 6 fixed point 16.16, 7 float), big endian, note; its place in the list",
		{"domain", "address", "size", "display", "bigEndian", "note"},
		[this](const Args &a, std::string &error) -> Value {
			Watch w;
			int64_t size = 1, display = 0;
			if (!arg(a, 0, w.domain, error) || !arg(a, 1, w.address, error) || !arg(a, 2, size, error) || !arg(a, 3, display, error))
				return {};
			if (a.size() > 4 && !arg(a, 4, w.bigEndian, error)) return {};
			if (a.size() > 5 && !arg(a, 5, w.note, error)) return {};
			if (size != 1 && size != 2 && size != 4)
			{
				error = "a watch is 1, 2 or 4 bytes";
				return {};
			}
			w.size = static_cast<int>(size);
			w.display = static_cast<watch::Display>(display);
			if (display < 0 || display > 7 || !watch::fits(w.size, w.display))
			{
				error = "a value of " + std::to_string(size) + (size == 1 ? " byte" : " bytes") + " cannot be shown that way";
				return {};
			}
			if (w.address < 0)
			{
				error = "an address is not negative";
				return {};
			}
			sizeFromDomain(w);
			watches_.push_back(w);
			listChanged(true);
			return static_cast<int64_t>(watches_.size() - 1);
		});
	command("addFromForm", "watches an address (hex) in a domain, at the form's size and display", {"domain", "address", "bigEndian", "note"},
		[this](const Args &a, std::string &error) -> Value {
			std::string domain, address, note;
			bool bigEndian = false;
			if (!arg(a, 0, domain, error) || !arg(a, 1, address, error)) return {};
			if (a.size() > 2 && !arg(a, 2, bigEndian, error)) return {};
			if (a.size() > 3 && !arg(a, 3, note, error)) return {};
			char *end = nullptr;
			const long long at = std::strtoll(address.c_str(), &end, 16);
			if (address.empty() || end == nullptr || *end != '\0' || at < 0)
			{
				error = "\"" + address + "\" is not an address in hex";
				return {};
			}
			const std::vector<watch::Display> &ways = watch::displaysFor(static_cast<int>(formSize.get()));
			if (ways.empty() || formDisplay.get() < 0 || formDisplay.get() >= static_cast<int64_t>(ways.size()))
			{
				error = "choose a size and a way to show it";
				return {};
			}
			const auto added = run("add", {domain, int64_t{at}, formSize.get(), static_cast<int64_t>(ways[static_cast<size_t>(formDisplay.get())]),
				bigEndian, note}, error);
			return added.has_value() ? *added : Value{};
		});
	command("addProperty", "watches a game core's property by its name (\"Score\", \"Row[2]\")", {"name"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!arg(a, 0, name, error)) return {};
			bool found = false;
			session_.onMachineWait([&](ce_session *s) {
				uint32_t element = 0;
				found = s != nullptr && ce_session_property_find(s, name.c_str(), &element) >= 0;
			});
			if (!found)
			{
				error = "the game has no property " + name;
				return {};
			}
			Watch w;
			w.property = name;
			w.note = name;
			watches_.push_back(w);
			listChanged(true);
			return static_cast<int64_t>(watches_.size() - 1);
		});
	command("addSeparator", "a line with only a note, to group watches", {"note"},
		[this](const Args &a, std::string &error) -> Value {
			Watch w;
			w.separator = true;
			if (!a.empty() && !arg(a, 0, w.note, error)) return {};
			watches_.push_back(w);
			listChanged(true);
			return static_cast<int64_t>(watches_.size() - 1);
		});
	command("remove", "takes a watch off the list", {"index"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		if (!index(a, 0, i, error)) return {};
		watches_.erase(watches_.begin() + static_cast<long>(i));
		listChanged(true);
		return {};
	});
	command("move", "moves a watch to another place in the list", {"index", "to"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0, to = 0;
		if (!index(a, 0, i, error) || !index(a, 1, to, error)) return {};
		Watch w = watches_[i];
		watches_.erase(watches_.begin() + static_cast<long>(i));
		watches_.insert(watches_.begin() + static_cast<long>(to), w);
		listChanged(true);
		return {};
	});
	command("setNote", "a watch's note", {"index", "note"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		std::string note;
		if (!index(a, 0, i, error) || !arg(a, 1, note, error)) return {};
		watches_[i].note = note;
		listChanged(true);
		return {};
	});
	command("setDisplay", "how a watch's value is shown (as add)", {"index", "display"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		int64_t display = 0;
		if (!index(a, 0, i, error) || !arg(a, 1, display, error)) return {};
		if (display < 0 || display > 7 || watches_[i].separator || !watches_[i].property.empty() || !watch::fits(watches_[i].size, static_cast<watch::Display>(display)))
		{
			error = "that watch cannot be shown that way";
			return {};
		}
		watches_[i].display = static_cast<watch::Display>(display);
		listChanged(true);
		return {};
	});
	command("setOnScreen", "whether a watch is shown over the picture", {"index", "on"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		bool on = false;
		if (!index(a, 0, i, error) || !arg(a, 1, on, error)) return {};
		if (watches_[i].separator)
		{
			error = "a separator has no value to show";
			return {};
		}
		watches_[i].onScreen = on;
		listChanged(true);
		return {};
	});
	command("toggleOnScreen", "shows a watch over the picture, or stops", {"index"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		if (!index(a, 0, i, error)) return {};
		return run("setOnScreen", {static_cast<int64_t>(i), !watches_[i].onScreen}, error).has_value() ? Value(watches_[i].onScreen) : Value{};
	});
	command("clearChangeCounts", "every watch's changes back to 0", {}, [this](const Args &, std::string &) -> Value {
		for (Watch &w : watches_) w.changes = 0;
		listChanged(false);
		return {};
	});
	command("newList", "an empty list, in no file", {}, [this](const Args &, std::string &) -> Value {
		watches_.clear();
		path.set("");
		listChanged(false);
		changed.set(false);
		status.set("A new list");
		return {};
	});
	command("open", "the watches of a .wch file; with append, added to these", {"path", "append"},
		[this](const Args &a, std::string &error) -> Value {
			std::string file;
			bool append = false;
			if (!arg(a, 0, file, error)) return {};
			if (a.size() > 1 && !arg(a, 1, append, error)) return {};
			if (!load(file, append, error)) return {};
			return count.get();
		});
	command("save", "the list to a .wch file (the one it came from when no path is given)", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string file;
			if (!a.empty() && !arg(a, 0, file, error)) return {};
			if (file.empty()) file = path.get();
			if (file.empty())
			{
				error = "the list has no file yet: give it one";
				return {};
			}
			if (!save(file, error)) return {};
			return {};
		});

	hooks_ = session_.addMachineHooks([this](ce_session *s) { readOnMachine(s, false); },
		[this](ce_session *) {
			for (Watch &w : watchesM_) w.readable = false;
		});
	auto showWays = [this] {
		std::string lines;
		for (const watch::Display d : watch::displaysFor(static_cast<int>(formSize.get())))
			lines += (lines.empty() ? "" : "\n") + std::string(watch::displayName(d));
		formDisplays.set(lines);
		const int64_t n = static_cast<int64_t>(watch::displaysFor(static_cast<int>(formSize.get())).size());
		if (formDisplay.get() >= n) formDisplay.set(0);
	};
	formSizeWatch_ = formSize.changed.connect(showWays);
	showWatch_ = showOnScreen.changed.connect([this] { refreshOnScreen(); });
	showWays();
	opensWatch_ = session_.opens.changed.connect([this] { refreshDomains(); });
	refreshDomains();
	previousTypeWatch_ = previousType.changed.connect([this] {
		const int t = static_cast<int>(previousType.get());
		session_.onMachine([this, t](ce_session *) { previousTypeM_ = t; });
	});
}

void RamWatchViewModel::refreshDomains()
{
	session_.onMachine([this](ce_session *s) {
		std::string names, system;
		std::map<std::string, int64_t> sizes;
		if (s != nullptr)
		{
			for (int32_t i = 0; i < ce_session_domain_count(s); i++)
			{
				const std::string n = ce_session_domain_name(s, i);
				sizes[n] = ce_session_domain_size(s, i);
				if (ce_session_domain_hidden(s, i) != 0) continue;
				names += (names.empty() ? "" : ",") + n;
			}
			if (const char *id = ce_session_system_id(s)) system = id;
		}
		later([this, names, system, sizes] {
			domainSizes_ = sizes;
			for (Watch &w : watches_) sizeFromDomain(w);
			domains.set(names);
			systemId_ = system;
			if (!watches_.empty()) rows_.changed.emit(0, static_cast<int64_t>(watches_.size()) - 1);
		});
		readOnMachine(s, true);
	});
}

RamWatchViewModel::~RamWatchViewModel()
{
	session_.removeMachineHooks(hooks_);
	session_.onMachineWait([](ce_session *) {});
}

void RamWatchViewModel::readProperty(ce_session *s, Watch &w, bool fresh)
{
	uint32_t element = 0;
	const int32_t index = ce_session_property_find(s, w.property.c_str(), &element);
	bool ok = index >= 0;
	std::string now;
	if (ok)
	{
		char buf[1024];
		const int32_t n = ce_session_property_text(s, index, element, 1, buf, sizeof buf);
		ok = n >= 0;
		if (ok) now.assign(buf, static_cast<size_t>(std::min<int32_t>(n, sizeof buf - 1)));
		/* where it is now, and in which domain, as the engine's table says */
		const int64_t offset = ce_session_property_offset(s, index, element);
		if (offset >= 0) w.address = offset;
		/* a property of a dynamic table that is not there now */
		if (offset < 0 && ce_session_property_dynamic(s) != 0 && now.empty()) now = "(not there)";
		/* the table is read on a first read only: a property keeps its domain */
		json::Json table;
		std::string jerr;
		if (const char *t = (fresh || !w.readable) ? ce_session_property_table(s) : nullptr; t != nullptr && json::parse(t, table, jerr))
			if (const json::Json *list = table.find("properties"); list != nullptr && static_cast<size_t>(index) < list->items.size())
			{
				const json::Json &p = list->items[static_cast<size_t>(index)];
				if (const json::Json *dm = p.find("domain")) w.domain = dm->text;
				if (const json::Json *en = p.find("endian")) w.bigEndian = en->text == "big";
			}
		if (w.domainSize == 0)
			for (int32_t i = 0; i < ce_session_domain_count(s); i++)
				if (w.domain == ce_session_domain_name(s, i)) w.domainSize = ce_session_domain_size(s, i);
	}
	const bool was = w.readable;
	w.readable = ok;
	if (!ok) return;
	if (!was)
	{
		w.valueText = now;
		if (!w.seen) w.previousText = now;
		w.seen = true;
		return;
	}
	if (fresh) return;
	switch (previousTypeM_)
	{
	case 0: w.valueText = now; break;
	case 3:
		if (now != w.valueText)
		{
			w.previousText = w.valueText;
			w.valueText = now;
			w.changes++;
		}
		break;
	default:
		w.previousText = w.valueText;
		w.valueText = now;
		if (w.valueText != w.previousText) w.changes++;
		break;
	}
}

void RamWatchViewModel::refreshOnScreen()
{
	std::string lines;
	if (showOnScreen.get())
		for (const Watch &w : watches_)
		{
			if (!w.onScreen || w.separator) continue;
			const std::string value = !w.property.empty() ? (w.readable ? w.valueText : std::string("-"))
									: w.readable ? watch::format(w.value, w.size, w.display) : std::string("-");
			lines += (lines.empty() ? "" : "\n") + w.note + ": " + value;
		}
	onScreenText.set(lines);
}

void RamWatchViewModel::sizeFromDomain(Watch &w) const
{
	const auto found = domainSizes_.find(w.domain);
	if (found != domainSizes_.end()) w.domainSize = found->second;
}

bool RamWatchViewModel::index(const Args &a, size_t at, size_t &out, std::string &error) const
{
	int64_t i = 0;
	if (!arg(a, at, i, error)) return false;
	if (i < 0 || i >= static_cast<int64_t>(watches_.size()))
	{
		error = "no watch " + std::to_string(i);
		return false;
	}
	out = static_cast<size_t>(i);
	return true;
}

void RamWatchViewModel::listChanged(bool edit)
{
	count.set(static_cast<int64_t>(watches_.size()));
	if (edit) changed.set(true);
	const uint64_t serial = ++serial_;
	session_.onMachine([this, list = watches_, serial](ce_session *s) {
		watchesM_ = list;
		serialM_ = serial;
		readOnMachine(s, true);
	});
	rows_.reset.emit();
	refreshOnScreen();
}

void RamWatchViewModel::readOnMachine(ce_session *s, bool fresh)
{
	if (s == nullptr) return;
	std::map<std::string, int32_t> byName;
	for (int32_t i = 0; i < ce_session_domain_count(s); i++) byName[ce_session_domain_name(s, i)] = i;
	for (Watch &w : watchesM_)
	{
		if (w.separator) continue;
		if (!w.property.empty())
		{
			readProperty(s, w, fresh);
			continue;
		}
		const auto found = byName.find(w.domain);
		bool ok = false;
		uint32_t now = 0;
		if (found != byName.end())
		{
			w.domainSize = ce_session_domain_size(s, found->second);
			if (w.address + w.size <= w.domainSize) now = readValue(s, found->second, w.address, w.size, w.bigEndian, ok);
		}
		const bool was = w.readable;
		w.readable = ok;
		if (!ok) continue;
		/* read for the first time, or again on a machine opened since: its
		 * value, and a watch never seen before starts with it as the previous
		 * one; nothing counts as a change */
		if (!was)
		{
			w.value = now;
			if (!w.seen) w.previous = now;
			w.seen = true;
			continue;
		}
		/* the list changed, the machine did not move */
		if (fresh) continue;
		switch (previousTypeM_)
		{
		case 0: /* the first value seen stays the previous one */
			w.value = now;
			break;
		case 3: /* the value before the last change */
			if (now != w.value)
			{
				w.previous = w.value;
				w.value = now;
				w.changes++;
			}
			break;
		default: /* the last frame's */
			w.previous = w.value;
			w.value = now;
			if (w.value != w.previous) w.changes++;
			break;
		}
	}
	{
		std::lock_guard<std::mutex> lock(valuesM_);
		valuesOut_ = watchesM_;
		valuesSerial_ = serialM_;
	}
	if (!postedM_.exchange(true))
		later([this] {
			postedM_ = false;
			std::vector<Watch> read;
			uint64_t serial = 0;
			{
				std::lock_guard<std::mutex> lock(valuesM_);
				read = valuesOut_;
				serial = valuesSerial_;
			}
			if (serial != serial_ || read.size() != watches_.size()) return;
			for (size_t i = 0; i < read.size(); i++)
			{
				watches_[i].value = read[i].value;
				watches_[i].previous = read[i].previous;
				watches_[i].changes = read[i].changes;
				watches_[i].readable = read[i].readable;
				watches_[i].seen = read[i].seen;
				watches_[i].valueText = read[i].valueText;
				watches_[i].previousText = read[i].previousText;
				if (!read[i].property.empty())
				{
					watches_[i].address = read[i].address;
					watches_[i].domain = read[i].domain;
					watches_[i].bigEndian = read[i].bigEndian;
				}
				watches_[i].domainSize = read[i].domainSize;
			}
			if (!watches_.empty()) rows_.changed.emit(0, static_cast<int64_t>(watches_.size()) - 1);
			refreshOnScreen();
		});
}

/* Chimera's .wch: a SystemID line, a line a watch - address in hex, size
 * letter, display letter, 1 for big endian, domain, note, split by tabs -
 * and an OnScreen line naming by place the watches shown over the picture.
 * Files from other emulators have a count first, a place number before
 * each line and no domain. */
bool RamWatchViewModel::load(const std::string &file, bool append, std::string &error)
{
	std::ifstream in(file, std::ios::binary);
	if (!in)
	{
		error = "cannot read " + file;
		return false;
	}
	std::vector<std::string> names;
	{
		std::string list = domains.get(), one;
		std::istringstream parts(list);
		while (std::getline(parts, one, ',')) names.push_back(one);
	}
	std::vector<Watch> read;
	std::vector<int> placeOf; /* each line of the file: the watch it gave, or -1 */
	std::string onScreen, line;
	bool ours = true;
	while (std::getline(in, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.size() < 5)
		{
			ours = false;
			continue;
		}
		if (line.rfind("Domain", 0) == 0) ours = true;
		if (line.rfind("SystemID", 0) == 0) continue;
		if (line.rfind(kOnScreen, 0) == 0)
		{
			onScreen = line.substr(std::char_traits<char>::length(kOnScreen));
			continue;
		}
		if (std::count(line.begin(), line.end(), '\t') != 5) continue;
		placeOf.push_back(-1);
		if (!ours) line = line.substr(line.find('\t') + 1);
		std::vector<std::string> f;
		{
			std::string rest = line;
			const size_t fields = ours ? 5 : 4;
			for (size_t k = 0; k < fields; k++)
			{
				const size_t tab = rest.find('\t');
				if (tab == std::string::npos) break;
				f.push_back(rest.substr(0, tab));
				rest = rest.substr(tab + 1);
			}
			f.push_back(rest);
		}
		if (f.size() < (ours ? 6u : 5u) || f[1].empty() || f[2].empty() || f[3].empty()) continue;
		Watch w;
		char *end = nullptr;
		w.address = std::strtoll(f[0].c_str(), &end, 16);
		if (f[0].empty() || end == nullptr || *end != '\0') continue;
		const int size = watch::sizeFromLetter(f[1][0]);
		if (f[1][0] == 'p' && ours)
		{
			/* a game property, known by its name (the note) */
			Watch p;
			p.propertyLine = line;
			p.property = p.note = trimLineEnds(f[5]);
			p.address = w.address;
			p.domain = f[4];
			p.bigEndian = !f[3].empty() && f[3][0] != '0';
			placeOf.back() = static_cast<int>(read.size());
			read.push_back(p);
			continue;
		}
		if (size < 0) continue;
		if (f[3][0] < '0' || f[3][0] > '9') continue;
		w.bigEndian = f[3][0] != '0';
		if (size == 0)
		{
			w.separator = true;
			w.note = trimLineEnds(ours ? f[5] : f[4]);
		}
		else
		{
			w.size = size;
			if (!watch::displayFromLetter(f[2][0], w.display) || !watch::fits(w.size, w.display)) w.display = watch::Display::Unsigned;
			w.domain = ours ? f[4] : std::string();
			/* a domain this machine does not have: its main memory, as Chimera does */
			if (!names.empty() && std::find(names.begin(), names.end(), w.domain) == names.end()) w.domain = names.front();
			w.note = trimLineEnds(ours ? f[5] : f[4]);
		}
		sizeFromDomain(w);
		placeOf.back() = static_cast<int>(read.size());
		read.push_back(w);
	}
	/* a place names a line of the file */
	std::istringstream places(onScreen);
	for (std::string p; std::getline(places, p, ',');)
	{
		const size_t at = p.find_first_not_of(' ');
		if (at == std::string::npos) continue;
		char *end = nullptr;
		const long i = std::strtol(p.c_str() + at, &end, 10);
		if (i >= 0 && i < static_cast<long>(placeOf.size()) && placeOf[static_cast<size_t>(i)] >= 0 &&
			!read[static_cast<size_t>(placeOf[static_cast<size_t>(i)])].separator)
			read[static_cast<size_t>(placeOf[static_cast<size_t>(i)])].onScreen = true;
	}
	if (!append) watches_.clear();
	watches_.insert(watches_.end(), read.begin(), read.end());
	path.set(file);
	listChanged(false);
	changed.set(append);
	status.set("Opened " + file + " (" + std::to_string(read.size()) + (read.size() == 1 ? " watch)" : " watches)"));
	return true;
}

bool RamWatchViewModel::save(const std::string &file, std::string &error)
{
	std::string text = "SystemID " + systemId_ + kNewLine;
	std::string onScreen;
	for (size_t i = 0; i < watches_.size(); i++)
	{
		const Watch &w = watches_[i];
		if (!w.property.empty())
			/* as Chimera writes one: where it is now when a machine says so,
			 * else the line the file had */
			text += w.domainSize != 0 || w.propertyLine.empty()
				? watch::hex(w.address, watch::addressDigits(w.domainSize != 0 ? w.domainSize : 0xFE)) + "\tp\tu\t" +
					  (w.bigEndian ? "1" : "0") + "\t" + w.domain + "\t" + w.property + kNewLine
				: w.propertyLine + kNewLine;
		else if (w.separator)
			text += std::string("0\tS\t_\t1\t\t") + trimLineEnds(w.note) + kNewLine;
		else
		{
			const int digits = watch::addressDigits(w.domainSize != 0 ? w.domainSize : 0xFE);
			text += watch::hex(w.address, digits) + "\t" + watch::sizeLetter(w.size) + "\t" + watch::displayLetter(w.display) + "\t" +
					(w.bigEndian ? "1" : "0") + "\t" + w.domain + "\t" + trimLineEnds(w.note) + kNewLine;
		}
		if (w.onScreen) onScreen += (onScreen.empty() ? "" : ",") + std::to_string(i);
	}
	if (!onScreen.empty()) text += kOnScreen + onScreen + kNewLine;
	text += kNewLine;
	/* written beside it and moved over it: a failed save leaves the old file */
	const std::filesystem::path writing = std::filesystem::path(file).concat(".writing");
	{
		std::ofstream out(writing, std::ios::binary | std::ios::trunc);
		out << text;
		if (!out)
		{
			error = "cannot write " + file;
			return false;
		}
	}
	std::error_code ec;
	std::filesystem::rename(writing, file, ec);
	if (ec)
	{
		std::filesystem::remove(writing, ec);
		error = "cannot write " + file;
		return false;
	}
	path.set(file);
	changed.set(false);
	status.set("Saved " + file);
	return true;
}

std::string RamWatchViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"address", "value", "previous", "changes", "diff", "domain", "note", "size", "shownAs", "onScreen"};
	return column >= 0 && column < 10 ? names[column] : "";
}

Value RamWatchViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Watch &w = vm_.watches_[static_cast<size_t>(row)];
	if (w.separator) return column == 6 ? Value(w.note) : column == 9 ? Value(false) : Value(std::string());
	if (!w.property.empty())
		switch (column)
		{
		case 0: return w.domainSize != 0 ? watch::hex(w.address, watch::addressDigits(w.domainSize - 1)) : std::string("-");
		case 1: return w.readable ? w.valueText : std::string("-");
		case 2: return w.readable ? w.previousText : std::string("-");
		case 3: return w.changes;
		case 5: return w.domain;
		case 6: return w.note;
		case 7: return int64_t{0};
		case 8: return std::string("Game property");
		case 9: return w.onScreen;
		default: return std::string();
		}
	switch (column)
	{
	case 0: return watch::hex(w.address, watch::addressDigits(w.domainSize != 0 ? w.domainSize - 1 : 0xFE));
	case 1: return w.readable ? watch::format(w.value, w.size, w.display) : std::string("-");
	case 2: return w.readable ? watch::format(w.previous, w.size, w.display) : std::string("-");
	case 3: return w.changes;
	case 4: return w.readable ? difference(w) : std::string("-");
	case 5: return w.domain;
	case 6: return w.note;
	case 7: return static_cast<int64_t>(w.size);
	case 8: return std::string(watch::displayName(w.display));
	case 9: return w.onScreen;
	default: return {};
	}
}

} // namespace chimera::vm
