#pragma once
/* A running process, told apart from a later one that was given the same
 * number: its id and when it started, as Chimera records
 * the session that wrote a recovery folder. A folder whose writer is still
 * running is another window's work in progress, not a crash's. */

#include <cstdint>

namespace chimera::vm {

/* this process's id */
int64_t currentProcessId();

/* when the process with this id started, in the system's own units (clock
 * ticks since boot on Linux, a FILETIME on Windows), or -1 when no such
 * process is running */
int64_t processStarted(int64_t pid);

} // namespace chimera::vm
