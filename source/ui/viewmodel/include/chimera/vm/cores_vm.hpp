#pragma once
/* The cores a person has: the packages in the Cores folder, and where
 * firmware is looked for. Chimera downloads nothing; a core is a file someone
 * put in that folder. A project names its core (and maybe the exact package,
 * by hash): the package run is the one whose hash it pins when that is here,
 * else the most recently installed of that core. */

#include <mutex>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class CoresViewModel : public ViewModel
{
public:
	explicit CoresViewModel(Dispatcher &dispatcher);

	Property<std::string> coresFolder;
	Property<std::string> firmwareFolder;
	Property<int64_t> count{0};
	Property<std::string> status{"No cores folder"};

	/* any thread: the package to run for a core, "" when none is here;
	 * `note` says when it is not the pinned one */
	std::string packageFor(const std::string &coreName, const std::string &pinnedSha1, std::string &note) const;
	/* any thread: the newest package of each core whose files include this
	 * extension (lower case, no dot), as {core name, package} */
	std::vector<std::pair<std::string, std::string>> coresFor(const std::string &extension) const;
	/* any thread: the folders firmware is looked for in */
	std::vector<std::string> firmwareFolders() const;

private:
	struct Core
	{
		std::string coreName, version, made, path;
		long long modified = 0;
		std::vector<std::string> extensions;
	};
	class Rows : public RowModel
	{
	public:
		explicit Rows(CoresViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override;
		int32_t columnCount() const override { return 5; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		CoresViewModel &vm_;
	};

	void scan();

	mutable std::mutex m_;
	std::vector<Core> cores_;
	std::string firmwareM_;
	Rows rows_{*this};
	Connection folderWatch_, firmwareWatch_;
};

} // namespace chimera::vm
