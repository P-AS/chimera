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
	/* a column not shown (the piano roll's hidden ones): drawn nowhere */
	virtual bool columnHidden(int32_t) const { return false; }
	/* the same in one of several views of these rows (TAStudio's input
	 * rolls, each with columns of its own) */
	virtual bool columnHiddenIn(int64_t view, int32_t column) const
	{
		(void)view;
		return columnHidden(column);
	}
	/* a column marked out (the piano roll's Auto-hold ones) */
	virtual bool columnEmphasized(int32_t) const { return false; }
	/* the row shown after this one (the piano roll hides some lag frames);
	 * from -1, the first */
	virtual int64_t nextShown(int64_t row) const { return row + 1; }
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
