#pragma once
/* Chimera's Batch Runner (Tools > Batch Runner): games run one after
 * another for a number of frames, each with the installed core that takes
 * its kind of file, and what came of each - loaded or not, the frames run,
 * the lag frames, the file's SHA-1 - listed and written out as Chimera's
 * dump writes it. The engine runs one machine a process, so each game runs
 * in the headless runner (chimera-run), started by the window. */

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class BatchRunViewModel : public ViewModel
{
public:
	/* starts the headless runner with these arguments; done is called on
	 * the UI's thread with its exit code and what it printed */
	using RunTool = std::function<void(std::vector<std::string> args, std::function<void(int code, std::string out, std::string err)> done)>;
	/* the cores that take a file of this extension: {name, package} */
	using ExtensionFinder = std::function<std::vector<std::pair<std::string, std::string>>(const std::string &extension)>;

	BatchRunViewModel(Dispatcher &dispatcher, RunTool run, ExtensionFinder byExtension);

	Property<int64_t> frames{60}, done{0}, total{0}, fileCount{0};
	Property<bool> running{false};
	Property<std::string> status{"Status:"};
	/* a folder for the runner's files while it works */
	Property<std::string> workFolder;

	struct Result
	{
		std::string file, core;
		/* Chimera's: Success, ErrorOnLoad, ExceptOnAdv */
		std::string status;
		int64_t frames = 0, lagFrames = 0;
		std::string sha1, detail;
	};
	const std::vector<Result> &results() const { return results_; }

private:
	class FileRows : public RowModel
	{
	public:
		explicit FileRows(BatchRunViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.files_.size()); }
		int32_t columnCount() const override { return 1; }
		std::string columnName(int32_t) const override { return "path"; }
		Value cell(int64_t row, int32_t) const override;

	private:
		BatchRunViewModel &vm_;
	};
	class ResultRows : public RowModel
	{
	public:
		explicit ResultRows(BatchRunViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.results_.size()); }
		int32_t columnCount() const override { return 7; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		BatchRunViewModel &vm_;
	};

	void next();

	RunTool run_;
	ExtensionFinder byExtension_;
	std::vector<std::string> files_;
	std::vector<Result> results_;
	FileRows fileRows_{*this};
	ResultRows resultRows_{*this};
	size_t at_ = 0;
	bool cancel_ = false;
};

} // namespace chimera::vm
