#pragma once
/* Chimera's Cache Manager (Tools), for what the prototype keeps: each
 * project's cache (its greenzone and branch states, a folder named by the
 * project's id), named as the project calls itself (about.json beside it),
 * how much room it takes, and whether its project is still where it was.
 * Losing a cache costs waiting, never work: a run replays instead of
 * resuming. Rows ticked are removed; a project gone can be picked out. */

#include <string>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class CacheManagerViewModel : public ViewModel
{
public:
	explicit CacheManagerViewModel(Dispatcher &dispatcher);

	/* where the project caches are (the session's cacheFolder) */
	Property<std::string> cacheFolder;
	/* all of it together, as it reads ("1.2 GB in 3 projects") */
	Property<std::string> total;
	/* the open project's cache folder name (its id), "" for none: in use,
	 * never an orphan and not removed (the window says which) */
	Property<std::string> inUse;

	struct Item
	{
		std::string folder, title, projectPath, core, system, games;
		uint64_t bytes = 0;
		/* its project is no longer where it was last seen; the project open's */
		bool orphan = false, open = false;
	};
	const std::vector<Item> &items() const { return items_; }
	static std::string sizeText(uint64_t bytes);

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(CacheManagerViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.items_.size()); }
		int32_t columnCount() const override { return 11; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		CacheManagerViewModel &vm_;
	};
	void survey();
	std::vector<Item> items_;
	Rows rows_{*this};
	Connection folderWatch_, inUseWatch_;
};

} // namespace chimera::vm
