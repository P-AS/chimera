#pragma once
/* A machine's settings, as its package declares them (and the game, for an
 * arcade game's dip switches): which apply is the engine's to say, from the
 * game files and the other settings; edits wait until applied, and are then
 * given to the running machine, or the machine starts again with them. Chimera
 * knows no setting by name: a row is whatever the package declared. */

#include "chimera/vm/json.hpp"
#include "chimera/vm/settings_form.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class SettingsViewModel : public ViewModel
{
public:
	SettingsViewModel(Dispatcher &dispatcher, SessionViewModel &session);

	/* edits not applied yet */
	Property<bool> pending{false};
	Property<int64_t> shown{0};
	Property<std::string> status{"No machine is open"};

	/* which of a declaration's settings apply (SettingsForm's rule) */
	static std::vector<size_t> applicable(const std::vector<json::Json> &decl, const std::string &slots,
		const json::Json &values, const std::string &machineSetting)
	{
		return SettingsForm::applicable(decl, slots, values, machineSetting);
	}

	/* columns of the "settings" rows */
	enum Column { kName, kLabel, kType, kValue, kOptions, kOptionLabels, kMin, kMax, kDescription, kEdited, kColumns };

private:
	void evaluate();

	SessionViewModel &session_;
	SettingsForm form_;
	SettingsForm::Rows rows_{form_};
	SettingsForm::PresetRows presetRows_{form_};
	Connection opensWatch_;
	json::Json applied_;
};

} // namespace chimera::vm
