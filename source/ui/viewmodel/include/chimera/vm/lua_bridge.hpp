#pragma once
/* Lua scripts over the view models. Each view model is a table under
 * `chimera`, by its name: a property is read and set as a field
 * (chimera.session.frame, chimera.pianoRoll.followPlayback = false) and a
 * command is a function (chimera.session.play(), chimera.pianoRoll.toggle(5, 0)).
 * Nothing here knows any view model: what a registry has, a script has, and
 * the MCP server would list the same names (describe()).
 *
 * chimera.onFrame(fn) runs fn after every frame the window learns of, and
 * print() writes to the bridge's log. One thread: the view's. */

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

	/* runs a chunk; false with the message in `error` when it fails */
	bool run(const std::string &code, const std::string &chunkName, std::string &error);

	/* calls every chimera.onFrame function; a failing one is dropped and its error logged */
	void frameDone();

	/* what print() wrote and what failed, a line each */
	std::function<void(const std::string &)> onLog;

	/* every view model's properties and commands with their help, as JSON:
	 * what an MCP server's tool list would be made from */
	std::string describe() const;

private:
	static int index(lua_State *L);
	static int newIndex(lua_State *L);
	static int call(lua_State *L);
	static int print(lua_State *L);
	static int onFrame(lua_State *L);
	static void push(lua_State *L, const Value &v);
	static bool read(lua_State *L, int at, Value &out);
	void log(const std::string &line) const;

	lua_State *L_;
	std::vector<ViewModel *> models_;
	int frameFns_ = 0; /* a registry reference to the table of onFrame functions */
};

} // namespace chimera::vm
