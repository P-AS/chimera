#pragma once
/* The hex editor: a memory domain sixteen bytes a line, shown 1, 2 or 4
 * bytes at a time, with the text the bytes spell. Only the lines on screen
 * are held - a domain can be gigabytes - read on the machine's thread after
 * every frame, with the values that changed since the last read marked.
 * A value typed in is written into the machine's memory (a poke), as
 * Chimera's hex editor writes it; a search for bytes finds the next place
 * they are. */

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class HexEditorViewModel : public ViewModel
{
public:
	HexEditorViewModel(Dispatcher &dispatcher, SessionViewModel &session);
	~HexEditorViewModel() override;

	/* the machine's memory domains, comma-separated, and the one shown */
	Property<std::string> domains;
	Property<std::string> domain;
	/* bytes a value takes: 1, 2 or 4; and which end comes first */
	Property<int64_t> size{1};
	Property<bool> bigEndian{false};
	/* lines of 16 bytes the domain has, the first one shown, and how many
	 * are shown */
	Property<int64_t> lineCount{0};
	Property<int64_t> topLine{0};
	Property<int64_t> visibleLines{32};
	/* the address picked (-1 none) */
	Property<int64_t> picked{-1};
	Property<bool> writable{false};
	Property<std::string> status;

private:
	class Lines : public RowModel
	{
	public:
		explicit Lines(HexEditorViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override;
		/* address, then a value and a changed flag for each of the 16 / size
		 * places (v0, c0, v1, c1, ...), then the text */
		int32_t columnCount() const override { return 2 + 32; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		HexEditorViewModel &vm_;
	};

	/* the window asked for moved, or the domain or size changed: read again */
	void reread();
	/* the machine's thread */
	void readOnMachine(ce_session *s);
	void refreshDomains();

	SessionViewModel &session_;
	Lines lines_{*this};
	int hooks_ = 0;
	Connection opensWatch_, domainWatch_, topWatch_, visibleWatch_, sizeWatch_;

	/* what the window shows (the view's thread): the bytes from `shownFrom_`,
	 * which of them changed at the last read, and the domain's size */
	std::vector<uint8_t> shown_, changedShown_;
	int64_t shownFrom_ = 0, domainSize_ = 0;
	std::map<std::string, int64_t> sizes_;
	std::map<std::string, bool> canWrite_;

	/* the machine's: what to read, and what it read last time */
	std::mutex wantM_;
	std::string wantDomain_;
	int64_t wantFrom_ = 0, wantBytes_ = 0;
	std::vector<uint8_t> lastM_;
	int64_t lastFromM_ = -1;
	std::string lastDomainM_;
	std::atomic<bool> postedM_{false};
	std::mutex outM_;
	std::vector<uint8_t> out_, outChanged_;
	int64_t outFrom_ = 0, outSize_ = 0;
};

} // namespace chimera::vm
