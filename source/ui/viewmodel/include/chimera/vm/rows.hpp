#pragma once
/* A list a window shows as rows: it is asked only for the cells on screen, so
 * a list of a million frames costs what forty rows cost. */

#include <cstdint>
#include <string>

#include "chimera/vm/observable.hpp"
#include "chimera/vm/value.hpp"

namespace chimera::vm {

class RowModel
{
public:
	virtual ~RowModel() = default;

	virtual int64_t rowCount() const = 0;
	virtual int32_t columnCount() const = 0;
	virtual std::string columnName(int32_t column) const = 0;
	/* a name for a narrow column; the full name unless the list has a shorter one */
	virtual std::string columnShortName(int32_t column) const { return columnName(column); }
	virtual Value cell(int64_t row, int32_t column) const = 0;
	/* what a row is, beyond its cells, as flags the list defines (the piano
	 * roll: a lag frame, a frame in the greenzone) */
	virtual uint32_t rowFlags(int64_t) const { return 0; }
	/* a column whose cells are numbers to show as numbers (an analog
	 * control's), not marks */
	virtual bool columnIsNumber(int32_t) const { return false; }
	/* a note a row carries (the piano roll: a marker's text), "" for none */
	virtual std::string rowNote(int64_t) const { return {}; }

	/* anything may have changed, the number of rows and columns too */
	Signal<> reset;
	/* rows first..last (inclusive) changed in place */
	Signal<int64_t, int64_t> changed;
	/* rows were added at the end; the new count is rowCount() */
	Signal<int64_t, int64_t> appended;
};

} // namespace chimera::vm
