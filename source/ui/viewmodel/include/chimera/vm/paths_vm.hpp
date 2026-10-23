#pragma once
/* Chimera's Paths window (Config > Paths), its global folders: where
 * games are looked for, firmware, projects, their backups, screenshots,
 * Lua scripts, watch lists, macros, bug reports and themes. Each is
 * a path, absolute or relative to the base folder (the program's own at
 * first, as Chimera's "."); the window and its other parts ask here where a
 * folder is (resolve). Changed paths are remembered; the rest stay
 * Chimera's defaults. "Use recent for ROMs" opens the game dialog where
 * the last game came from. */

#include <string>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class PathsViewModel : public ViewModel
{
public:
	explicit PathsViewModel(Dispatcher &dispatcher);

	/* the folder relative paths start from; "" the program's (programFolder) */
	Property<std::string> base;
	/* the program's folder, said by the window at the start */
	Property<std::string> programFolder;
	/* the paths changed from Chimera's, {"Movies": "D:/runs", ...} */
	Property<std::string> changed{"{}"};
	/* the game dialog opens where the last game came from */
	Property<bool> recentForRoms{true};
	Property<std::string> lastRomFolder;

	/* Chimera's names, in its order */
	static const std::vector<std::pair<std::string, std::string>> &defaults();
	/* a folder as it is: its path made absolute; "" for a name that is not one */
	std::string resolve(const std::string &name) const;
	/* a path as it is set (changed or Chimera's) */
	std::string pathOf(const std::string &name) const;
	/* any of them changed or moved */
	Signal<> moved;

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(PathsViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override;
		int32_t columnCount() const override { return 4; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		PathsViewModel &vm_;
	};
	Rows rows_{*this};
	Connection baseWatch_, programWatch_, changedWatch_;
};

} // namespace chimera::vm
