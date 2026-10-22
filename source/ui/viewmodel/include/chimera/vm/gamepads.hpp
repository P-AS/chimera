#pragma once
/* Gamepads and joysticks through SDL2, found beside the program at run time
 * (as the C# interface does). Each input held is named as Chimera has always
 * named it, so a package's default bindings mean the same: "X1 A",
 * "X1 DpadUp", "X1 LStickUp" for a game controller and "J1 B1", "J1 POV0U"
 * for any other joystick, numbered by SDL's device order from 1. Without
 * SDL2 there are no gamepads, and nothing else changes.
 *
 * One thread only: the one that made it polls it. */

#include <memory>
#include <string>
#include <vector>

namespace chimera::vm {

class Gamepads
{
public:
	Gamepads();
	~Gamepads();
	Gamepads(const Gamepads &) = delete;
	Gamepads &operator=(const Gamepads &) = delete;

	/* SDL2 was found and started */
	bool available() const;
	/* why not, when it is not */
	const std::string &problem() const { return problem_; }

	/* the names of every input held now; picks up pads plugged in or out */
	void poll(std::vector<std::string> &held);

	/* the pads open now, by name */
	std::vector<std::string> names() const;

private:
	struct Api;
	struct Pad;
	void reopen();

	std::unique_ptr<Api> api_;
	std::vector<Pad> pads_;
	std::vector<int32_t> ids_;
	std::string problem_;
};

} // namespace chimera::vm
