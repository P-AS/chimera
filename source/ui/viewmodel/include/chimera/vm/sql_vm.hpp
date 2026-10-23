#pragma once
/* Lua's SQL library, as Chimera has it: a SQLite database made or opened by
 * its file, a command that writes, and one that reads every row back. Each
 * answers in Chimera's own words ("Command ran successfully", "No rows
 * found", or SQLite's message). */

#include <string>

#include "chimera/vm/view_model.hpp"

struct sqlite3;

namespace chimera::vm {

class SqlViewModel : public ViewModel
{
public:
	explicit SqlViewModel(Dispatcher &dispatcher);
	~SqlViewModel() override;

private:
	sqlite3 *db_ = nullptr;
};

} // namespace chimera::vm
