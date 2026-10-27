#pragma once
/* A core's suggestion for a game, before a project is final
 * (ce_project_suggest_settings, the core's SuggestSettings): the values it
 * would set, and what the game has of its own - settings beyond the
 * package's (an arcade game's dip switches) and, for a package that says
 * "gameFirmware", firmware beyond the package's. A game runs only in a
 * project (owner, 2026-10-26), so the core is asked of one: the files picked
 * so far in their slots (a cue's tracks come with it) and the settings
 * chosen so far. A compile session and a movie import are asked the same
 * way. */

#include <string>
#include <utility>
#include <vector>

#include "chimera/engine.h"
#include "chimera/vm/json.hpp"

namespace chimera::vm {

/* files to mount, as {name, path} */
using Mounts = std::vector<std::pair<std::string, std::string>>;

/* a file of the project a core is asked about: its slot ("" for a file with
 * no slot, mounted beside the project by name), its name and where it is */
struct AskFile
{
	std::string slot, name, path;
};

/* the files of a slot map ({slot: [names]}) at the paths given ({name, path});
 * a game named on its own and not among them goes in the package's slot that
 * takes its kind of file (else the first) */
std::vector<AskFile> askFilesOf(const std::string &package, const std::string &game, const Mounts &files, const std::string &slotsJson);

/* the project a core is asked about: the package's core, the files that have
 * a slot, and the settings; nullptr and why when it cannot be made. The
 * caller frees it (ce_project_free). */
ce_project *askingProject(const std::string &package, const std::vector<AskFile> &files, const std::string &settingsJson, std::string &why);

/* the same, saved where a helper process can open it, the folders its files
 * are in written to dirs (chimera-run --files); false and why when not */
bool saveAskingProject(const std::string &package, const std::vector<AskFile> &files, const std::string &settingsJson,
	const std::string &path, std::vector<std::string> &dirs, std::string &why);


/* asks the core in this process, which must have no machine open; the
 * answer, or "" and why */
std::string askForSuggestion(const std::string &package, const std::string &game, const Mounts &files,
	const std::string &slotsJson, const std::string &settingsJson, std::string &why);

/* the game's own setting declarations in an answer: one with no name, or with
 * a name the package's settings or an earlier entry have, is dropped */
std::vector<json::Json> gameSettingsOf(const json::Json &answer, const json::Json *packageSettings);

/* the game's own firmware in an answer, as an array in the package's
 * firmware format: one with no id, or with an id the package or an earlier
 * entry has, is dropped - a game adds firmware, it cannot redefine the
 * package's */
json::Json gameFirmwareOf(const json::Json &answer, const json::Json *packageFirmware);

} // namespace chimera::vm
