#include "chimera/vm/display_vm.hpp"

#include <algorithm>
#include <cmath>

namespace chimera::vm {

DisplayFit::Rect DisplayFit::source(int width, int height) const
{
	Rect r;
	const int l = std::max(0, cropLeft), t = std::max(0, cropTop), rr = std::max(0, cropRight), b = std::max(0, cropBottom);
	/* a crop that would leave nothing leaves the picture whole */
	if (l + rr >= width || t + b >= height) return {0, 0, static_cast<double>(width), static_cast<double>(height)};
	r.x = l;
	r.y = t;
	r.w = width - l - rr;
	r.h = height - t - b;
	return r;
}

void DisplayFit::naturalSize(int width, int height, double machineAspect, double &w, double &h) const
{
	const Rect s = source(width, height);
	if (aspectMode == 2 && customWidth > 0 && customHeight > 0)
	{
		/* the person's size for the whole picture; a crop takes its share off */
		w = customWidth * s.w / width;
		h = customHeight * s.h / height;
		return;
	}
	double shape = static_cast<double>(width) / height;
	if (aspectMode == 1 && machineAspect > 0) shape = machineAspect;
	if (aspectMode == 3 && ratioX > 0 && ratioY > 0) shape = ratioX / ratioY;
	/* the whole picture's shape, then the crop's share of each side */
	h = s.h;
	w = height * shape * s.w / width;
}

DisplayFit::Rect DisplayFit::place(int width, int height, double machineAspect, double areaW, double areaH) const
{
	Rect r;
	if (width <= 0 || height <= 0 || areaW <= 0 || areaH <= 0) return r;
	double nw = 0, nh = 0;
	naturalSize(width, height, machineAspect, nw, nh);
	if (nw <= 0 || nh <= 0) return r;
	if (!letterbox)
	{
		/* stretched: whole steps of each side when asked, else the area */
		r.w = integerScale && areaW >= nw ? std::floor(areaW / nw) * nw : areaW;
		r.h = integerScale && areaH >= nh ? std::floor(areaH / nh) * nh : areaH;
	}
	else
	{
		double scale = std::min(areaW / nw, areaH / nh);
		/* whole steps only, once there is room for one */
		if (integerScale && scale >= 1) scale = std::floor(scale);
		r.w = nw * scale;
		r.h = nh * scale;
	}
	r.x = (areaW - r.w) / 2;
	r.y = (areaH - r.h) / 2;
	return r;
}

DisplayViewModel::DisplayViewModel(Dispatcher &dispatcher) : ViewModel("display", dispatcher)
{
	expose("aspectMode", "the picture's shape: 0 its raw pixels, 1 the machine's own, 2 a size (customWidth x "
		"customHeight), 3 a ratio (ratioX:ratioY)", aspectMode, true);
	expose("customWidth", "the size the picture is shown as, across", customWidth, true);
	expose("customHeight", "the size the picture is shown as, down", customHeight, true);
	expose("ratioX", "the ratio the picture is shown in, across", ratioX, true);
	expose("ratioY", "the ratio the picture is shown in, down", ratioY, true);
	expose("letterbox", "the picture kept to its shape with bars; else stretched to the window", letterbox, true);
	expose("integerScale", "the picture grown by whole steps of its size only", integerScale, true);
	expose("cropLeft", "pixels cut off the picture's left edge", cropLeft, true);
	expose("cropTop", "pixels cut off the picture's top edge", cropTop, true);
	expose("cropRight", "pixels cut off the picture's right edge", cropRight, true);
	expose("cropBottom", "pixels cut off the picture's bottom edge", cropBottom, true);
	expose("finalFilter", "the picture drawn sharp (0) or smoothed (1)", finalFilter, true);
	expose("saveWindowPosition", "the window's place and size kept for the next start", saveWindowPosition, true);
	expose("allowDoubleClickFullscreen", "a double click on the picture goes full screen and back", allowDoubleClickFullscreen, true);
	expose("menuWindowed", "the menu bar shown in a window", menuWindowed, true);
	expose("captionWindowed", "the title bar's buttons shown in a window", captionWindowed, true);
	expose("frameWindowed", "the window's frame: 0 none, 1 thin, 2 thick", frameWindowed, true);
	expose("menuFullscreen", "the menu bar shown full screen", menuFullscreen, true);
	expose("statusBarFullscreen", "the status bar shown full screen", statusBarFullscreen, true);
	expose("autohideMouseFullscreen", "full screen, the pointer hidden while it is still", autohideMouseFullscreen, true);
	expose("stayOnTop", "the main window kept over others", stayOnTop, true);
	expose("minimalDisplay", "Display Features' Minimal picked (drawn as Full, as Chimera's)", minimalDisplay, true);
	expose("staticWindowTitles", "the main window's title keeps to its last words", staticWindowTitles, true);
	expose("titleSuffix", "the words every main window title ends with (\"\" for \"Chimera\")", titleSuffix, true);
	expose("windowGeometry", "the window's place and size kept, \"x,y,w,h\"", windowGeometry, true);
}

DisplayFit DisplayViewModel::fit() const
{
	DisplayFit f;
	f.aspectMode = static_cast<int>(std::clamp<int64_t>(aspectMode.get(), 0, 3));
	f.customWidth = static_cast<int>(std::max<int64_t>(0, customWidth.get()));
	f.customHeight = static_cast<int>(std::max<int64_t>(0, customHeight.get()));
	f.ratioX = ratioX.get();
	f.ratioY = ratioY.get();
	f.letterbox = letterbox.get();
	f.integerScale = integerScale.get();
	f.cropLeft = static_cast<int>(cropLeft.get());
	f.cropTop = static_cast<int>(cropTop.get());
	f.cropRight = static_cast<int>(cropRight.get());
	f.cropBottom = static_cast<int>(cropBottom.get());
	return f;
}

} // namespace chimera::vm
