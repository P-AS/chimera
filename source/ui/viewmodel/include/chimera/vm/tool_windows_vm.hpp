#pragma once
/* What Chimera puts at the end of every tool window's Settings menu, kept per
 * tool: Save Window Position (on unless turned off: the window opens where
 * it was left, at the size it had), Stay on Top, Float From Main Window
 * (off: the window belongs to the main window, above it and minimized with
 * it), and Restore Defaults. One JSON object, by tool name, remembered:
 * {"luaConsole": {"save": true, "top": false, "float": false, "x": 10,
 * "y": 20, "w": 760, "h": 460}}. */

#include <string>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class ToolWindowsViewModel : public ViewModel
{
public:
	explicit ToolWindowsViewModel(Dispatcher &dispatcher);

	Property<std::string> settings{"{}"};

	/* a tool's option ("save", "top", "float"), as it is or by default */
	bool option(const std::string &tool, const std::string &name) const;
};

} // namespace chimera::vm
