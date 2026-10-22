#pragma once
/* The Lua console: a script to edit and run over the view models (the
 * bridge's), and what it printed, a row a line. */

#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class LuaConsoleViewModel : public ViewModel
{
public:
	/* takes over the bridge's log: everything it prints comes here (and to
	 * `echo`, when given, as before) */
	LuaConsoleViewModel(Dispatcher &dispatcher, LuaBridge &lua, std::function<void(const std::string &)> echo = {});

	Property<std::string> script;
	Property<std::string> path;
	Property<int64_t> lines{0};

	void append(const std::string &line);

private:
	class Output : public RowModel
	{
	public:
		explicit Output(LuaConsoleViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.output_.size()); }
		int32_t columnCount() const override { return 1; }
		std::string columnName(int32_t) const override { return "line"; }
		Value cell(int64_t row, int32_t) const override
		{
			return row >= 0 && row < rowCount() ? Value(vm_.output_[static_cast<size_t>(row)]) : Value();
		}

	private:
		LuaConsoleViewModel &vm_;
	};

	LuaBridge &lua_;
	std::function<void(const std::string &)> echo_;
	std::vector<std::string> output_;
	Output rows_{*this};
};

} // namespace chimera::vm
