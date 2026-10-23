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
	/* branches the machine has made (their states kept or not), branches
	 * loaded, and whether the last one loaded came from its state */
	Property<int64_t> made{0};
	Property<int64_t> loads{0};
	Property<bool> loadedFromState{false};
	/* the branch picked in the list (-1: none), and what Undo would take
	 * back, as Chimera names it ("Undo Branch Load"...; "" for nothing) */
	Property<int64_t> selected{-1};
	Property<std::string> undoText;
	/* TAStudio's branch settings: a double click on a branch loads it; and
	 * the old way, where loading a branch only puts the machine on its
	 * frame (Chimera's "old control scheme for branches", as it is when
	 * not recording) */
	Property<bool> loadOnDoubleClick{true};
	Property<bool> oldControlScheme{false};

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(BranchesViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.branches_.size()); }
		int32_t columnCount() const override { return 5; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		BranchesViewModel &vm_;
	};

	bool index(const Args &a, size_t &out, std::string &error) const;
	/* the run as it stands, as a new branch (slot -1, or past the last) or
	 * over the one at `slot`; where it went */
	int64_t keepRun(int64_t slot, std::string name);
	void changed();
	/* Chimera's one step of branch undo: what it puts back, and of what kind;
	 * a branch kept for it that is in no list loses its state file when it
	 * is no longer needed */
	enum class Undo { None, Load, Update, Text, Remove };
	Undo undo_ = Undo::None;
	SessionViewModel::Branch backup_;
	int64_t backupAt_ = -1;
	void keepBackup(Undo kind, SessionViewModel::Branch b, int64_t at);
	void keepLoadBackup();
	void dropBackup();
	bool listed(uint64_t id) const;

	InputLog &inputs_;
	SessionViewModel &session_;
	PianoRollViewModel &roll_;
	std::vector<SessionViewModel::Branch> branches_;
	Rows rows_{*this};
	Connection loadedWatch_;
	uint64_t lastId_ = 0;
};

} // namespace chimera::vm
