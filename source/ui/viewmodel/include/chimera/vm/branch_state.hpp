#pragma once
/* A branch's state, kept in a file in the project's cache folder so that
 * loading the branch is a state load and not a replay. Beside it a note says
 * which machine it is a state of (a state is the memory of one exact
 * machine: core, settings and files), which frame it is, and the picture the
 * branch had, which a state load does not draw. Everything here is
 * regenerable: a missing or unfitting file costs a replay, never work. */

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "chimera/vm/picture.hpp"

namespace chimera::vm::branch_state {

/* a path as the engine takes it, UTF-8 */
std::string utf8(const std::filesystem::path &path);

/* 16 hex digits of a 64-bit FNV-1a: names files, compares notes */
std::string fingerprint(std::string_view text);

/* The file name a branch's state has, from what makes the branch itself -
 * when it was made, its frame and its movie - so a project needs to keep
 * nothing more to find it again. */
std::string nameFor(const std::string &time, int64_t frame, const std::string &text);

struct Note
{
	std::string machine;
	int64_t frame = -1;
	Picture::Frame picture;
};

/* written beside its name and moved into place: false when it could not be */
bool writeNote(const std::filesystem::path &path, const Note &note);
/* false when there is none, or it is not one */
bool readNote(const std::filesystem::path &path, Note &out);

} // namespace chimera::vm::branch_state
