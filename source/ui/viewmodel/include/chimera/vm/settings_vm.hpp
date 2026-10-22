#pragma once
/* A machine's settings, as its package declares them (and the game, for an
 * arcade game's dip switches): which apply is the engine's to say, from the
 * game files and the other settings; edits wait until applied, and are then
 * given to the running machine, or the machine starts again with them. Chimera
 * knows no setting by name: a row is whatever the package declared. */

#include "chimera/vm/json.hpp"
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

	/* columns of the "settings" rows */
	enum Column { kName, kLabel, kType, kValue, kOptions, kOptionLabels, kMin, kMax, kDescription, kEdited, kColumns };

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(SettingsViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.exposed_.size()); }
		int32_t columnCount() const override { return kColumns; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		SettingsViewModel &vm_;
	};

	void evaluate();
	const json::Json *declared(const std::string &name) const;
	Value valueOf(const json::Json &decl, const json::Json &values) const;

	SessionViewModel &session_;
	Rows rows_{*this};
	Connection opensWatch_;
	std::vector<json::Json> decl_;
	std::string declText_, slots_;
	json::Json applied_, values_;
	std::vector<size_t> exposed_;
};

} // namespace chimera::vm
