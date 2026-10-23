#pragma once
/* What the person's own devices hold, for Lua's input library as Chimera
 * has it: the keys down (by Chimera's names, whatever they are bound to),
 * the gamepads' buttons, and the pointer over the picture - where, in the
 * machine's pixels, its buttons and its wheel. The window tells it. */

#include <set>
#include <string>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class HostInputViewModel : public ViewModel
{
public:
	HostInputViewModel(Dispatcher &dispatcher, SessionViewModel &session);

	/* a key down or up, by its name */
	void key(const std::string &name, bool down);
	/* every key let go (the window lost the keyboard) */
	void releaseKeys() { keys_.clear(); }

	/* the pointer over the picture, in the machine's pixels */
	Property<int64_t> mouseX{0}, mouseY{0};
	/* its buttons held: 1 left, 2 middle, 4 right, 8 and 16 the side ones */
	Property<int64_t> mouseButtons{0};
	/* the wheel turned, in notches of 120, so far */
	Property<int64_t> mouseWheel{0};

	static constexpr int64_t kLeft = 1, kMiddle = 2, kRight = 4, kX1 = 8, kX2 = 16;

private:
	SessionViewModel &session_;
	std::set<std::string> keys_;
};

} // namespace chimera::vm
