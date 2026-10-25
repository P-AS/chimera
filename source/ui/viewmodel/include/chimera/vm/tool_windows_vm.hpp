#pragma once
/* What Chimera puts at the end of every tool window's Settings menu, kept per
 * tool: Save Window Position (on unless turned off: the window opens where
 * it was left, at the size it had), Stay on Top, Float From Main Window
 * (off: the window belongs to the main window, above it and minimized with
 * it), and Restore Defaults. One JSON object, by tool name, remembered:
 * {"luaConsole": {"save": true, "top": false, "float": false, "x": 10,
 * "y": 20, "w": 760, "h": 460}}. */

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class ToolWindowsViewModel : public ViewModel
{
public:
	explicit ToolWindowsViewModel(Dispatcher &dispatcher);

	Property<std::string> settings{"{}"};
	/* a tool window asked to be shown (by a script: Chimera's client.opentasstudio), by --show's names; the
	 * window shows it and the request goes; and the program asked to close with an exit code (client.exit) */
	Property<std::string> showAsked;
	Property<int64_t> exitAsked{-1};

	/* a tool's option ("save", "top", "float"), as it is or by default */
	bool option(const std::string &tool, const std::string &name) const;

	/* What only the window can do or say, for scripts (Chimera's client
	 * library), given by the program: a screenshot to a file ("" for the
	 * usual name and folder), the picture to the clipboard, the window sized
	 * again to its scale, the main window's place on the screen, and the tool
	 * windows open, by Chimera's names for its tools (luaconsole, tastudio) */
	std::function<void(const std::string &path)> screenshot;
	std::function<void()> screenshotToClipboard, resizeWindow;
	std::function<std::pair<int64_t, int64_t>()> windowPosition;
	std::function<std::vector<std::string>()> openTools;
};

} // namespace chimera::vm
