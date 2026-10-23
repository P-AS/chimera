#pragma once
/* The cores a person has: the packages in the Cores folder, and where
 * firmware is looked for. Chimera downloads nothing; a core is a file someone
 * put in that folder. A project names its core (and maybe the exact package,
 * by hash): the package run is the one whose hash it pins when that is here,
 * else the most recently installed of that core. */

#include <mutex>

#include "chimera/vm/json.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class CoresViewModel : public ViewModel
{
public:
	explicit CoresViewModel(Dispatcher &dispatcher);

	Property<std::string> coresFolder;
	Property<std::string> firmwareFolder;
	/* the firmware files chosen by hand, every core's, as {"core": {"id": "file"}}:
	 * a file chosen outside the firmware folder is found there again */
	Property<std::string> chosenFirmware{"{}"};
	Property<int64_t> count{0};
	Property<std::string> status{"No cores folder"};

	/* any thread: the package to run for a core, "" when none is here;
	 * `note` says when it is not the pinned one */
	std::string packageFor(const std::string &coreName, const std::string &pinnedSha1, std::string &note) const;
	/* any thread: the newest package of each core whose files include this
	 * extension (lower case, no dot), as {core name, package} */
	std::vector<std::pair<std::string, std::string>> coresFor(const std::string &extension) const;
	/* any thread: the newest package of each installed core, by name, as
	 * {core name, package} */
	std::vector<std::pair<std::string, std::string>> installed() const;
	/* Any thread: every package, as Chimera's New Project lists them - by
	 * core, newest first - each with its line: the core, its version dated
	 * in Chimera's clock, the package's hash when two packages are of one
	 * core, and its systems ("Dolphin  2026-10-21 18:11 CEST  (285b6a8f
	 * local)  -  GameCube, Wii, Triforce"). */
	struct Package
	{
		std::string name, line, path;
		bool gameCore = false;
	};
	std::vector<Package> packages() const;
	/* a package's systems, by its own word for each: its machines', else
	 * its one ("GameCube, Wii, Triforce") */
	static std::string systemsOf(const json::Json &cfg);
	/* any thread: where firmware is looked for - the folders, then the files
	 * chosen by hand */
	std::vector<std::string> firmwareFolders() const;
	/* the file chosen for a core's firmware, remembered; and read back ("" for none) */
	void rememberFirmware(const std::string &coreName, const std::string &id, const std::string &path);
	std::string chosenFirmwareFor(const std::string &coreName, const std::string &id) const;

private:
	struct Core
	{
		std::string coreName, version, made, path, systems, line;
		long long modified = 0;
		std::vector<std::string> extensions;
		bool gameCore = false, dated = false;
		int64_t date = 0;
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
	std::vector<std::string> chosenM_;
	Rows rows_{*this};
	Connection folderWatch_, firmwareWatch_, chosenWatch_;
};

} // namespace chimera::vm
