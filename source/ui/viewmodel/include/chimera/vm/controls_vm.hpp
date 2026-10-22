#pragma once
/* Controls: which keys and gamepad controls hold each of the machine's
 * buttons. The package says what they are at first; a person changes them
 * here, a button at a time, and the changes are remembered for that core
 * (the window's own settings file keeps `saved`). Key names are Chimera's
 * ("Z", "Up", "J1 POV0U", "X1 A"). */

#include <string>
#include <vector>

#include "chimera/vm/json.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class ControlsViewModel : public ViewModel
{
public:
	ControlsViewModel(Dispatcher &dispatcher, SessionViewModel &session);

	/* the changes made, every core's: {"core": {"button": "keys"}} */
	Property<std::string> saved{"{}"};
	/* the core whose buttons are listed */
	Property<std::string> core;
	Property<std::string> status;

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(ControlsViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.session_.buttonNames().size()); }
		int32_t columnCount() const override { return 4; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		ControlsViewModel &vm_;
	};

	bool button(const Args &a, size_t &out, std::string &error) const;
	/* the session's keys: the package's, with this core's changes over them */
	void apply();
	/* a button's keys changed: remembered, and given to the session */
	void change(size_t button, const std::string &keys);

	SessionViewModel &session_;
	Rows rows_{*this};
	Connection opensWatch_, savedWatch_;
};

} // namespace chimera::vm
