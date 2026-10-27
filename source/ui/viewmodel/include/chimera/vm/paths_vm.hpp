#pragma once
/* Chimera's Paths window (Config > Paths): the global folders (the base,
 * where games are looked for, firmware, projects and their backups, video
 * dumps, tools, Lua scripts, themes, watch lists, debug logs, bug reports,
 * macros, temporary files) and, for each system a game has been opened
 * for, its own base, games, savestates and screenshots. A path is absolute, or starts with "." for
 * its base (a system's own base, which starts from the global one, which
 * starts from the program's folder), or with %exe% (the program's folder),
 * %rom% (where the last game came from) or %recent% (the system's recent
 * files folder); anything else is the program's folder, as Chimera's
 * AbsolutePathFor decides. The window and its other parts ask here where a
 * folder is (resolve, resolveFor). Changed paths are remembered; the rest
 * stay Chimera's. "Always use recent path for ROMs" opens the game dialog
 * where the last game came from. */

#include <string>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class PathsViewModel : public ViewModel
{
public:
	explicit PathsViewModel(Dispatcher &dispatcher);

	/* the global base, as set ("" for Chimera's "."); relative paths start from it */
	Property<std::string> base;
	/* the program's folder, said by the window at the start */
	Property<std::string> programFolder;
	/* the global paths changed from Chimera's, {"Movies": "D:/runs", ...} */
	Property<std::string> changed{"{}"};
	/* the systems that have folders of their own (a game was opened for each), and
	 * their paths changed from Chimera's: {"NES": {"Screenshots": "./Shots"}} */
	Property<std::string> systems{"[]"};
	Property<std::string> systemChanged{"{}"};
	/* the game dialog opens where the last game came from */
	Property<bool> recentForRoms{true};
	Property<std::string> lastRomFolder;

	/* Chimera's global names and paths, in its order */
	static const std::vector<std::pair<std::string, std::string>> &defaults();
	/* Chimera's for a system: Base ("./<id>"), ROM, Savestates, Screenshots */
	static std::vector<std::pair<std::string, std::string>> systemDefaults(const std::string &system);
	/* a global folder as it is: its path made absolute; "" for a name that is not one */
	std::string resolve(const std::string &name) const;
	/* a system's folder, as Chimera does: the system's own path of that
	 * kind, else the system's base, else the global base */
	std::string resolveFor(const std::string &system, const std::string &name) const;
	/* a path as it is set (changed or Chimera's) */
	std::string pathOf(const std::string &name) const;
	std::string pathOf(const std::string &system, const std::string &name) const;
	/* a path made absolute, for a system or for none ("") */
	std::string absolute(const std::string &path, const std::string &system) const;
	/* a system gets folders of its own (a game for it was opened) */
	void useSystem(const std::string &system);
	/* any of them changed or moved */
	Signal<> moved;

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(PathsViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override;
		int32_t columnCount() const override { return 5; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		PathsViewModel &vm_;
	};
	/* the systems with folders, by name (Chimera's order) */
	std::vector<std::string> systemList() const;
	std::string globalBase() const;
	Rows rows_{*this};
	Connection baseWatch_, programWatch_, changedWatch_, systemsWatch_, systemChangedWatch_;
};

} // namespace chimera::vm
