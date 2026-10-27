#pragma once
/* A package's default key bindings (default_keybinds.json): which of the
 * machine's buttons each key name holds. Key names are the ones Chimera has
 * always used ("Z", "Up", "Enter", "Number1", "J1 POV0U"); the window turns
 * its own key codes into them. */

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace chimera::vm {

class Bindings
{
public:
	/* Reads the package's file; buttonNames are the machine's buttons in
	 * order. A button the file does not name is left unbound. False with
	 * the reason in `error` when the text is not readable JSON. */
	bool parse(std::string_view json, const std::vector<std::string> &buttonNames, std::string &error);
	/* the same from a group the file has: "AllTrollers" (the buttons) or
	 * "AllTrollersAutoFire" (the keys that fire a button while held); the
	 * section of the machine's controller when the file has one by that
	 * name, as Chimera's, else the one that names most of its buttons */
	bool parse(std::string_view json, const std::vector<std::string> &buttonNames, const std::string &group, std::string &error,
		const std::string &controller = {});

	/* the buttons a key holds, by index (empty when it holds none) */
	const std::vector<int> &buttonsOf(const std::string &key) const;

	size_t boundKeys() const { return byKey_.size(); }

	/* each button's keys as the file writes them ("Down, J1 POV0D"), by
	 * index; "" for a button with none */
	const std::vector<std::string> &texts() const { return texts_; }
	/* the keys of every button given again, by index */
	void setTexts(std::vector<std::string> texts);

private:
	std::map<std::string, std::vector<int>> byKey_;
	std::vector<std::string> texts_;
};

/* keys as a binding writes them ("Shift, LeftShift", "Ctrl+X") with key
 * names replaced, as Chimera's rewrite of the keys bound: each key of the
 * list and each part of a combination; a key there twice is kept once */
std::string replaceKeyNames(const std::string &keys, const std::map<std::string, std::string> &names);
/* the names to replace from a JSON object ({"LeftShift": "Shift"}); false
 * with the reason when it is not one */
bool keyNamesFrom(std::string_view json, std::map<std::string, std::string> &out, std::string &error);

/* An analog control's binding, as Chimera does: a host axis
 * ("X1 LeftThumbX Axis", "J1 X Axis") read from -10000 to 10000, a dead
 * zone (a fraction of it), a multiplier (-2 to 2), and two keys that push
 * it to either end instead */
struct AnalogBind
{
	std::string value;
	double mult = 1.0, deadzone = 0.1;
	std::string positive, negative;
	bool empty() const { return value.empty() && positive.empty() && negative.empty(); }
};

/* A package's analog bindings ("AllTrollersAnalog" in default_keybinds.json),
 * by the machine's analog controls in order; unbound ones empty */
std::vector<AnalogBind> parseAnalogBinds(std::string_view json, const std::vector<std::string> &axisNames, std::string &error,
	const std::string &controller = {});
/* an analog binding as JSON, as Chimera writes one, and back */
std::string analogBindJson(const AnalogBind &bind);
bool analogBindFrom(std::string_view json, AnalogBind &out);

/* Chimera's Controller.LatchFromPhysical for one analog control: a key
 * pushing it to an end, else the host axis (-10000..10000), past the dead
 * zone and scaled back up, times the multiplier, spread over the larger side
 * of the control's range around its rest value, and kept in the range.
 * `active` says whether anything moved it off its rest value: a key held, or
 * the axis past the dead zone */
int32_t analogValue(const AnalogBind &bind, int hostValue, bool positiveHeld, bool negativeHeld, int64_t min, int64_t max,
	int64_t neutral, bool &active);

} // namespace chimera::vm
