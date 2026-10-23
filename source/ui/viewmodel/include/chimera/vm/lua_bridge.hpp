#pragma once
/* Lua scripts over the view models. Each view model is a table under
 * `chimera`, by its name: a property is read and set as a field
 * (chimera.session.frame, chimera.pianoRoll.followPlayback = false) and a
 * command is a function (chimera.session.play(), chimera.pianoRoll.toggle(5, 0)).
 * Nothing here knows any view model: what a registry has, a script has, and
 * the MCP server would list the same names (describe()).
 *
 * Scripts run one beside another, as in Chimera's Lua Console: each is
 * started from its file (or text) as a coroutine of its own, and where it
 * yields (emu.frameadvance()) it goes on after the next frame. What it
 * registers (event.onframeend, tastudio.onbranchload, ...) is its own: called
 * as that script, and gone when it stops. A script whose code has ended goes
 * on while it has something registered or a form open, as Chimera's do. While
 * a script's code runs the working folder is the script's, as Chimera's.
 * Chimera's Lua libraries (emu, memory, mainmemory, joypad, movie, client,
 * console, gameinfo, event, bit, ...) are a layer of Lua over the view models,
 * defined at the start (lua_libraries.cpp). One thread: the view's. */

#include <functional>
#include <string>
#include <vector>

#include "chimera/vm/view_model.hpp"

struct lua_State;

namespace chimera::vm {

class LuaBridge
{
public:
	LuaBridge();
	~LuaBridge();
	LuaBridge(const LuaBridge &) = delete;
	LuaBridge &operator=(const LuaBridge &) = delete;

	void add(ViewModel &vm);

	enum class State { Stopped, Running, Paused };

	/* a script from a file: read, loaded and started, run to where it first
	 * waits; its number, or 0 with what went wrong in `error` */
	int64_t start(const std::string &path, std::string &error);
	/* a script from text; its working folder `directory` ("" for the
	 * program's), its name in messages `chunkName` */
	int64_t startCode(const std::string &code, const std::string &chunkName, const std::string &directory, std::string &error);
	/* a chunk run as a script of its own; false with the message in `error` when it fails */
	bool run(const std::string &code, const std::string &chunkName, std::string &error)
	{
		return startCode(code, chunkName, "", error) != 0;
	}
	/* a script stopped: its OnExit functions called, then everything it registered gone */
	void stop(int64_t script);
	/* a paused script's code waits; its registered functions are still called, as Chimera's */
	void setPaused(int64_t script, bool paused);
	State state(int64_t script) const;
	/* every script stopped */
	void stopAll();
	/* whether any script is running or paused */
	bool running() const;
	/* told when a script stops, whatever stopped it (its end, its error, stop()) */
	std::function<void(int64_t)> onStopped;

	/* the console's line: an expression's values, or a statement run, as the
	 * console's own (script 0); what it gives back is printed, as Chimera's */
	void execute(const std::string &command);

	/* called after every frame: the frame's registered functions, then every
	 * running script that waits for a frame goes on, each to its next wait;
	 * one that ends with nothing registered, or fails, is stopped */
	void frameDone();
	/* the console closing: its OnConsoleClose functions, then every script stopped */
	void consoleClosed();

	/* what print() wrote and what failed, a line each */
	std::function<void(const std::string &)> onLog;
	/* what scripts wrote as it is written (console.write writes no line end);
	 * when set, it is told instead of onLog */
	std::function<void(const std::string &)> onWrite;

	/* every view model's properties and commands with their help, as JSON:
	 * what an MCP server's tool list would be made from */
	std::string describe() const;

	/* a global function of the libraries' called with a number, if it is
	 * there (a form's click, a form closed) */
	void callHook(const char *name, int64_t arg);
	/* a global function of the libraries' asked about a cell (row, column):
	 * its answer as text, "" for none */
	std::string query(const char *name, int64_t row, const std::string &column);
	/* a global function of the libraries' called with these: its first answer
	 * as text ("true" for true, "" for none or false) */
	std::string ask(const char *name, const std::vector<Value> &args = {});

private:
	struct Script
	{
		int64_t id = 0;
		std::string directory;
		State state = State::Running;
		/* a registry reference to its coroutine; none once its code has ended */
		int thread = -2;
		/* stop() was asked while its code ran: stopped when it next waits */
		bool stopAfter = false;
	};

	static int index(lua_State *L);
	static int newIndex(lua_State *L);
	static int call(lua_State *L);
	static int print(lua_State *L);
	static int write(lua_State *L);
	static int enter(lua_State *L);
	static int folder(lua_State *L);
	static void push(lua_State *L, const Value &v);
	static bool read(lua_State *L, int at, Value &out);
	void log(const std::string &line);
	void writeText(const std::string &text);
	static const char *luaLibraries();
	/* a global function of the libraries' called, if it is there */
	void callHook(const char *name);
	/* the script whose code runs now: the global __chimera_script and the
	 * working folder; the one before back */
	int64_t enterScript(int64_t id);
	Script *find(int64_t id);
	/* goes on with a script's coroutine to its next wait; false when it failed */
	bool resume(int64_t id, std::string &error);
	bool keeps(int64_t id);
	void published();

	lua_State *L_;
	std::vector<ViewModel *> models_;
	std::vector<Script> scripts_;
	int64_t nextId_ = 1;
	int64_t current_ = 0;
	/* the script whose coroutine is being resumed, 0 for none */
	int64_t resuming_ = 0;
	/* the working folder before a script's was taken */
	std::string home_;
	/* what console.write left without a line end, when only onLog listens */
	std::string partial_;
	/* how many times anything was written: whether the console's line printed something itself */
	uint64_t written_ = 0;
};

} // namespace chimera::vm
