#pragma once
/* New Project: a project made before any machine runs, as Chimera's New
 * Project window makes one. The core is chosen among the installed ones, and
 * for a package of several machines the machine; the game's files go in the
 * slots the package declares (file_slots.json: kinds of file, how many,
 * which slots a choice rules out); the settings that apply are offered at
 * their defaults; the firmware they call for is found by the engine's rules
 * or chosen by hand. What keeps the project from being made is said, a line
 * each, until nothing does. Making it writes the project file and opens it. */

#include <map>
#include <string>
#include <vector>

#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/firmware.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/settings_form.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class NewProjectViewModel : public ViewModel
{
public:
	NewProjectViewModel(Dispatcher &dispatcher, SessionViewModel &session, CoresViewModel &cores);

	/* the installed cores, a line each, and the one chosen */
	Property<std::string> coreNames;
	Property<std::string> core;
	/* the chosen package's machines, a line each ("" for a package of one),
	 * and the one chosen, by place */
	Property<std::string> machines;
	Property<int64_t> machine{-1};
	Property<std::string> title;
	/* what keeps the project from being made, a line each; "" when ready */
	Property<std::string> problems{"Choose a core"};
	Property<bool> ready{false};
	Property<std::string> status;

private:
	struct Picked
	{
		std::string name, path;
	};
	struct Slot
	{
		std::string id, title, help, namePattern, nameHelp;
		int64_t min = 0, max = -1;
		std::vector<std::string> formats;
		bool exposed = true;
	};
	struct Need
	{
		std::string id, display, path, how, sha1;
	};

	class SlotRows : public RowModel
	{
	public:
		explicit SlotRows(NewProjectViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.slots_.size()); }
		int32_t columnCount() const override { return 10; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		NewProjectViewModel &vm_;
	};
	class FirmwareRows : public RowModel
	{
	public:
		explicit FirmwareRows(NewProjectViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.needs_.size()); }
		int32_t columnCount() const override { return 5; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		NewProjectViewModel &vm_;
	};

	bool chooseCore(const std::string &name, std::string &error);
	/* the files as the engine reads a slot map: {"slot": ["name", ...]} */
	std::string slotMap() const;
	const Slot *slot(const std::string &id) const;
	/* which slots, settings and firmware apply now, and what is missing */
	void evaluate();

	SessionViewModel &session_;
	CoresViewModel &cores_;
	SlotRows slotRows_{*this};
	SettingsForm form_;
	SettingsForm::Rows settingRows_{form_};
	FirmwareRows firmwareRows_{*this};

	std::string package_, packageSha1_, version_, slotsText_;
	json::Json cfg_;
	std::vector<Slot> slots_;
	std::vector<std::vector<std::string>> atLeastOneOf_;
	std::map<std::string, std::vector<Picked>> files_;
	std::vector<Need> needs_;
	/* firmware chosen by hand: id -> {path, sha1} */
	std::map<std::string, std::pair<std::string, std::string>> chosenFirmware_;
	std::vector<FirmwareFile> firmwareIndex_;
	Connection coresWatch_;
};

} // namespace chimera::vm
