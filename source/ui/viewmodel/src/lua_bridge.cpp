#include "chimera/vm/lua_bridge.hpp"

#include <cstdio>
#include <optional>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace chimera::vm {

namespace {
const char *const kBridge = "chimera.bridge";
const char *const kModel = "chimera.viewmodel";

LuaBridge *bridgeOf(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, kBridge);
	auto *b = static_cast<LuaBridge *>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return b;
}

std::string json(const std::string &s)
{
	std::string out = "\"";
	for (const char c : s)
	{
		if (c == '"' || c == '\\') { out += '\\'; out += c; }
		else if (c == '\n') out += "\\n";
		else if (static_cast<unsigned char>(c) < 0x20)
		{
			char buf[8];
			std::snprintf(buf, sizeof buf, "\\u%04x", c);
			out += buf;
		}
		else out += c;
	}
	return out + "\"";
}
} // namespace

LuaBridge::LuaBridge() : L_(luaL_newstate())
{
	luaL_openlibs(L_);
	lua_pushlightuserdata(L_, this);
	lua_setfield(L_, LUA_REGISTRYINDEX, kBridge);

	/* a view model in Lua: a userdata holding its pointer, read and written by name */
	luaL_newmetatable(L_, kModel);
	lua_pushcfunction(L_, &LuaBridge::index);
	lua_setfield(L_, -2, "__index");
	lua_pushcfunction(L_, &LuaBridge::newIndex);
	lua_setfield(L_, -2, "__newindex");
	lua_pop(L_, 1);

	lua_newtable(L_);
	frameFns_ = luaL_ref(L_, LUA_REGISTRYINDEX);
	lua_newtable(L_);
	waiting_ = luaL_ref(L_, LUA_REGISTRYINDEX);

	lua_newtable(L_);
	lua_pushcfunction(L_, &LuaBridge::onFrame);
	lua_setfield(L_, -2, "onFrame");
	lua_setglobal(L_, "chimera");
	lua_pushcfunction(L_, &LuaBridge::print);
	lua_setglobal(L_, "print");
	/* Chimera's libraries, a layer of Lua over the view models */
	std::string error;
	if (!run(luaLibraries(), "chimera-libraries", error)) std::fprintf(stderr, "lua: the libraries did not load: %s\n", error.c_str());
}

LuaBridge::~LuaBridge() { lua_close(L_); }

void LuaBridge::add(ViewModel &vm)
{
	models_.push_back(&vm);
	lua_getglobal(L_, "chimera");
	*static_cast<ViewModel **>(lua_newuserdatauv(L_, sizeof(ViewModel *), 0)) = &vm;
	luaL_setmetatable(L_, kModel);
	lua_setfield(L_, -2, vm.name().c_str());
	lua_pop(L_, 1);
}

void LuaBridge::push(lua_State *L, const Value &v)
{
	if (const bool *b = std::get_if<bool>(&v)) lua_pushboolean(L, *b);
	else if (const int64_t *i = std::get_if<int64_t>(&v)) lua_pushinteger(L, static_cast<lua_Integer>(*i));
	else if (const double *d = std::get_if<double>(&v)) lua_pushnumber(L, *d);
	else if (const std::string *s = std::get_if<std::string>(&v)) lua_pushlstring(L, s->data(), s->size());
	else lua_pushnil(L);
}

bool LuaBridge::read(lua_State *L, int at, Value &out)
{
	switch (lua_type(L, at))
	{
	case LUA_TNIL: out = std::monostate{}; return true;
	case LUA_TBOOLEAN: out = lua_toboolean(L, at) != 0; return true;
	case LUA_TNUMBER:
		if (lua_isinteger(L, at)) out = static_cast<int64_t>(lua_tointeger(L, at));
		else out = static_cast<double>(lua_tonumber(L, at));
		return true;
	case LUA_TSTRING:
	{
		size_t n = 0;
		const char *s = lua_tolstring(L, at, &n);
		out = std::string(s, n);
		return true;
	}
	default: return false;
	}
}

/* Lua reports an error by jumping out of the C function (longjmp), past any
 * C++ object's destructor. So each function below does its C++ work in an
 * inner block, leaves the message on the Lua stack, and raises it only once
 * that block has ended. */

