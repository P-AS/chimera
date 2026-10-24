#pragma once
/* Chimera's Customize window (Config > Customize), for what the prototype
 * has: how the machine behaves around the window. Each is remembered.
 *
 * - startPaused: the first machine opened waits paused. After that a
 *   machine opened runs when the last one was running and waits when it
 *   was paused, as Chimera's pause is the person's and not the machine's.
 * - startFullscreen: the window opens full screen (read at the start).
 * Nothing else pauses the machine: neither a menu open nor the window going
 *   behind another (owner, 2026-10-24, Chimera's Run in background and
 *   Pause when menu activated removed).
 * - neverAskSave: changes not saved are let go without asking when a
 *   project is closed, another opened or the program quit.
 * - luaDuringTurbo: scripts are run while turbo is held.
 * - showContextMenu: a right click on the picture opens the window's menu
 *   (Chimera's Enable Context Menu).
 * The frame advance skipping lag frames and background input are the
 * session's (skipLagFrames, acceptBackgroundInput). */

#include <cstdint>
#include <memory>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class CustomizeViewModel : public ViewModel
{
public:
	CustomizeViewModel(Dispatcher &dispatcher, SessionViewModel &session);

	Property<bool> startPaused{false}, startFullscreen{false}, neverAskSave{false}, luaDuringTurbo{true}, showContextMenu{true};

	/* whether a script is to be run for the frame now: not while turbo is
	 * held, unless asked */
	bool luaRunsNow() const { return luaDuringTurbo.get() || !session_.turboHeld(); }

private:
	void say(const char *command);
	SessionViewModel &session_;
	/* a machine opened runs: the first by startPaused, the rest as the last was left */
	bool firstOpen_ = true, runOnOpen_ = true;
	uint64_t opensSeen_ = 0;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
	Connection runningWatch_, opensWatch_;
};

} // namespace chimera::vm
