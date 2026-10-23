#pragma once
/* Chimera's Customize window (Config > Customize), for what the prototype
 * has: how the machine behaves around the window. Each is remembered.
 *
 * - startPaused: the first machine opened waits paused. After that a
 *   machine opened runs when the last one was running and waits when it
 *   was paused, as Chimera's pause is the person's and not the machine's.
 * - startFullscreen: the window opens full screen (read at the start).
 * - pauseWhenMenuActivated: a menu open pauses the machine, and closing it
 *   runs the machine again when the menu was what paused it - unless what
 *   was picked from it said what the machine does (play, pause, frame
 *   advance, a seek, a restart): Chimera runs the machine again before the
 *   item's action and Qt runs the action first, so the action is let stand.
 * - runInBackground: off, the machine pauses when the window goes behind
 *   another and runs again when it is back in front. (Chimera's pauses only
 *   when "pause when menu activated" is on too; here it does what its
 *   window says.)
 * - neverAskSave: changes not saved are let go without asking when a
 *   project is closed, another opened or the program quit.
 * - luaDuringTurbo: scripts are run while turbo is held.
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

	Property<bool> startPaused{false}, startFullscreen{false}, pauseWhenMenuActivated{false}, runInBackground{true},
		neverAskSave{false}, luaDuringTurbo{true};

	/* whether a script is to be run for the frame now: not while turbo is
	 * held, unless asked */
	bool luaRunsNow() const { return luaDuringTurbo.get() || !session_.turboHeld(); }

private:
	void say(const char *command);
	SessionViewModel &session_;
	/* a machine opened runs: the first by startPaused, the rest as the last was left */
	bool firstOpen_ = true, runOnOpen_ = true;
	uint64_t opensSeen_ = 0;
	/* paused by a menu or by the window going behind another: run again when that ends */
	bool pausedByMenu_ = false, pausedByFocus_ = false, saying_ = false;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
	Connection runningWatch_, opensWatch_, activeWatch_, commandWatch_;
};

} // namespace chimera::vm
