#include "chimera/vm/host_input_vm.hpp"

namespace chimera::vm {

HostInputViewModel::HostInputViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("input", dispatcher), session_(session)
{
	expose("mouseX", "the pointer across the picture, in the machine's pixels", mouseX, true);
	expose("mouseY", "the pointer down the picture, in the machine's pixels", mouseY, true);
	expose("mouseButtons", "the pointer's buttons held: 1 left, 2 middle, 4 right, 8 and 16 the side ones", mouseButtons, true);
	expose("mouseWheel", "the wheel turned so far, in notches of 120", mouseWheel, true);
	command("held", "what is held, a name a line: keys, gamepad buttons, and the pointer's buttons as WMouse L, R, M, 1 and 2",
		{}, [this](const Args &, std::string &) -> Value {
			std::string out;
			auto add = [&](const std::string &n) { out += (out.empty() ? "" : "\n") + n; };
			for (const std::string &k : keys_) add(k);
			for (const std::string &p : session_.padsHeld()) add(p);
			const int64_t b = mouseButtons.get();
			if (b & kLeft) add("WMouse L");
			if (b & kRight) add("WMouse R");
			if (b & kMiddle) add("WMouse M");
			if (b & kX1) add("WMouse 1");
			if (b & kX2) add("WMouse 2");
			return out;
		});
}

void HostInputViewModel::key(const std::string &name, bool down)
{
	if (down) keys_.insert(name);
	else keys_.erase(name);
}

} // namespace chimera::vm
