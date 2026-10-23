#pragma once
/* A package's settings as a form: its declarations, the values chosen,
 * which settings apply (the engine's to say, from the game files and the
 * other values, and the machine a package of several has chosen), typed
 * edits, and the rows a window lists. The Settings window edits a running
 * machine's with it, the New Project window a project's before there is a
 * machine. Chimera knows no setting by name: a row is whatever the package
 * declared. */

#include <string>
#include <vector>

#include "chimera/vm/json.hpp"
#include "chimera/vm/rows.hpp"
#include "chimera/vm/value.hpp"

namespace chimera::vm {

class SettingsForm
{
public:
	/* the package's "settings" (and a game's own, after them) and the
	 * setting that chooses the machine, when the package has several */
	void declare(std::vector<json::Json> decl, std::string machineSetting);
	/* the package's machines: a machine may narrow a setting's options and
	 * default ("settingOverrides"), as Chimera has them for the machine the
	 * machine setting chooses (a Master System's port takes a pad or
	 * nothing); a value already chosen is kept as it is */
	void declareMachines(std::vector<json::Json> machines);
	/* the game files, as {"slot": ["name", ...]} */
	void setSlots(std::string slotsJson);
	void setValues(json::Json values);
	const json::Json &values() const { return values_; }
	/* the values the rows' "edited" column compares with (none: nothing is
	 * edited) */
	void setApplied(const json::Json *applied) { applied_ = applied; }
	/* the machine's chooser left out of the rows, for a window that offers
	 * the machine on its own (it is still recorded); and any other setting
	 * such a window offers on its own (the renderer) */
	void listMachineSetting(bool listed) { listMachine_ = listed; }
	void unlist(std::string name) { unlisted_.push_back(std::move(name)); }
	/* a declaration as the chosen machine has it, whether it applies or not;
	 * and whether it applies (listed or not) */
	const json::Json *declaration(const std::string &name) const;
	bool applies(const std::string &name) const;

	/* a setting that applies, set to a value of its type and range */
	bool set(const std::string &name, const Value &value, std::string &error);
	/* the setting by name, when it applies */
	const json::Json *declared(const std::string &name) const;
	/* The package's presets: named bundles of values it suggests ("1981 IBM
	 * PC/XT 5150"), each for the machines its "when" names. Applying one
	 * writes its values as if typed in, and is then finished with: the
	 * machine's own setting is not a preset's to move, and a name this
	 * machine does not have is left out and named in `skipped`. */
	void declarePresets(std::vector<json::Json> presets) { presets_ = std::move(presets); }
	bool applyPreset(const std::string &id, std::string &label, std::vector<std::string> &skipped, std::string &error);

	/* which apply, again (after declare, setSlots, setValues or set) */
	void evaluate();
	const std::vector<size_t> &exposed() const { return exposed_; }
	const std::vector<json::Json> &declarations() const { return decl_; }
	const std::string &machineSetting() const { return machineSetting_; }

	/* every setting that applies at its value, the machine's chooser too:
	 * what a project records (its settings are exactly the knobs offered) */
	json::Json recorded() const;

	/* the value a declaration has among these values, else its default */
	static Value valueOf(const json::Json &decl, const json::Json &values);
	/* Which of a declaration's settings apply, in its order: those the
	 * engine exposes for these game files and values, and of those, the
	 * ones whose "when" names the machine the package's machineSetting
	 * (if it has one) chose. */
	static std::vector<size_t> applicable(const std::vector<json::Json> &decl, const std::string &slots,
		const json::Json &values, const std::string &machineSetting);

	/* columns of the rows */
	enum Column { kName, kLabel, kType, kValue, kOptions, kOptionLabels, kMin, kMax, kDescription, kEdited, kColumns };

	/* the presets this machine is offered: id, label, description */
	class PresetRows : public RowModel
	{
	public:
		explicit PresetRows(const SettingsForm &form) : form_(form) {}
		int64_t rowCount() const override { return static_cast<int64_t>(form_.offered().size()); }
		int32_t columnCount() const override { return 3; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		const SettingsForm &form_;
	};
	std::vector<const json::Json *> offered() const;

	class Rows : public RowModel
	{
	public:
		explicit Rows(const SettingsForm &form) : form_(form) {}
		int64_t rowCount() const override { return static_cast<int64_t>(form_.exposed_.size()); }
		int32_t columnCount() const override { return kColumns; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		const SettingsForm &form_;
	};

private:
	/* as declared, and as the chosen machine has them */
	std::vector<json::Json> base_, decl_, machines_;
	void narrow();
	std::string machineSetting_, slots_{"{}"};
	json::Json values_;
	const json::Json *applied_ = nullptr;
	bool listMachine_ = true;
	std::vector<std::string> unlisted_;
	std::vector<size_t> exposed_;
	std::vector<json::Json> presets_;
	/* the machine the machine setting says (its value), "" without one */
	std::string machineValue() const;
};

} // namespace chimera::vm
