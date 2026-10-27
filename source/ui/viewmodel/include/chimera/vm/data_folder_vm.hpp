#pragma once
/* Chimera's Data Directory (Config > Data Directory): where everything the
 * window keeps for the person lives - each project's cache (Projects) and
 * the unsaved work kept against a crash (Recovery) - and a way to send it
 * somewhere other than the system drive. As Chimera's, a change is only
 * recorded here; the next start carries it out (applyPending), before any
 * of it is open, moving what is there when asked. A move is a rename where
 * it can be, else a copy that is checked and only then a delete; a failure
 * leaves the old place whole and removes what was copied. */

#include <cstdint>
#include <string>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

namespace data_folder {
/* what a folder chosen for the data turns out to be */
enum class Choice { Empty, HoldsData, Same, Nested, Unwritable };
/* where the data goes for a folder picked: the folder, unless it holds
 * other things, then a "Chimera" folder inside it */
std::string targetFor(const std::string &picked);
bool holdsData(const std::string &dir);
Choice inspect(const std::string &current, const std::string &target);
/* everything in `from` into `to`: "" when done, else why not (and then
 * `from` is as it was and nothing of it is left in `to`) */
std::string move(const std::string &from, const std::string &to);
/* every file's bytes under a folder, and the bytes free on the drive a folder is (or would be) on */
int64_t sizeOf(const std::string &dir);
int64_t freeAt(const std::string &dir);
/* bytes as Chimera writes them: "1.5 GB", "12.0 MB", "40 KB" */
std::string bytesText(int64_t bytes);
} // namespace data_folder

class DataFolderViewModel : public ViewModel
{
public:
	explicit DataFolderViewModel(Dispatcher &dispatcher);

	/* the folder in use ("" the system's place, `standard`); the change
	 * asked for, for the next start, and whether what is there moves with
	 * it; all remembered */
	Property<std::string> current, pending;
	Property<bool> pendingMove{false};
	/* the system's place, said by the window */
	Property<std::string> standard;
	/* CHIMERA_DATA_HOME, when it is set: it decides, as Chimera's does (a
	 * portable install, or somebody meant it), and nothing is changed here */
	Property<std::string> environment;
	Property<std::string> status;
	/* how much is in the folder in use and how much its drive has free, as Chimera says it (refresh) */
	Property<std::string> figures;

	/* the folder in use, made absolute */
	std::string inUse() const;
	/* at the start, before anything in it is open: the change asked for,
	 * carried out; "" or what to tell the person */
	std::string applyPending();
};

} // namespace chimera::vm
