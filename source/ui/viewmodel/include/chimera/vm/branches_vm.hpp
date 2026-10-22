#pragma once
/* Branches: other versions of the movie, kept to come back to. Making one
 * keeps the inputs and markers as they are, and the frame the machine is
 * on; loading one puts them back and the machine on that frame. The states
 * before the first frame where the two movies differ still hold, so only
 * what follows it is run again. */

#include "chimera/vm/piano_roll_vm.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class BranchesViewModel : public ViewModel
{
public:
	BranchesViewModel(Dispatcher &dispatcher, InputLog &inputs, SessionViewModel &session, PianoRollViewModel &roll);

	Property<int64_t> count{0};
	Property<std::string> status;

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(BranchesViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.branches_.size()); }
		int32_t columnCount() const override { return 4; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		BranchesViewModel &vm_;
	};

	bool index(const Args &a, size_t &out, std::string &error) const;
	void changed();

	InputLog &inputs_;
	SessionViewModel &session_;
	PianoRollViewModel &roll_;
	std::vector<SessionViewModel::Branch> branches_;
	Rows rows_{*this};
	Connection loadedWatch_;
};

} // namespace chimera::vm
