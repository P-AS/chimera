#pragma once
/* Controls, as Chimera's Controller Config: which keys and gamepad inputs
 * hold each of the machine's buttons (Normal Controls), which fire a button
 * while held (Autofire Controls), and what moves each analog control (Analog
 * Controls: a gamepad axis or the pointer, a sensitivity, a dead zone, and a
 * key for either end). The package says what they are at first. The window
 * works on a copy (edit): Save gives the copy to the machine and remembers
 * what differs from the package, for that core; Cancel lets it go. Key names
 * are Chimera's ("Z", "Up", "J1 POV0U", "X1 A", "X1 LeftThumbX Axis"). */

#include <array>
#include <string>
#include <vector>

#include "chimera/vm/bindings.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class ControlsViewModel : public ViewModel
{
public:
	ControlsViewModel(Dispatcher &dispatcher, SessionViewModel &session);

	/* what differs from the package, every core's: the buttons' keys and the
	 * autofire keys as {"core": {"button": "keys"}}, the analog controls as
	 * {"core": {"control": {"Value", "Mult", "Deadzone", "ButtonBindPositive",
	 * "ButtonBindNegative"}}} (the window's own settings file keeps them) */
	Property<std::string> saved{"{}"};
	Property<std::string> savedAutofire{"{}"};
	Property<std::string> savedAnalog{"{}"};
	/* Chimera's bindings, carried over from its settings (chimera_settings.hpp)
	 * as Chimera keeps them, by controller name: {"AllTrollers",
	 * "AllTrollersAutoFire", "AllTrollersAnalog"}, and "taken", the cores that
	 * took theirs: a core takes its controller's the first time it opens */
	Property<std::string> fromChimera{"{}"};
	/* Chimera's Auto Tab: a key pressed in a box replaces its keys and the
	 * next box takes the next key; off, a key pressed is one more for the box */
	Property<bool> autoTab{true};
	/* the core whose controls are listed, and the window's title */
	Property<std::string> core;
	Property<std::string> title{"Controller Config"};
	/* the tabs inside each kind, in the order their first control comes:
	 * "Player 1", "Player 2"..., "Console" (JSON arrays; one tab is none) */
	Property<std::string> buttonGroups{"[]"};
	Property<std::string> axisGroups{"[]"};
	/* the window's copy is open: changes wait for Save */
	Property<bool> editing{false};
	/* the picture of the controller, counted up each time another is shown (0: none) */
	Property<int64_t> pictureShown{0};
	Property<std::string> status;

	/* the picture's bytes (a PNG), for the view to draw */
	const std::string &picture() const { return session_.controllerPicture(); }

	/* Chimera's tab for a control: "Player N" for "PN ...", else "Console" */
	static std::string groupOf(const std::string &name);

private:
	enum Table
	{
		Normal,
		Autofire,
		Positive,
		Negative,
	};
	class KeyRows : public RowModel
	{
	public:
		KeyRows(ControlsViewModel &vm, Table table) : vm_(vm), table_(table) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.session_.buttonNames().size()); }
		int32_t columnCount() const override { return 6; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		ControlsViewModel &vm_;
		Table table_;
	};
	class AnalogRows : public RowModel
	{
	public:
		explicit AnalogRows(ControlsViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.session_.axes().size()); }
		int32_t columnCount() const override { return 9; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		ControlsViewModel &vm_;
	};

	bool table(const Args &a, Table &out, std::string &error) const;
	bool index(const Args &a, size_t at, Table table, size_t &out, std::string &error) const;
	/* a box's keys, of the copy */
	std::string &keysOf(Table table, size_t i);
	/* the copy, from what the machine has now, unless it is open */
	void begin();
	/* the copy given to the machine, and what differs from the package remembered (save) */
	void keep();
	/* the core open takes Chimera's bindings for its controller, once */
	void takeFromChimera();
	/* the machine's controls: the package's, with this core's changes over them */
	void apply();
	void changed();

	SessionViewModel &session_;
	std::array<std::vector<std::string>, 2> draft_;
	std::vector<AnalogBind> analogDraft_;
	KeyRows normalRows_{*this, Normal}, autofireRows_{*this, Autofire};
	AnalogRows analogRows_{*this};
	Connection opensWatch_, savedWatch_, savedAutofireWatch_, savedAnalogWatch_;
};

} // namespace chimera::vm
