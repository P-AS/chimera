#include "chimera/vm/customize_vm.hpp"

namespace chimera::vm {

CustomizeViewModel::CustomizeViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("customize", dispatcher), session_(session)
{
	expose("startPaused", "the first machine opened waits paused; after that a machine opened runs when the last was "
		"running", startPaused, true);
	expose("startFullscreen", "the window opens full screen (read at the start)", startFullscreen, true);
	expose("neverAskSave", "changes not saved are let go without asking when a project is closed, another opened or "
		"the program quit", neverAskSave, true);
	expose("luaDuringTurbo", "scripts are run while turbo is held", luaDuringTurbo, true);
	expose("showContextMenu", "a right click on the picture opens the window's menu (Enable Context Menu)", showContextMenu, true);
	/* the person's pause, not the machine's: a stop while a machine is open
	 * counts, unless it is the machine being replaced by the next one (the
	 * open says so in the same turn) */
	runningWatch_ = session_.running.changed.connect([this] {
		if (!session_.isOpen.get()) return;
		if (session_.running.get())
		{
			runOnOpen_ = true;
			return;
		}
		const uint64_t seen = opensSeen_;
		std::weak_ptr<int> alive = alive_;
		dispatcher_.post([this, seen, alive] {
			if (!alive.expired() && opensSeen_ == seen) runOnOpen_ = false;
		});
	});
	opensWatch_ = session_.opens.changed.connect([this] {
		opensSeen_++;
		const bool run = firstOpen_ ? !startPaused.get() : runOnOpen_;
		firstOpen_ = false;
		runOnOpen_ = run;
		if (run) say("play");
	});
}

void CustomizeViewModel::say(const char *command)
{
	std::string error;
	session_.run(command, {}, error);
}

} // namespace chimera::vm
