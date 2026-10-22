#pragma once
/* A package's default key bindings (default_keybinds.json): which of the
 * machine's buttons each key name holds. Key names are the ones Chimera has
 * always used ("Z", "Up", "Enter", "Number1", "J1 POV0U"); the window turns
 * its own key codes into them. */

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

} // namespace chimera::vm
