#pragma once
/* The Lua Console, as Chimera's: a list of scripts, each started and stopped
 * (toggled), paused, moved, duplicated or taken out, with separators between
 * them; the list kept as a session (a .luases file: a line a script, "1 " or
 * "0 " for whether it runs, then its path, and "---" for a separator); what
 * the scripts print; a line to type Lua into; and the functions the scripts
 * registered, to call or remove. The scripts run in the bridge. */

#include <filesystem>
#include <string>
#include <vector>

#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class LuaConsoleViewModel : public ViewModel
{
public:
	/* takes over the bridge's output: everything it prints comes here (and to
	 * `echo` a line at a time, when given and Duplicate to Stdout is on) */
	LuaConsoleViewModel(Dispatcher &dispatcher, LuaBridge &lua, std::function<void(const std::string &)> echo = {});
	/* Chimera's Lua Functions List made from its documentation and the
	 * prototype's own list ([{"library", "name", "params"}]), as JSON */
	static std::string functionsList(const std::string &prototypeList);
	~LuaConsoleViewModel() override;

	/* the session's file, "" until one is opened or saved */
	Property<std::string> sessionPath;
	/* the list changed since the session was opened or saved */
	Property<bool> changes{false};
	/* the line under the window: the session's file name, "* " before it
	 * while changed, or what saving it did */
	Property<std::string> status;
	/* "2 scripts (1 active, 0 paused)", as Chimera counts them */
	Property<std::string> summary{"0 scripts"};
	Property<int64_t> lines{0};
	/* any script running or paused */
	Property<bool> running{false};
	/* rows selected, and of them the scripts (not separators) */
	Property<int64_t> selectedCount{0};
	Property<int64_t> selectedScripts{0};
	/* any script in the list running or paused */
	Property<bool> anyEnabled{false};
	Property<int64_t> functionCount{0};
	/* the script Toggle Last Lua Script toggles: the last one toggled */
	Property<std::string> lastScript;
	/* the output lines selected (for Copy), first..last; -1 for none */
	Property<int64_t> outputFirst{-1};
	Property<int64_t> outputLast{-1};

	/* Settings, remembered */
	Property<bool> disableOnLoad{false};
	Property<bool> toggleAllIfNoneSelected{true};
	Property<bool> reloadOnChange{false};
#ifdef _WIN32
	Property<bool> duplicateToStdout{false};
#else
	Property<bool> duplicateToStdout{true};
#endif
	/* newest first, a line each, remembered */
	Property<std::string> recentSessions;
	Property<std::string> recentScripts;
	/* the Paths' Lua folder: where New Script's template is kept */
	Property<std::string> luaFolder;

	/* said again after each frame (the window calls it with the frame) */
	void frameDone();
	/* text the scripts wrote, line ends and all */
	void append(const std::string &text);

private:
	struct Entry
	{
		std::string path;
		bool separator = false;
		/* the bridge's number for it while it runs or is paused, else 0 */
		int64_t script = 0;
		bool selected = false;
		std::filesystem::file_time_type written{};
	};
	struct Function
	{
		std::string event, name, guid;
	};

	class Scripts : public RowModel
	{
	public:
		explicit Scripts(LuaConsoleViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.entries_.size()); }
		int32_t columnCount() const override { return 5; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		LuaConsoleViewModel &vm_;
	};
	class Output : public RowModel
	{
	public:
		explicit Output(LuaConsoleViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.output_.size()); }
		int32_t columnCount() const override { return 2; }
		std::string columnName(int32_t column) const override { return column == 0 ? "line" : "selected"; }
		Value cell(int64_t row, int32_t column) const override;

	private:
		LuaConsoleViewModel &vm_;
	};
	class Functions : public RowModel
	{
	public:
		explicit Functions(LuaConsoleViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.functions_.size()); }
		int32_t columnCount() const override { return 3; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		LuaConsoleViewModel &vm_;
	};

	/* a script's file into the list: started unless Disable Scripts on Load
	 * (or `start` is false); one already there is started if stopped */
	void openScript(const std::string &path, bool start);
	void enable(Entry &e);
	void toggle(Entry &e);
	void removeAll();
	bool loadSession(const std::string &path, std::string &error);
	bool saveSession(const std::string &path, std::string &error);
	/* the entries Toggle and Refresh work on: those selected, or all when
	 * none is and Toggle All if None Selected is on */
	std::vector<size_t> targets() const;
	std::vector<size_t> selectedRows() const;
	void listChanged();
	void markChanged();
	void refresh();
	void refreshFunctions(bool always);
	void addRecent(Property<std::string> &list, const std::string &path);
	void line(const std::string &text);

	LuaBridge &lua_;
	std::function<void(const std::string &)> echo_;
	std::vector<Entry> entries_;
	std::vector<std::string> output_;
	std::vector<Function> functions_;
	std::vector<std::string> history_;
	int64_t historyAt_ = -1;
	/* the last line has no line end yet (console.write) */
	bool open_ = false;
	/* what was written since the last frame: past 100 the rest is not shown, as Chimera's */
	int messages_ = 0;
	int64_t anchor_ = -1;
	int64_t outputAnchor_ = -1;
	std::string sortedBy_;
	bool sortReverse_ = false;
	std::string functionsStamp_;
	Output rows_{*this};
	Scripts scripts_{*this};
	Functions functionRows_{*this};
	Connection changesWatch_, sessionWatch_;
};

} // namespace chimera::vm
