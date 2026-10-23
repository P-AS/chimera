#include "chimera/vm/customize_vm.hpp"

namespace chimera::vm {

CustomizeViewModel::CustomizeViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("customize", dispatcher), session_(session)
{
	expose("startPaused", "the first machine opened waits paused; after that a machine opened runs when the last was "
		"running", startPaused, true);
	expose("startFullscreen", "the window opens full screen (read at the start)", startFullscreen, true);
	expose("pauseWhenMenuActivated", "a menu open pauses the machine until it closes", pauseWhenMenuActivated, true);
	expose("runInBackground", "the machine runs on while the window is behind another; off, it pauses until the "
		"window is in front again", runInBackground, true);
	expose("neverAskSave", "changes not saved are let go without asking when a project is closed, another opened or "
		"the program quit", neverAskSave, true);
	expose("luaDuringTurbo", "scripts are run while turbo is held", luaDuringTurbo, true);
	expose("showContextMenu", "a right click on the picture opens the window's menu (Enable Context Menu)", showContextMenu, true);
	command("menuOpened", "a menu was opened: the machine pauses when asked to (pauseWhenMenuActivated)", {},
		[this](const Args &, std::string &) -> Value {
			if (pauseWhenMenuActivated.get() && session_.running.get() && !pausedByMenu_)
			{
				pausedByMenu_ = true;
				say("pause");
			}
			return {};
		});
	command("menuClosed", "the menu closed: a machine the menu paused runs again", {}, [this](const Args &, std::string &) -> Value {
		if (pausedByMenu_)
		{
			pausedByMenu_ = false;
			say("play");
		}
		return {};
	});

	/* the person's pause, not the machine's: a stop while a machine is open
	 * counts, unless it is the machine being replaced by the next one (the
	 * open says so in the same turn), or a menu or the window's focus did it */
	runningWatch_ = session_.running.changed.connect([this] {
		if (!session_.isOpen.get()) return;
		if (session_.running.get())
		{
			/* running again, by the person or by what paused it: nothing of
			 * the menu's or the focus's pause is left to undo */
			pausedByMenu_ = pausedByFocus_ = false;
			runOnOpen_ = true;
			return;
		}
		if (pausedByMenu_ || pausedByFocus_) return;
		const uint64_t seen = opensSeen_;
		std::weak_ptr<int> alive = alive_;
		dispatcher_.post([this, seen, alive] {
			if (!alive.expired() && opensSeen_ == seen) runOnOpen_ = false;
		});
	});
	opensWatch_ = session_.opens.changed.connect([this] {
		opensSeen_++;
		pausedByMenu_ = pausedByFocus_ = false;
		const bool run = firstOpen_ ? !startPaused.get() : runOnOpen_;
		firstOpen_ = false;
		runOnOpen_ = run;
		if (run) say("play");
	});
	/* what the person asks of the machine while a menu has it paused is
	 * theirs to keep: the menu does not run it again */
	commandWatch_ = session_.commandRunning.connect([this](const std::string &name) {
		if (saying_ || !pausedByMenu_) return;
		if (name == "play" || name == "pause" || name == "frameAdvance" || name == "seek" || name == "restart") pausedByMenu_ = false;
	});
	activeWatch_ = session_.windowActive.changed.connect([this] {
		if (runInBackground.get()) return;
		if (!session_.windowActive.get())
		{
			if (session_.isOpen.get() && session_.running.get() && !pausedByFocus_)
			{
				pausedByFocus_ = true;
				say("pause");
			}
		}
		else if (pausedByFocus_)
		{
			pausedByFocus_ = false;
			say("play");
		}
	});
}

void CustomizeViewModel::say(const char *command)
{
	std::string error;
	saying_ = true;
	session_.run(command, {}, error);
	saying_ = false;
}

} // namespace chimera::vm
