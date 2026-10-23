#pragma once
/* A core's suggestion for a game, before a project exists (ce_suggest_settings,
 * the core's SuggestSettings): the values it would set, and what the game has
 * of its own - settings beyond the package's (an arcade game's dip switches)
 * and, for a package that says "gameFirmware", firmware beyond the package's.
 * The core is asked with what a run would have: the game, the other files
 * picked under their names, the track files of every cue among them, the slot
 * map (mounted as "slots") and the settings chosen so far. */

#include <string>
#include <utility>
#include <vector>

#include "chimera/vm/json.hpp"

namespace chimera::vm {

/* files to mount, as {name, path} */
using Mounts = std::vector<std::pair<std::string, std::string>>;

/* these files and, for every cue among them and the game, the tracks it names
 * that lie beside it and are not listed already */
Mounts withCueTracks(const std::string &game, Mounts files);

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
