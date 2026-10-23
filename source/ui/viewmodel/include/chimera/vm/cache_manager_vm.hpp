#pragma once
/* Chimera's Cache Manager (Tools), for what the prototype keeps: each
 * project's cache (its greenzone and branch states, a folder named by the
 * project's id), named as the project calls itself (about.json beside it),
 * how much room it takes, and whether its project is still where it was;
 * and each project's unsaved work kept against a crash (the session's
 * recovery folder). Losing a cache costs waiting, never work: a run replays
 * instead of resuming. Losing unsaved work is losing work.
 *
 * As Chimera's: the cache is held under a limit by an auto-clean that takes
 * the oldest entries first, at the start and when a project closes; the
 * limit is the smaller of what is asked for and what leaves the disk a
 * floor free, never below a gigabyte for the disk's sake. A lock is what
 * keeps an entry from the auto-clean (only from it: a row ticked and
 * removed by hand goes). Project caches start unlocked, unsaved work
 * locked; the locks said otherwise are kept in cache-locks.json in the
 * cache folder, by place. Never taken: what is in use, the entry last
 * worked on, the project just closed. */

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class CacheManagerViewModel : public ViewModel
{
public:
	explicit CacheManagerViewModel(Dispatcher &dispatcher);

	/* where the project caches are (the session's cacheFolder), and where
	 * unsaved work is kept (the session's recoveryFolder) */
	Property<std::string> cacheFolder, recoveryFolder;
	/* all of it together, as it reads ("1.2 GB in 3 items") */
	Property<std::string> total;
	/* the open project's cache folder name (its id), "" for none: in use,
	 * never an orphan and not removed (the window says which) */
	Property<std::string> inUse;
	/* the auto-clean, its limit and the disk's floor, in gigabytes */
	Property<bool> autoClean{true};
	Property<int64_t> limitGb{100}, floorGb{20};
	/* the line saying the cache is over its limit and what that means, "" when it is not */
	Property<std::string> overLimit;
	Property<std::string> status;

	struct Item
	{
		std::string folder, title, projectPath, core, system, games;
		/* "Project cache" or "Unsaved work" */
		std::string kind;
		uint64_t bytes = 0;
		/* when anything in it was last written, in the file clock's units */
		int64_t lastUsed = 0;
		/* its project is no longer where it was last seen; in use (the
		 * project open's, or unsaved work whose window still runs); kept
		 * from the auto-clean */
		bool orphan = false, open = false, locked = false, unsavedWork = false;
	};
	const std::vector<Item> &items() const { return items_; }
	static std::string sizeText(uint64_t bytes);

	/* the limit that applies: what is asked for, unless the disk has less
	 * than its floor free; then what gives the floor back, never below a
	 * gigabyte (or the limit, when that is smaller) */
	static uint64_t effectiveLimit(uint64_t limit, uint64_t floor, uint64_t cacheBytes, uint64_t freeBytes,
		uint64_t never = 1024ull * 1024ull * 1024ull);
	/* the entries the auto-clean would take, in the order it would, to come
	 * under the limit: oldest first (the biggest first among the same age),
	 * never one in use, locked, empty, spared (by folder) or the newest */
	static std::vector<size_t> whatWouldGo(const std::vector<Item> &items, uint64_t limit, const std::vector<std::string> &spare);
	/* the free room on the cache's disk; a test puts its own */
	std::function<uint64_t(const std::string &)> freeSpaceAt;
	/* what limitGb and floorGb count in: a gigabyte, or less for a test */
	uint64_t unitBytes = 1024ull * 1024ull * 1024ull;

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(CacheManagerViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.items_.size()); }
		int32_t columnCount() const override { return 14; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		CacheManagerViewModel &vm_;
	};
	void survey();
	uint64_t limitNow() const;
	void sayOverLimit();
	/* the rows named "0,2", or false with the reason */
	bool rowsOf(const Args &a, std::vector<size_t> &picked, std::string &error) const;
	/* removes the entries at these places; how many went, and what could not */
	int64_t removeAll(const std::vector<size_t> &picked, std::string &failed);
	std::string locksFile() const;
	std::vector<Item> items_;
	Rows rows_{*this};
	Connection folderWatch_, recoveryWatch_, inUseWatch_, limitWatch_, floorWatch_, autoWatch_;
};

} // namespace chimera::vm
