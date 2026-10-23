#pragma once
/* Chimera's Firmware window (Config > Firmware): every installed core
 * asked what firmware it declares, and what the person has for each -
 * before any project, before any core is open. Nothing is kept here: the
 * rows are made from the packages in the cores folder when asked
 * (refresh), and what the person points at is remembered where it lives
 * (the cores' chosen files), never copied. A row's state, as Chimera's:
 * 0 on hand and right (its hash, or found by its name when the core
 * declares no hash), 1 on hand but not a dump the core knows (a file chosen
 * whose hash is another - for a game core, the person's own), 2 chosen but
 * gone, 3 nothing. Scan Folder hashes a folder and remembers what answers
 * a row with nothing usable (2 or 3); a file the person chose stays. */

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
	/* rows with nothing on hand */
	Property<int64_t> missing{0};

	struct Row
	{
		std::string core, id, name, path, how;
		std::string declJson;
		int state = 3;
	};
	const std::vector<Row> &items() const { return rows_; }

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(FirmwareViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.rows_.size()); }
		int32_t columnCount() const override { return 7; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		FirmwareViewModel &vm_;
	};
	void survey();
	bool rowOf(const Args &a, size_t &i, std::string &error) const;
	CoresViewModel &cores_;
	std::vector<Row> rows_;
	Rows model_{*this};
};

} // namespace chimera::vm