int LuaBridge::index(lua_State *L)
{
	ViewModel *vm = *static_cast<ViewModel **>(luaL_checkudata(L, 1, kModel));
	const char *k = luaL_checkstring(L, 2);
	bool failed = false;
	{
		const std::string key = k;
		if (const auto v = vm->get(key))
			push(L, *v);
		else
		{
			bool command = false;
			for (const auto &c : vm->commands()) command = command || c.name == key;
			if (command)
			{
				/* the command as a function, holding its view model and name */
				lua_pushlightuserdata(L, vm);
				lua_pushstring(L, key.c_str());
				lua_pushcclosure(L, &LuaBridge::call, 2);
			}
			else
			{
				lua_pushstring(L, (vm->name() + " has no property or command " + key).c_str());
				failed = true;
			}
		}
	}
	return failed ? lua_error(L) : 1;
}

int LuaBridge::newIndex(lua_State *L)
{
	ViewModel *vm = *static_cast<ViewModel **>(luaL_checkudata(L, 1, kModel));
	const char *k = luaL_checkstring(L, 2);
	bool failed = false;
	{
		const std::string key = k;
		Value v;
		std::string error;
		if (!read(L, 3, v)) error = vm->name() + "." + key + " cannot be a " + luaL_typename(L, 3);
		else vm->set(key, v, error);
		if (!error.empty())
		{
			lua_pushstring(L, error.c_str());
			failed = true;
		}
	}
	return failed ? lua_error(L) : 0;
}

int LuaBridge::call(lua_State *L)
{
	auto *vm = static_cast<ViewModel *>(lua_touserdata(L, lua_upvalueindex(1)));
	const int argc = lua_gettop(L);
	bool failed = false;
	{
		const std::string name = lua_tostring(L, lua_upvalueindex(2));
		Args args;
		std::string error;
		for (int i = 1; i <= argc && error.empty(); i++)
		{
			Value v;
			if (!read(L, i, v))
				error = vm->name() + "." + name + ": argument " + std::to_string(i) + " cannot be a " + luaL_typename(L, i);
			args.push_back(std::move(v));
		}
		std::optional<Value> result;
		if (error.empty()) result = vm->run(name, args, error);
		if (result) push(L, *result);
		else
		{
			lua_pushstring(L, error.c_str());
			failed = true;
		}
	}
	return failed ? lua_error(L) : 1;
}

int LuaBridge::print(lua_State *L)
{
	const int n = lua_gettop(L);
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	for (int i = 1; i <= n; i++)
	{
		if (i > 1) luaL_addchar(&b, '\t');
		luaL_tolstring(L, i, nullptr);
		luaL_addvalue(&b);
	}
	luaL_pushresult(&b);
	bridgeOf(L)->log(lua_tostring(L, -1));
	return 0;
}

int LuaBridge::onFrame(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	LuaBridge *b = bridgeOf(L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, b->frameFns_);
	lua_pushvalue(L, 1);
	lua_rawseti(L, -2, static_cast<lua_Integer>(lua_rawlen(L, -2)) + 1);
	lua_pop(L, 1);
	return 0;
}

void LuaBridge::log(const std::string &line) const
{
	if (onLog) onLog(line);
}

bool LuaBridge::run(const std::string &code, const std::string &chunkName, std::string &error)
{
	if (luaL_loadbuffer(L_, code.data(), code.size(), ("@" + chunkName).c_str()) != LUA_OK)
	{
		error = lua_tostring(L_, -1) != nullptr ? lua_tostring(L_, -1) : "unknown error";
		lua_pop(L_, 1);
		return false;
	}
	/* a coroutine of its own: where it yields, it waits for the next frame */
	lua_State *co = lua_newthread(L_);
	lua_insert(L_, -2);
	lua_xmove(L_, co, 1);
	int results = 0;
	const int status = lua_resume(co, L_, 0, &results);
	if (status == LUA_OK)
	{
		lua_pop(co, results);
		lua_pop(L_, 1);
		return true;
	}
	if (status == LUA_YIELD)
	{
		lua_pop(co, results);
		lua_rawgeti(L_, LUA_REGISTRYINDEX, waiting_);
		lua_insert(L_, -2);
		lua_rawseti(L_, -2, static_cast<lua_Integer>(lua_rawlen(L_, -2)) + 1);
		lua_pop(L_, 1);
		return true;
	}
	error = lua_tostring(co, -1) != nullptr ? lua_tostring(co, -1) : "unknown error";
	lua_pop(L_, 1);
	return false;
}

