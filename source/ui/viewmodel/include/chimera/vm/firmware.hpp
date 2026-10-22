#pragma once
/* Finding the firmware a machine needs, as Chimera always has: which of a
 * package's declared files a project calls for is the engine's to say
 * (ce_firmware_evaluate); each is then looked for in the firmware folder - by
 * its hash when one is pinned (the project's pin first, else the package's),
 * which is the only thing that counts then; without a hash by the name the
 * package gives it, and failing that by the one file whose size and name the
 * package's hints point at - never one of several (chimera issue 349). */

#include <cstdint>
#include <string>
#include <vector>

#include "chimera/vm/json.hpp"

namespace chimera::vm {

struct FirmwareFile
{
	std::string path, name;
	int64_t size = 0;
	/* hashed only when a hash is asked of it (a firmware folder can hold
	 * gigabytes, and a file the wrong size is never asked) */
	mutable std::string sha1;
	const std::string &hash() const;
};

/* every file under the folders: names and sizes, nothing read */
/* every file in these folders (and below); a path that is a file is that file */
std::vector<FirmwareFile> indexFirmware(const std::vector<std::string> &folders);

/* the file that answers one declared entry, or nullptr; `how` says by what.
 * A declared size rules out every file of another size before any is read. */
const FirmwareFile *findFirmware(const json::Json &decl, const std::string &pinnedSha1,
	const std::vector<FirmwareFile> &index, std::string &how);

struct FirmwareNeed
{
	std::string id, display;
	/* the file found, empty when none, and its hash */
	std::string path, how, sha1;
};

/* What a machine with this package, these game files and these settings
 * needs, each with the file found for it. firmwareDecl: the package's
 * "firmware" array; settingsDecl its "settings" array (their defaults fill
 * what `settings` leaves out); pins: a project's "firmware" array of
 * {"id", "sha1"}, or empty. */
std::vector<FirmwareNeed> neededFirmware(const json::Json &firmwareDecl, const json::Json &settingsDecl,
	const std::string &slotsJson, const json::Json &settings, const json::Json &pins,
	const std::vector<FirmwareFile> &index);

} // namespace chimera::vm
