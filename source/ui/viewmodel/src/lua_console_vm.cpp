#include "chimera/vm/lua_console_vm.hpp"

#include <fstream>
#include <sstream>

namespace chimera::vm {

namespace {
/* the most lines kept: a script printing every frame would otherwise grow without end */
constexpr size_t kMostLines = 10000;
}

LuaConsoleViewModel::LuaConsoleViewModel(Dispatcher &dispatcher, LuaBridge &lua, std::function<void(const std::string &)> echo)
	: ViewModel("luaConsole", dispatcher), lua_(lua), echo_(std::move(echo))
{
	expose("script", "the script being edited", script, true);
	expose("path", "the file the script was read from or saved to", path);
	expose("lines", "lines of output", lines);
	rows("output", "what scripts printed and what failed, a row a line", rows_);
	lua_.onLog = [this](const std::string &line) { append(line); };

	expose("running", "something of a script still runs (a frame function, or a script waiting for a frame)", running);
	command("run", "runs the script; what it gives chimera.onFrame keeps running after every frame, and where it waits "
		"for a frame (emu.frameadvance) it goes on after the next", {}, [this](const Args &, std::string &) -> Value {
			std::string error;
			const std::string name = path.get().empty() ? std::string("console") : path.get();
			if (!lua_.run(script.get(), name, error)) append("error: " + error);
			running.set(lua_.running());
			return {};
		});
	command("stop", "every script stopped: its frame functions and what waits for a frame", {}, [this](const Args &, std::string &) -> Value {
		lua_.stopAll();
		running.set(false);
		append("stopped");
		return {};
	});
	command("toggleLast", "Chimera's Toggle Last Lua Script: stopped when running, else run again", {}, [this](const Args &, std::string &) -> Value {
		std::string error;
		if (lua_.running()) run("stop", {}, error);
		else run("run", {}, error);
		return running.get();
	});
	command("clear", "empties the output", {}, [this](const Args &, std::string &) -> Value {
		output_.clear();
		lines.set(0);
		rows_.reset.emit();
		return {};
	});
	command("load", "reads a script from a file", {"path"}, [this](const Args &a, std::string &error) -> Value {
		std::string p;
		if (!arg(a, 0, p, error)) return {};
		std::ifstream in(p, std::ios::binary);
		if (!in)
		{
			error = "cannot read " + p;
			return {};
		}
		std::stringstream ss;
		ss << in.rdbuf();
		script.set(ss.str());
		path.set(p);
		return {};
	});
	command("save", "writes the script to its file, or to the one given", {"path"}, [this](const Args &a, std::string &error) -> Value {
		std::string p = path.get();
		if (!a.empty() && !arg(a, 0, p, error)) return {};
		if (p.empty())
		{
			error = "no file to save to";
			return {};
		}
		std::ofstream out(p, std::ios::binary);
		out << script.get();
		if (!out)
		{
			error = "cannot write " + p;
			return {};
		}
		path.set(p);
		return {};
	});
}

void LuaConsoleViewModel::frameDone()
{
	lua_.frameDone();
	const bool now = lua_.running();
	if (now != running.get()) running.set(now);
}

void LuaConsoleViewModel::append(const std::string &line)
{
	if (echo_) echo_(line);
	const int64_t first = static_cast<int64_t>(output_.size());
	output_.push_back(line);
	if (output_.size() > kMostLines)
	{
		output_.erase(output_.begin(), output_.begin() + static_cast<long>(output_.size() - kMostLines));
		lines.set(static_cast<int64_t>(output_.size()));
		rows_.reset.emit();
		return;
	}
	lines.set(static_cast<int64_t>(output_.size()));
	rows_.appended.emit(first, first);
}

} // namespace chimera::vm
