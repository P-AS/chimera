#pragma once
/* Chimera's Help menu: the build and the core open, worded as a bug report
 * asks for them (Copy Version Info, chimera issue 188); a report's first
 * lines filled in from this session, the files worth attaching, and a zip
 * of them all (Report an Issue, chimera issue 243). Nothing is sent: the
 * person files the report in their browser. */

#include <string>
#include <vector>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/settings_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class HelpViewModel : public ViewModel
{
public:
	HelpViewModel(Dispatcher &dispatcher, SessionViewModel &session, SettingsViewModel &settings);

	/* this build: "Qt prototype, commit 1a2b3c4d5e6f" */
	Property<std::string> buildName;
	/* where Chimera's site is, and where an issue is filed */
	Property<std::string> siteUrl{"https://toolassisted.run"};
	Property<std::string> formUrl{"https://github.com/ToolAssisted-run/chimera/issues/new/choose"};
	/* the computer's system, as the window's toolkit names it (the window
	 * says; "" leaves the report's line for the person to fill in) */
	Property<std::string> osName;
	/* the report made last: its text, and the files to attach, a line each */
	Property<std::string> reportText, reportFiles;
	Property<std::string> status;
	/* where the program is: the sandbox's log is looked for there first */
	Property<std::string> exeFolder;

	/* the core open and its version, as the Cores window words it ("" for none) */
	std::string coreAndVersion() const;

private:
	SessionViewModel &session_;
	SettingsViewModel &settings_;
};

} // namespace chimera::vm
