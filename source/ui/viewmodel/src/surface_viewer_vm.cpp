#include "chimera/vm/surface_viewer_vm.hpp"

#include <algorithm>

#include "chimera/vm/json.hpp"

namespace chimera::vm {

SurfaceViewerViewModel::SurfaceViewerViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("surfaceViewer", dispatcher), session_(session)
{
	expose("picked", "the surface shown, by its place in the session's list", picked, true);
	expose("zoom", "how many times its size it is shown, 1 to 4, as Chimera's", zoom, true);
	expose("watching", "drawn again as the machine moves (the window is open)", watching, true);
	expose("status", "the surface shown and its size, or why there is none", status);
	picture("surface", "the surface drawn last", surface, surfaceChanged);
	command("refresh", "the surface drawn now", {}, [this](const Args &, std::string &) -> Value {
		draw();
		return {};
	});
	frameWatch_ = session_.frame.changed.connect([this] {
		if (watching.get()) draw();
	});
	pickedWatch_ = picked.changed.connect([this] { draw(); });
	zoomWatch_ = zoom.changed.connect([this] {
		if (zoom.get() < 1 || zoom.get() > 4) zoom.set(std::clamp<int64_t>(zoom.get(), 1, 4));
	});
	watchingWatch_ = watching.changed.connect([this] {
		if (watching.get()) draw();
	});
	surfacesWatch_ = session_.surfaces.changed.connect([this] {
		picked.set(0);
		draw();
	});
}

void SurfaceViewerViewModel::draw()
{
	json::Json list;
	std::string err;
	json::parse(session_.surfaces.get(), list, err);
	const int64_t i = picked.get();
	if (list.items.empty())
	{
		status.set(session_.isOpen.get() ? "This core draws no surfaces." : "No core is running.");
		return;
	}
	if (i < 0 || i >= static_cast<int64_t>(list.items.size()))
	{
		status.set("No surface " + std::to_string(i));
		return;
	}
	const json::Json &s = list.items[static_cast<size_t>(i)];
	if (!session_.renderSurface(static_cast<int32_t>(i), surface))
	{
		status.set(s.find("name")->text + ": the core drew nothing");
		return;
	}
	/* as Chimera's: "Nametables - 512x480" */
	status.set(s.find("name")->text + " - " + s.find("width")->text + "x" + s.find("height")->text);
	surfaceChanged.emit();
}

} // namespace chimera::vm
