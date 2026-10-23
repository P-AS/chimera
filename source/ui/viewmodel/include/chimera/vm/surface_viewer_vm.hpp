#pragma once
/* Chimera's Surface Viewer (Tools): the pictures a core draws for itself -
 * a nametable, a tile set, what the core thought worth looking at - picked
 * by name and drawn as the machine moves while the window is open. The
 * window knows nothing of what they are; the core draws them. */

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class SurfaceViewerViewModel : public ViewModel
{
public:
	SurfaceViewerViewModel(Dispatcher &dispatcher, SessionViewModel &session);

	/* the surface shown, by its place in the session's list; how many times
	 * its size it is shown (1 to 8); drawn as the machine moves while watching */
	Property<int64_t> picked{0}, zoom{1};
	Property<bool> watching{false};
	Property<std::string> status;
	Picture surface;
	Signal<> surfaceChanged;

private:
	void draw();
	SessionViewModel &session_;
	Connection frameWatch_, pickedWatch_, zoomWatch_, watchingWatch_, surfacesWatch_;
};

} // namespace chimera::vm
