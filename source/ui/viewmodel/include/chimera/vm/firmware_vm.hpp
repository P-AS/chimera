#pragma once
/* Chimera's Firmware window (Config > Firmware): every installed core
 * asked what firmware it declares, and what the person has for each -
 * before any project, before any core is open. Nothing is kept here: the
 * rows are made from the packages in the cores folder when asked
 * (refresh), and what the person points at is remembered where it lives
 * (the cores' chosen files, a pinned dump under its own hash as Chimera's
 * CoreFirmwareStore keys it), never copied. As Chimera's FirmwareSurvey:
 * one group a core (its versions together, the emulators before the game
 * cores), a row a declaration (an id declared for many releases is a row
 * each), each row's state - on hand and right, on hand but a dump the core
 * has never seen, on hand but not the size the core expects, chosen but
 * gone, nothing - where it was found, and Chimera's words for all of it.
 * Scan Folder hashes a folder and remembers every declaration it answers. */

#include <string>
#include <vector>

#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class FirmwareViewModel : public ViewModel
{
public:
	FirmwareViewModel(Dispatcher &dispatcher, CoresViewModel &cores);

	Property<std::string> status;
	/* declarations with nothing on hand */
	Property<int64_t> missing{0};
	/* which cores' files are listed: 0 all, 1 the emulators', 2 the game cores' (remembered) */
	Property<int64_t> shows{0};
	/* every release of an id declared many times, not only the dumps on hand */
	Property<bool> showAll{false};
	/* the line picked in the list (-1: none), and what it says in full */
	Property<int64_t> picked{-1};
	Property<std::string> detail;
	/* Set File... and Clear: a declaration picked, and one whose file was chosen by hand */
	Property<bool> canSet{false}, canClear{false};
	/* what a file picker for the line picked is titled: "core: firmware - release" */
	Property<std::string> pickTitle;

	/* Chimera's states, by its numbers */
	enum State
	{
		Good,
		Unrecognised,
		Custom,
		Unreadable,
		Missing,
	};
	/* where the file answering a row was found */
	enum Where
	{
		Nowhere,
		FirmwareFolder,
		Chosen,
	};
	struct Row
	{
		std::string core, id, display, label, sha1, name, description, condition, path, actualSha1;
		int64_t size = 0;
		bool gameCore = false;
		State state = Missing;
		Where where = Nowhere;
		std::string declJson;
		bool onHand() const { return state == Good || state == Unrecognised || state == Custom; }
		std::string statusText() const;
		std::string whereText() const;
	};
	struct Group
	{
		std::string core;
		bool gameCore = false;
		std::vector<size_t> rows;
	};
	const std::vector<Row> &items() const { return rows_; }
	/* Chimera's line beside a core's name */
	std::string summaryOf(const Group &g) const;
	/* the words of a declaration's "requiredWhen" (Chimera's ConditionText) */
	static std::string conditionText(const json::Json &when);
	/* more releases than this of one id show only those on hand until asked */
	static constexpr size_t kCollapseAbove = 8;

private:
	/* a line of the list: a declaration, a core's "needs no firmware", or "more releases" */
	struct Line
	{
		size_t group = 0;
		long long row = -1;
		std::string text;
	};
	class Lines : public RowModel
	{
	public:
		explicit Lines(FirmwareViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.lines_.size()); }
		int32_t columnCount() const override { return 12; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		FirmwareViewModel &vm_;
	};
	void survey();
	void render();
	void describe();
	bool lineOf(const Args &a, const Row *&out, std::string &error) const;
	/* where a choice for a declaration is remembered: "id#SHA1" when it pins a hash, else "id" */
	static std::string keyOf(const Row &r);
	CoresViewModel &cores_;
	std::vector<Row> rows_;
	std::vector<Group> groups_;
	std::vector<Line> lines_;
	Lines model_{*this};
	Connection showsWatch_, showAllWatch_, pickedWatch_;
};

} // namespace chimera::vm
