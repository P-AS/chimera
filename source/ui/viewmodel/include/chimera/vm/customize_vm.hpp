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
 * - alternateLayouts: a key is named by what the keyboard's layout makes it
 *   rather than by its place, on Windows (Chimera's Handle alternate
 *   keyboard layouts); keys are named by place on the others, as Chimera's.
 * - singleInstance: a second start of the program hands the files it is
 *   given to the window already running and ends, on Windows (Chimera's
 *   Single instance mode; read when the program starts).
 * - gameCubeAdapter: Wii U and Switch GameCube adapters are read, through
 *   SDL's own USB reading (Chimera's Enable Wii U/Switch GameCube Adapter
 *   Support; read when the program starts).
 * - mergeModifiers: both Shift keys are "Shift", rather than "LeftShift"
 *   and "RightShift", likewise Ctrl and Alt (Chimera's Merge L+R modifier
 *   keys). Changing it can rewrite the keys bound (modifierKeys).
 * The frame advance skipping lag frames and background input are the
 * session's (skipLagFrames, acceptBackgroundInput). */

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class CustomizeViewModel : public ViewModel
{
public:
	CustomizeViewModel(Dispatcher &dispatcher, SessionViewModel &session);

	Property<bool> startPaused{false}, startFullscreen{false}, neverAskSave{false}, luaDuringTurbo{true}, showContextMenu{true};
	Property<bool> alternateLayouts{false}, mergeModifiers{true}, singleInstance{false}, gameCubeAdapter{false};

	/* Chimera's rewrite of the keys bound when mergeModifiers changes
	 * (ConfigExtensions.ReplaceKeysInBindings with Input.ModifierKeyPreMap or
	 * ModifierKeyInvPreMap): merging, each side's name becomes the kind's
	 * ("LeftShift" and "RightShift" "Shift"); separating, the kind's becomes
	 * the left's. Super's are left out: no key is named so. */
	static std::map<std::string, std::string> modifierKeys(bool merging);

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
