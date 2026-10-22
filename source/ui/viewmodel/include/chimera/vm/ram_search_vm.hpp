#pragma once
/* RAM Search: narrow a memory domain down to the addresses that behave as a
 * value in the game does. The search itself is the engine's (candidates,
 * previous values, change counts, undo); this keeps it on the machine's
 * thread, updated after every frame, and lists the candidates as rows. */

#include <mutex>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

struct ce_ramsearch;

namespace chimera::vm {

class RamSearchViewModel : public ViewModel
{
public:
	RamSearchViewModel(Dispatcher &dispatcher, SessionViewModel &session);
	~RamSearchViewModel() override;

	/* the machine's memory domains, comma-separated, and the one searched */
	Property<std::string> domains;
	Property<std::string> domain;
	/* bytes a value takes: 1, 2 or 4 */
	Property<int64_t> size{1};
	Property<bool> bigEndian{false};
	/* how values read: 0 unsigned, 1 signed, 2 a real number (4 bytes) */
	Property<int64_t> display{0};
	/* what a search compares with: 0 the previous value, 1 a value,
	 * 2 an address, 3 the number of changes, 4 a difference */
	Property<int64_t> compare{0};
	/* how: 0 equal, 1 more, 2 at least, 3 less, 4 at most, 5 not equal,
	 * 6 different by */
	Property<int64_t> op{0};
	Property<int64_t> value{0};
	Property<int64_t> differentBy{0};
	/* candidates left, and whether a search can be taken back or redone */
	Property<int64_t> count{0};
	Property<bool> canUndo{false};
	Property<bool> canRedo{false};
	Property<bool> searching{false};
	Property<std::string> status{"No search"};

private:
	class Results : public RowModel
	{
	public:
		explicit Results(RamSearchViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return vm_.rows_; }
		int32_t columnCount() const override { return 4; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		RamSearchViewModel &vm_;
	};

	/* on the machine's thread */
	void startOnMachine(ce_session *s, const std::string &domain, int32_t size, bool bigEndian);
	void searchOnMachine(int32_t compare, int32_t op, int32_t display, uint32_t value, uint32_t differentBy);
	void afterFrameOnMachine();
	void dropOnMachine();
	void reportOnMachine(const std::string &text);
	Value shown(uint32_t raw) const;

	SessionViewModel &session_;
	Results results_{*this};
	int hooks_ = 0;
	Connection opensWatch_;

	/* the search, the machine's thread's; the window reads rows under rsM_ */
	mutable std::mutex rsM_;
	ce_ramsearch *rs_ = nullptr;
	int64_t rows_ = 0;
	std::atomic<bool> refreshPosted_{false};
};

} // namespace chimera::vm
