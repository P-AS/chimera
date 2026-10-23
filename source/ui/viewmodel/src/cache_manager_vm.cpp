#include "chimera/vm/cache_manager_vm.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

#include "chimera/vm/json.hpp"
#include "chimera/vm/process_stamp.hpp"

namespace chimera::vm {

namespace fs = std::filesystem;

namespace {
const char *const kProjectCache = "Project cache";
const char *const kUnsavedWork = "Unsaved work";

std::string readText(const fs::path &file)
{
	std::ifstream in(file, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/* a whole number as written: a process's start time does not fit a double */
int64_t whole(const json::Json *v, int64_t otherwise)
{
	if (v == nullptr || v->kind != json::Json::Kind::Number) return otherwise;
	try
	{
		return static_cast<int64_t>(std::stoll(v->text));
	}
	catch (...)
	{
		return otherwise;
	}
}

/* what somebody has said about each place: locked or not, where it is not the default */
std::map<std::string, bool> readLocks(const std::string &file)
{
	std::map<std::string, bool> said;
	json::Json j;
	std::string err;
	const std::string text = readText(file);
	if (text.empty() || !json::parse(text, j, err) || j.kind != json::Json::Kind::Object) return said;
	for (const auto &[place, value] : j.members)
		if (value.kind == json::Json::Kind::Bool) said[place] = value.boolean;
	return said;
}

bool lockedByDefault(const std::string &kind) { return kind != kProjectCache; }
} // namespace

CacheManagerViewModel::CacheManagerViewModel(Dispatcher &dispatcher) : ViewModel("cacheManager", dispatcher)
{
	freeSpaceAt = [](const std::string &place) -> uint64_t {
		std::error_code ec;
		const fs::space_info s = fs::space(place, ec);
		return ec ? UINT64_MAX : static_cast<uint64_t>(s.available);
	};
	expose("cacheFolder", "where the project caches are", cacheFolder, true);
	expose("recoveryFolder", "where unsaved work is kept against a crash", recoveryFolder, true);
	expose("total", "all of it together, as it reads", total);
	expose("inUse", "the open project's cache folder name, \"\" for none", inUse, true);
	expose("autoClean", "the cache is held under its limit by itself, the oldest unlocked entries going first, at the "
		"start and when a project closes", autoClean, true);
	expose("limitGb", "what the whole cache may weigh, in gigabytes", limitGb, true);
	expose("floorGb", "how much of the disk to leave free whatever the limit says, in gigabytes", floorGb, true);
	expose("overLimit", "the line saying the cache is over its limit, and what that means; \"\" when it is not", overLimit);
	expose("status", "what the cache manager last did", status);
	rows("caches", "one row an entry: title, project, core, system, games, size (bytes), sizeText, orphan (its "
		"project is not where it was last seen), folder, open (in use), note, kind (\"Project cache\" or \"Unsaved work\"), "
		"locked (kept from the auto-clean), lastUsed", rows_);
	command("refresh", "looks at what is kept again", {}, [this](const Args &, std::string &) -> Value {
		survey();
		return static_cast<int64_t>(items_.size());
	});
	command("remove", "removes the entries at these places in the list (\"0,2\"), locked or not: a project cache's run "
		"replays instead of resuming; unsaved work is gone", {"rows"}, [this](const Args &a, std::string &error) -> Value {
			std::vector<size_t> picked;
			if (!rowsOf(a, picked, error)) return {};
			std::string failed;
			const int64_t removed = removeAll(picked, failed);
			survey();
			if (!failed.empty()) error = "could not remove: " + failed;
			return removed;
		});
	command("orphans", "the places in the list of entries whose project is gone (\"1,3\")", {}, [this](const Args &, std::string &) -> Value {
		std::string list;
		for (size_t i = 0; i < items_.size(); i++)
			if (items_[i].orphan) list += (list.empty() ? "" : ",") + std::to_string(i);
		return list;
	});
	command("toggleLock", "locks the entries at these places (\"0,2\") when any of them is unlocked, else unlocks them all, "
		"as Chimera's Lock / Unlock", {"rows"}, [this](const Args &a, std::string &error) -> Value {
			std::vector<size_t> picked;
			if (!rowsOf(a, picked, error)) return {};
			if (picked.empty()) return {};
			bool locking = false;
			for (size_t i : picked) locking = locking || !items_[i].locked;
			std::map<std::string, bool> said = readLocks(locksFile());
			for (size_t i : picked)
			{
				/* only what differs from its kind's default is written down */
				if (locking == lockedByDefault(items_[i].kind)) said.erase(items_[i].folder);
				else said[items_[i].folder] = locking;
			}
			json::Json book;
			book.kind = json::Json::Kind::Object;
			std::error_code ec;
			for (const auto &[place, locked] : said)
				if (fs::is_directory(place, ec)) book.members.emplace_back(place, json::Json::boolean_(locked));
			fs::create_directories(cacheFolder.get(), ec);
			std::ofstream(locksFile(), std::ios::binary | std::ios::trunc) << json::write(book);
			survey();
			status.set(locking ? "Locked " + std::to_string(picked.size()) + " item(s); the auto-clean will leave them alone."
							   : "Unlocked " + std::to_string(picked.size()) +
					" item(s); the auto-clean may take them when the cache is over its limit.");
			return locking;
		});
	command("titles", "the entries at these places (\"0,2\"), a line each: \"- title (size)\", for a question to name them",
		{"rows"}, [this](const Args &a, std::string &error) -> Value {
			std::vector<size_t> picked;
			if (!rowsOf(a, picked, error)) return {};
			std::string lines;
			for (size_t i : picked) lines += (lines.empty() ? "- " : "\n- ") + items_[i].title + " (" + sizeText(items_[i].bytes) + ")";
			return lines;
		});
	command("wouldGo", "the places in the list Clean Now would take (\"1,3\"), oldest first, to come under the limit", {},
		[this](const Args &, std::string &) -> Value {
			std::string list;
			for (size_t i : whatWouldGo(items_, limitNow(), {})) list += (list.empty() ? "" : ",") + std::to_string(i);
			return list;
		});
	command("cleanNow", "does now what the auto-clean would, whether it is on or not: the oldest unlocked entries until "
		"the cache is under its limit", {}, [this](const Args &, std::string &) -> Value {
			uint64_t all = 0;
			for (const Item &i : items_) all += i.bytes;
			const uint64_t limit = limitNow();
			const std::vector<size_t> going = whatWouldGo(items_, limit, {});
			if (going.empty())
			{
				status.set(all > limit ? "Over the limit, and nothing left may be taken: what remains is locked, in use, or the run last worked on."
									   : "The cache is already under its limit.");
				return int64_t{0};
			}
			uint64_t freed = 0;
			for (size_t i : going) freed += items_[i].bytes;
			std::string failed;
			const int64_t removed = removeAll(going, failed);
			survey();
			status.set("Removed " + std::to_string(removed) + " of the oldest unlocked item(s), freeing " + sizeText(freed) + "." +
				(failed.empty() ? "" : " Could not remove: " + failed));
			return removed;
		});
	command("runAutoClean", "the auto-clean, when it is on: the oldest unlocked entries until the cache is under its limit, "
		"never the one named (a project cache folder's name, the project just closed); says what it freed", {"spare"},
		[this](const Args &a, std::string &error) -> Value {
			std::string spare;
			if (a.size() > 0 && !arg(a, 0, spare, error)) return {};
			if (!autoClean.get()) return std::string();
			survey();
			std::vector<std::string> spared;
			if (!spare.empty()) spared.push_back((fs::path(cacheFolder.get()) / spare).string());
			const std::vector<size_t> going = whatWouldGo(items_, limitNow(), spared);
			if (going.empty()) return std::string();
			uint64_t freed = 0;
			for (size_t i : going) freed += items_[i].bytes;
			std::string failed;
			removeAll(going, failed);
			survey();
			const std::string said = "Cache auto-clean freed " + sizeText(freed);
			status.set(said);
			return said;
		});
	folderWatch_ = cacheFolder.changed.connect([this] { survey(); });
	recoveryWatch_ = recoveryFolder.changed.connect([this] { survey(); });
	inUseWatch_ = inUse.changed.connect([this] { survey(); });
	/* a setting: kept at once; nothing is removed while a number is being typed */
	limitWatch_ = limitGb.changed.connect([this] {
		if (limitGb.get() < 1) limitGb.set(1);
		sayOverLimit();
	});
	floorWatch_ = floorGb.changed.connect([this] {
		if (floorGb.get() < 0) floorGb.set(0);
		sayOverLimit();
	});
	autoWatch_ = autoClean.changed.connect([this] { sayOverLimit(); });
}

std::string CacheManagerViewModel::sizeText(uint64_t bytes)
{
	const char *units[] = {"bytes", "KB", "MB", "GB", "TB"};
	double v = static_cast<double>(bytes);
	int u = 0;
	while (v >= 1024 && u < 4) v /= 1024, u++;
	char text[32];
	if (u == 0) std::snprintf(text, sizeof text, "%llu bytes", static_cast<unsigned long long>(bytes));
	else std::snprintf(text, sizeof text, "%.1f %s", v, units[u]);
	return text;
}

uint64_t CacheManagerViewModel::effectiveLimit(uint64_t limit, uint64_t floor, uint64_t cacheBytes, uint64_t freeBytes, uint64_t never)
{
	if (freeBytes >= floor) return limit;
	/* giving the floor back costs what it is short by, and the cache can only give what it holds */
	const uint64_t shortBy = floor - freeBytes;
	const uint64_t fromCache = cacheBytes > shortBy ? cacheBytes - shortBy : 0;
	/* never less than the part that is always the person's: a disk low for
	 * reasons of its own must not cost every greenzone at every start */
	const uint64_t always = std::min(never, limit);
	return std::max(always, std::min(limit, fromCache));
}

std::vector<size_t> CacheManagerViewModel::whatWouldGo(const std::vector<Item> &items, uint64_t limit, const std::vector<std::string> &spare)
{
	uint64_t total = 0;
	for (const Item &i : items) total += i.bytes;
	if (total <= limit) return {};
	/* the newest is never taken: the last thing worked on survives, and the cache never empties itself */
	size_t newest = items.size();
	for (size_t i = 0; i < items.size(); i++)
		if (items[i].bytes > 0 && (newest == items.size() || items[i].lastUsed > items[newest].lastUsed)) newest = i;
	std::vector<size_t> candidates;
	for (size_t i = 0; i < items.size(); i++)
	{
		const Item &it = items[i];
		if (it.open || it.locked || it.bytes == 0 || i == newest) continue;
		if (std::find(spare.begin(), spare.end(), it.folder) != spare.end()) continue;
		candidates.push_back(i);
	}
	/* oldest first; among the same age the biggest, so the limit costs as few removals as it can */
	std::stable_sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) {
		if (items[a].lastUsed != items[b].lastUsed) return items[a].lastUsed < items[b].lastUsed;
		return items[a].bytes > items[b].bytes;
	});
	std::vector<size_t> going;
	for (size_t i : candidates)
	{
		if (total <= limit) break;
		going.push_back(i);
		total -= items[i].bytes;
	}
	return going;
}

std::string CacheManagerViewModel::locksFile() const
{
	return cacheFolder.get().empty() ? std::string() : (fs::path(cacheFolder.get()) / "cache-locks.json").string();
}

uint64_t CacheManagerViewModel::limitNow() const
{
	uint64_t all = 0;
	for (const Item &i : items_) all += i.bytes;
	const uint64_t freeBytes = cacheFolder.get().empty() || !freeSpaceAt ? UINT64_MAX : freeSpaceAt(cacheFolder.get());
	return effectiveLimit(static_cast<uint64_t>(limitGb.get()) * unitBytes, static_cast<uint64_t>(floorGb.get()) * unitBytes, all,
		freeBytes, unitBytes);
}

void CacheManagerViewModel::sayOverLimit()
{
	uint64_t all = 0;
	for (const Item &i : items_) all += i.bytes;
	const uint64_t limit = limitNow();
	if (all <= limit)
	{
		overLimit.set("");
		return;
	}
	std::string line;
	const uint64_t asked = static_cast<uint64_t>(limitGb.get()) * unitBytes;
	if (limit < asked)
	{
		const uint64_t freeBytes = freeSpaceAt ? freeSpaceAt(cacheFolder.get()) : UINT64_MAX;
		line += "The disk has " + sizeText(freeBytes) + " free and is to be left " + std::to_string(floorGb.get()) +
			" GB free, so the cache is being held to " + sizeText(limit) + " rather than to the " +
			std::to_string(limitGb.get()) + " GB asked for" + (limit <= unitBytes ? " (it is never held to less than that because of the disk)" : "") +
			". Lower the free-space figure, or move the cache to a roomier disk. ";
	}
	uint64_t going = 0;
	for (size_t i : whatWouldGo(items_, limit, {})) going += items_[i].bytes;
	line += "That is " + sizeText(all - limit) + " over. ";
	line += going < all - limit ? "What is left is locked, in use, or the run last worked on, so the limit cannot be met without unlocking something."
			: autoClean.get()   ? "The oldest unlocked items go when a project closes or at the next start, or now with Clean Now."
								: "Nothing is enforcing it while the auto-clean is off; Clean Now applies it once.";
	overLimit.set(line);
}

bool CacheManagerViewModel::rowsOf(const Args &a, std::vector<size_t> &picked, std::string &error) const
{
	std::string list;
	if (!arg(a, 0, list, error)) return false;
	std::stringstream in(list);
	for (std::string one; std::getline(in, one, ',');)
		if (!one.empty())
		{
			char *end = nullptr;
			const long i = std::strtol(one.c_str(), &end, 10);
			if (end == nullptr || *end != '\0' || i < 0 || static_cast<size_t>(i) >= items_.size())
			{
				error = "no row " + one;
				return false;
			}
			if (std::find(picked.begin(), picked.end(), static_cast<size_t>(i)) == picked.end()) picked.push_back(static_cast<size_t>(i));
		}
	return true;
}

int64_t CacheManagerViewModel::removeAll(const std::vector<size_t> &picked, std::string &failed)
{
	int64_t removed = 0;
	for (size_t i : picked)
	{
		std::error_code ec;
		const Item &item = items_[i];
		/* only ever a folder inside the cache folder or the recovery folder */
		const fs::path folder = item.folder;
		const fs::path root = item.unsavedWork ? fs::path(recoveryFolder.get()) : fs::path(cacheFolder.get());
		if (root.empty() || folder.parent_path() != root) continue;
		/* the open project's is being written */
		if (item.open)
		{
			failed += (failed.empty() ? "" : ", ") + item.title + " (in use)";
			continue;
		}
		fs::remove_all(folder, ec);
		if (ec) failed += (failed.empty() ? "" : ", ") + item.title;
		else removed++;
	}
	return removed;
}

void CacheManagerViewModel::survey()
{
	items_.clear();
	std::error_code ec;
	const std::map<std::string, bool> said = readLocks(locksFile());
	auto measure = [&](Item &item, const fs::path &dir) {
		/* the file clock's zero is not the past (libstdc++ counts today as below it): start at its least */
		fs::file_time_type newest = fs::file_time_type::min();
		for (fs::recursive_directory_iterator f(dir, ec), fend; !ec && f != fend; f.increment(ec))
			if (f->is_regular_file(ec))
			{
				item.bytes += f->file_size(ec);
				const fs::file_time_type t = f->last_write_time(ec);
				if (!ec && t > newest) newest = t;
			}
		ec.clear();
		item.lastUsed = static_cast<int64_t>(newest.time_since_epoch().count());
		const auto lock = said.find(item.folder);
		item.locked = lock != said.end() ? lock->second : lockedByDefault(item.kind);
	};
	const fs::path root = cacheFolder.get();
	if (!root.empty() && fs::is_directory(root, ec))
		for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
		{
			if (!it->is_directory(ec)) continue;
			Item item;
			item.kind = kProjectCache;
			item.folder = it->path().string();
			item.title = "(a project that never said its name)";
			item.open = !inUse.get().empty() && it->path().filename().string() == inUse.get();
			/* who it belongs to, when it says */
			json::Json about;
			std::string err;
			if (json::parse(readText(it->path() / "about.json"), about, err))
			{
				if (const json::Json *t = about.find("title"); t != nullptr && !t->text.empty()) item.title = t->text;
				if (const json::Json *p = about.find("projectPath")) item.projectPath = p->text;
				if (const json::Json *c = about.find("core")) item.core = c->text;
				if (const json::Json *s = about.find("system")) item.system = s->text;
				if (const json::Json *g = about.find("games"))
					for (const json::Json &one : g->items) item.games += (item.games.empty() ? "" : ", ") + one.text;
			}
			/* as Chimera: only a project whose place is known and empty now; the open one is not missing */
			item.orphan = !item.open && !item.projectPath.empty() && !fs::exists(item.projectPath, ec);
			measure(item, it->path());
			items_.push_back(std::move(item));
		}
	/* unsaved work: a folder per project, by its id, while it has work not
	 * saved, or after a crash until that project opens again */
	const fs::path kept = recoveryFolder.get();
	ec.clear();
	if (!kept.empty() && fs::is_directory(kept, ec))
		for (fs::directory_iterator it(kept, ec), end; !ec && it != end; it.increment(ec))
		{
			/* "recovered" holds rebuilt copies when there is no backup folder: work, not a cache */
			if (!it->is_directory(ec) || it->path().filename() == "recovered") continue;
			Item item;
			item.kind = kUnsavedWork;
			item.unsavedWork = true;
			item.folder = it->path().string();
			json::Json record;
			std::string err;
			int64_t pid = 0, started = -1;
			if (json::parse(readText(it->path() / "session.json"), record, err))
			{
				if (const json::Json *p = record.find("projectPath")) item.projectPath = p->text;
				pid = whole(record.find("processId"), 0);
				started = whole(record.find("processStarted"), -1);
			}
			item.title = "Unsaved work" + (item.projectPath.empty() ? std::string() : ": " + fs::path(item.projectPath).stem().string());
			/* a window still writing it has it open */
			item.open = pid > 0 && started >= 0 && processStarted(pid) == started;
			item.orphan = !item.open && !item.projectPath.empty() && !fs::exists(item.projectPath, ec);
			measure(item, it->path());
			items_.push_back(std::move(item));
		}
	/* the largest first: what is worth reclaiming is at the top */
	std::stable_sort(items_.begin(), items_.end(), [](const Item &a, const Item &b) { return a.bytes > b.bytes; });
	uint64_t all = 0;
	for (const Item &i : items_) all += i.bytes;
	total.set(sizeText(all) + " in " + std::to_string(items_.size()) + (items_.size() == 1 ? " item" : " items"));
	rows_.reset.emit();
	sayOverLimit();
}

std::string CacheManagerViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"title", "project", "core", "system", "games", "size", "sizeText", "orphan", "folder",
		"open", "note", "kind", "locked", "lastUsed"};
	return column >= 0 && column < 14 ? names[column] : "";
}

Value CacheManagerViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Item &i = vm_.items_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return i.title;
	case 1: return i.projectPath;
	case 2: return i.core;
	case 3: return i.system;
	case 4: return i.games;
	case 5: return static_cast<int64_t>(i.bytes);
	case 6: return sizeText(i.bytes);
	case 7: return i.orphan;
	case 8: return i.folder;
	case 9: return i.open;
	case 10:
		if (i.unsavedWork)
			return i.open ? std::string("In use: a window has this project open with changes not saved.")
				   : i.orphan ? std::string("Work a crash left; the project it belongs to is not where it was last seen.")
							  : std::string("Work a crash left: it is offered back when the project next opens. Removing it loses that work.");
		return i.open ? std::string("In use: the project open now.")
			   : i.orphan ? std::string("The project this belongs to is not where it was last seen.")
						  : std::string();
	case 11: return i.kind;
	case 12: return i.locked;
	case 13: return i.lastUsed;
	default: return {};
	}
}

} // namespace chimera::vm