void LuaBridge::stopAll()
{
	lua_newtable(L_);
	lua_rawseti(L_, LUA_REGISTRYINDEX, frameFns_);
	lua_newtable(L_);
	lua_rawseti(L_, LUA_REGISTRYINDEX, waiting_);
}

bool LuaBridge::running() const
{
	lua_rawgeti(L_, LUA_REGISTRYINDEX, frameFns_);
	const bool fns = lua_rawlen(L_, -1) > 0;
	lua_pop(L_, 1);
	lua_rawgeti(L_, LUA_REGISTRYINDEX, waiting_);
	const bool scripts = lua_rawlen(L_, -1) > 0;
	lua_pop(L_, 1);
	return fns || scripts;
}

void LuaBridge::frameDone()
{
	lua_rawgeti(L_, LUA_REGISTRYINDEX, frameFns_);
	const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L_, -1));
	std::vector<lua_Integer> failed;
	for (lua_Integer i = 1; i <= n; i++)
	{
		lua_rawgeti(L_, -1, i);
		if (lua_pcall(L_, 0, 0, 0) != LUA_OK)
		{
			log(std::string("onFrame function dropped: ") + (lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "?"));
			lua_pop(L_, 1);
			failed.push_back(i);
		}
	}
	/* drop the failing ones, last first so the others keep their places */
	for (auto it = failed.rbegin(); it != failed.rend(); ++it)
	{
		const lua_Integer len = static_cast<lua_Integer>(lua_rawlen(L_, -1));
		for (lua_Integer j = *it; j < len; j++)
		{
			lua_rawgeti(L_, -1, j + 1);
			lua_rawseti(L_, -2, j);
		}
		lua_pushnil(L_);
		lua_rawseti(L_, -2, len);
	}
	lua_pop(L_, 1);

	/* the scripts that waited for a frame go on, each to its next yield;
	 * the ones that finish or fail are let go (a list taken first, so one
	 * that starts another script does not change it under this loop) */
	lua_rawgeti(L_, LUA_REGISTRYINDEX, waiting_);
	const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L_, -1));
	lua_newtable(L_);
	lua_rawseti(L_, LUA_REGISTRYINDEX, waiting_);
	for (lua_Integer i = 1; i <= count; i++)
	{
		lua_rawgeti(L_, -1, i);
		lua_State *co = lua_tothread(L_, -1);
		int results = 0;
		const int status = co != nullptr ? lua_resume(co, L_, 0, &results) : LUA_ERRRUN;
		if (status == LUA_YIELD)
		{
			lua_pop(co, results);
			lua_rawgeti(L_, LUA_REGISTRYINDEX, waiting_);
			lua_pushvalue(L_, -2);
			lua_rawseti(L_, -2, static_cast<lua_Integer>(lua_rawlen(L_, -2)) + 1);
			lua_pop(L_, 1);
		}
		else if (status == LUA_OK)
			lua_pop(co, results);
		else if (co != nullptr)
			log(std::string("script stopped: ") + (lua_tostring(co, -1) ? lua_tostring(co, -1) : "?"));
		lua_pop(L_, 1);
	}
	lua_pop(L_, 1);
}

std::string LuaBridge::describe() const
{
	std::string out = "[";
	for (size_t m = 0; m < models_.size(); m++)
	{
		const ViewModel &vm = *models_[m];
		out += m ? ",\n" : "\n";
		out += " {\"name\": " + json(vm.name()) + ", \"properties\": [";
		for (size_t i = 0; i < vm.properties().size(); i++)
		{
			const auto &p = vm.properties()[i];
			out += (i ? ", " : "") + std::string("{\"name\": ") + json(p.name) + ", \"help\": " + json(p.help) +
				", \"settable\": " + (p.set ? "true" : "false") + "}";
		}
		out += "], \"commands\": [";
		for (size_t i = 0; i < vm.commands().size(); i++)
		{
			const auto &c = vm.commands()[i];
			out += (i ? ", " : "") + std::string("{\"name\": ") + json(c.name) + ", \"help\": " + json(c.help) + ", \"params\": [";
			for (size_t k = 0; k < c.params.size(); k++) out += (k ? ", " : "") + json(c.params[k]);
			out += "]}";
		}
		out += "]}";
	}
	return out + "\n]\n";
}

} // namespace chimera::vm
