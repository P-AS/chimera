#include "chimera/vm/tool_windows_vm.hpp"

#include "chimera/vm/json.hpp"

namespace chimera::vm {

using json::Json;

namespace {
Json read(const std::string &text)
{
	Json j;
	std::string error;
	if (!json::parse(text, j, error) || j.kind != Json::Kind::Object)
	{
		j = Json{};
		j.kind = Json::Kind::Object;
	}
	return j;
}

/* the tool's object in the whole, made when it is not there */
Json &toolOf(Json &all, const std::string &tool)
{
	for (auto &m : all.members)
		if (m.first == tool && m.second.kind == Json::Kind::Object) return m.second;
	Json t;
	t.kind = Json::Kind::Object;
	all.members.emplace_back(tool, t);
	return all.members.back().second;
}

void put(Json &tool, const std::string &name, Json value)
{
	for (auto &m : tool.members)
		if (m.first == name)
		{
			m.second = std::move(value);
			return;
		}
	tool.members.emplace_back(name, std::move(value));
}

bool known(const std::string &name) { return name == "save" || name == "top" || name == "float"; }
} // namespace

ToolWindowsViewModel::ToolWindowsViewModel(Dispatcher &dispatcher) : ViewModel("toolWindows", dispatcher)
{
	expose("showAsked", "a tool window asked to be shown, by the names --show takes (\"tastudio\"); \"\" when none is", showAsked, true);
	expose("exitAsked", "the program asked to close, with this exit code; -1 when it is not", exitAsked);
	command("show", "asks the window to show a tool window, by the names --show takes (Chimera's client.opentasstudio)", {"tool"},
		[this](const Args &a, std::string &error) -> Value {
			std::string tool;
			if (!arg(a, 0, tool, error)) return {};
			showAsked.set("");
			showAsked.set(tool);
			return {};
		});
	command("exit", "asks the program to close, as Chimera's client.exit, with this exit code; a project with changes asks first",
		{"code"}, [this](const Args &a, std::string &error) -> Value {
			int64_t code = 0;
			if (!a.empty() && !arg(a, 0, code, error)) return {};
			exitAsked.set(std::max<int64_t>(0, code));
			return {};
		});
	expose("settings", "each tool window's options and place, by tool: save (Save Window Position), top (Stay on Top), "
		"float (Float From Main Window), x, y, w, h", settings, true);
	command("setOption", "a tool window's option set: \"save\" (Save Window Position), \"top\" (Stay on Top) or \"float\" "
		"(Float From Main Window)", {"tool", "option", "on"}, [this](const Args &a, std::string &error) -> Value {
			std::string tool, name;
			bool on = false;
			if (!arg(a, 0, tool, error) || !arg(a, 1, name, error) || !arg(a, 2, on, error)) return {};
			if (!known(name))
			{
				error = "no option " + name + ": save, top or float";
				return {};
			}
			Json all = read(settings.get());
			Json &t = toolOf(all, tool);
			put(t, name, Json::boolean_(on));
			/* a place not to be kept is forgotten */
			if (name == "save" && !on)
			{
				std::vector<std::pair<std::string, Json>> kept;
				for (auto &m : t.members)
					if (m.first != "x" && m.first != "y" && m.first != "w" && m.first != "h") kept.push_back(m);
				t.members = kept;
			}
			settings.set(json::write(all));
			return {};
		});
	command("place", "where a tool window was left and its size, kept when Save Window Position is on", {"tool", "x", "y", "w", "h"},
		[this](const Args &a, std::string &error) -> Value {
			std::string tool;
			int64_t x = 0, y = 0, w = 0, h = 0;
			if (!arg(a, 0, tool, error) || !arg(a, 1, x, error) || !arg(a, 2, y, error) || !arg(a, 3, w, error) || !arg(a, 4, h, error)) return {};
			if (!option(tool, "save") || w <= 0 || h <= 0) return false;
			Json all = read(settings.get());
			Json &t = toolOf(all, tool);
			put(t, "x", Json::integer(x));
			put(t, "y", Json::integer(y));
			put(t, "w", Json::integer(w));
			put(t, "h", Json::integer(h));
			const std::string text = json::write(all);
			if (text != settings.get()) settings.set(text);
			return true;
		});
	command("restoreDefaults", "a tool window's options back to Chimera's (Save Window Position on, the others off) and its place forgotten",
		{"tool"}, [this](const Args &a, std::string &error) -> Value {
			std::string tool;
			if (!arg(a, 0, tool, error)) return {};
			Json all = read(settings.get());
			std::vector<std::pair<std::string, Json>> kept;
			for (auto &m : all.members)
				if (m.first != tool) kept.push_back(m);
			all.members = kept;
			settings.set(json::write(all));
			return {};
		});
}

bool ToolWindowsViewModel::option(const std::string &tool, const std::string &name) const
{
	const Json all = read(settings.get());
	const Json *t = all.find(tool);
	const Json *v = t != nullptr ? t->find(name) : nullptr;
	if (v != nullptr && v->kind == Json::Kind::Bool) return v->boolean;
	/* Chimera's: Save Window Position on, Stay on Top and Float From Main Window off */
	return name == "save";
}

} // namespace chimera::vm
