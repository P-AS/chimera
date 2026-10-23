#pragma once
/* Chimera's Macro Tool (Tools > Macro Input): zones of the movie kept as
 * macros. The first is the entire movie; a zone is set from a start and an
 * end frame, or loaded from a .chimeraMacro file. Each has a name, Overlay
 * (pressed over what is there) and Replace (over the frames there, else put
 * in before them), and the columns it carries; placed at a frame, or saved
 * as a .chimeraMacro file Chimera reads. Zones not saved are said before
 * the window lets them go. */

#include <string>
#include <vector>

#include "chimera/vm/piano_roll_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class MacroToolViewModel : public ViewModel
{
public:
	MacroToolViewModel(Dispatcher &dispatcher, PianoRollViewModel &roll, SessionViewModel &session);

	/* the zone picked, by its place */
	Property<int64_t> picked{0};
	/* the movie's columns, as [name, ...], for a zone's to be picked from */
	Property<std::string> movieColumns{"[]"};
	Property<int64_t> unsavedCount{0};
	Property<std::string> status;

	struct Zone
	{
		std::string name;
		int64_t start = 0, length = 0;
		bool overlay = false, replace = true, saved = true, whole = false;
		/* the columns it carries (none: all of the movie's) */
		std::vector<std::string> columns;
		/* a zone loaded from a file: its lines, as the file has them */
		std::vector<std::string> fileLines;
	};
	const std::vector<Zone> &zones() const { return zones_; }

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(MacroToolViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.zones_.size()); }
		int32_t columnCount() const override { return 7; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		MacroToolViewModel &vm_;
	};
	bool zoneOf(const Args &a, size_t &i, std::string &error) const;
	/* the zone's macro text (with its options), or "" with the reason */
	std::string textOf(const Zone &z, std::string &error) const;
	void changed();
	void readColumns();
	PianoRollViewModel &roll_;
	SessionViewModel &session_;
	std::vector<Zone> zones_;
	Rows rows_{*this};
	Connection lengthWatch_, opensWatch_;
};

} // namespace chimera::vm
