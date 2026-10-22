#pragma once
/* RAM Watch: addresses a person keeps an eye on, each read after every
 * frame and shown as its kind of value, with the value before it and how
 * many times it changed. The list is kept in Chimera's .wch files. The
 * reading is the machine's thread's; the list, and what the window shows,
 * the view's. */

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"
#include "chimera/vm/watch_values.hpp"

namespace chimera::vm {

class RamWatchViewModel : public ViewModel
{
public:
	RamWatchViewModel(Dispatcher &dispatcher, SessionViewModel &session);
	~RamWatchViewModel() override;

	struct Watch
	{
		/* a separator is a line with only a note */
		bool separator = false;
		/* a game property watch, kept as its file line until this window can
		 * read game properties: shown, not read, saved back as it was */
		std::string propertyLine;
		std::string domain;
		int64_t address = 0;
		int size = 1;
		watch::Display display = watch::Display::Unsigned;
		bool bigEndian = false;
		std::string note;
		bool onScreen = false;
		/* as last read: the value, the one before it, changes counted, and
		 * whether the address is in its domain at all */
		uint32_t value = 0, previous = 0;
		int64_t changes = 0;
		bool readable = false, seen = false;
		int64_t domainSize = 0;
	};

	Property<int64_t> count{0};
	/* the machine's memory domains, comma-separated */
	Property<std::string> domains;
	/* what "previous" means: 0 the value when the watch was made, 2 the
	 * last frame's, 3 the value before the last change (Chimera's numbers) */
	Property<int64_t> previousType{2};
	/* the .wch file the list was opened from or saved to, and whether it has
	 * changed since */
	Property<std::string> path;
	Property<bool> changed{false};
	Property<std::string> status;
	/* the window's form for a new watch: its size, the ways a value of that
	 * size can be shown (a line each) and the one chosen, by place */
	Property<int64_t> formSize{1};
	Property<std::string> formDisplays;
	Property<int64_t> formDisplay{0};

	/* the watches as the window shows them (the view's thread) */
	const std::vector<Watch> &watches() const { return watches_; }

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(RamWatchViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.watches_.size()); }
		int32_t columnCount() const override { return 10; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		RamWatchViewModel &vm_;
	};

	bool index(const Args &a, size_t at, size_t &out, std::string &error) const;
	/* the list changed: the machine gets the new one, the window is told */
	void listChanged(bool edit);
	bool load(const std::string &file, bool append, std::string &error);
	bool save(const std::string &file, std::string &error);

	/* the machine's domains and system, for the list and its file */
	void refreshDomains();
	/* the machine's thread */
	void readOnMachine(ce_session *s, bool fresh);

	SessionViewModel &session_;
	std::vector<Watch> watches_;
	Rows rows_{*this};
	int hooks_ = 0;
	Connection opensWatch_, previousTypeWatch_, formSizeWatch_;
	std::string systemId_;
	/* each domain's size, as the machine last said (the view's thread) */
	std::map<std::string, int64_t> domainSizes_;
	void sizeFromDomain(Watch &w) const;

	/* the machine's copy, and a serial so values read for an older list are
	 * not shown against a newer one */
	std::vector<Watch> watchesM_;
	int previousTypeM_ = 2;
	uint64_t serial_ = 0, serialM_ = 0;
	std::atomic<bool> postedM_{false};
	std::mutex valuesM_;
	std::vector<Watch> valuesOut_;
	uint64_t valuesSerial_ = 0;
};

} // namespace chimera::vm
