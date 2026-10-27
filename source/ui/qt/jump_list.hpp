#pragma once
/* Chimera's jump list, on Windows: the taskbar's Recent
 * list shown for the program, and each project or game opened put on it as
 * a task that starts the program with that file. Elsewhere, nothing. */

#include <string>

namespace chimera::qt {

class JumpList
{
public:
	/* the program's jump list shows its Recent category (COM must be started) */
	static void showRecent();
	/* a file opened, on the Recent list: the program started with it; `title` the entry's words */
	static void addRecent(const std::string &path, const std::string &title);
};

} // namespace chimera::qt
