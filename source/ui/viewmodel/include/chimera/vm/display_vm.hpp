#pragma once
/* Chimera's Display window (Config > Display), for what the window draws:
 * the picture's shape (the machine's own, its raw pixels, a size or a ratio
 * of the person's), kept with bars or stretched to fill, grown by whole
 * steps only or freely, edges cropped, and drawn sharp or smoothed. The
 * fitting is worked out here (fit), so it is the same wherever the picture
 * is drawn and can be tested; the window only draws the rectangle it gets.
 * Also where the window is kept: its place and size remembered when asked.
 * All remembered. */

#include <cstdint>
#include <string>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

struct DisplayFit
{
	/* 0 the raw pixels, 1 the machine's own shape, 2 a size, 3 a ratio */
	int aspectMode = 1;
	int customWidth = 0, customHeight = 0;
	double ratioX = 4, ratioY = 3;
	/* kept to its shape with bars; else stretched to the area */
	bool letterbox = true;
	/* grown by whole steps of its size only */
	bool integerScale = false;
	int cropLeft = 0, cropTop = 0, cropRight = 0, cropBottom = 0;
	/* Chimera's extra paddings, a script's (client.SetGameExtraPadding and
	 * SetClientExtraPadding): the game's, black around the picture in its
	 * own pixels, making the surface scripts draw on larger; the client's,
	 * window pixels left free around everything */
	int padLeft = 0, padTop = 0, padRight = 0, padBottom = 0;
	int clientLeft = 0, clientTop = 0, clientRight = 0, clientBottom = 0;

	/* the part of the picture shown, in its pixels (the crop taken off) */
	struct Rect
	{
		double x = 0, y = 0, w = 0, h = 0;
	};
	Rect source(int width, int height) const;
	/* its size shown at 1x: the height kept, the width its shape gives (a
	 * custom size is that size) */
	void naturalSize(int width, int height, double machineAspect, double &w, double &h) const;
	/* the surface at 1x: the picture shown with the game padding around it */
	void surfaceSize(int width, int height, double machineAspect, double &w, double &h) const;
	/* where the surface goes in an area of areaW x areaH (inside the client
	 * padding), and where the picture goes, inside its padding */
	Rect surface(int width, int height, double machineAspect, double areaW, double areaH) const;
	Rect place(int width, int height, double machineAspect, double areaW, double areaH) const;
};

class DisplayViewModel : public ViewModel
{
public:
	explicit DisplayViewModel(Dispatcher &dispatcher);

	Property<int64_t> aspectMode{1}, customWidth{0}, customHeight{0};
	Property<double> ratioX{4}, ratioY{3};
	Property<bool> letterbox{true}, integerScale{false};
	Property<int64_t> cropLeft{0}, cropTop{0}, cropRight{0}, cropBottom{0};
	/* the extra paddings a script set (not remembered, as Chimera's) */
	Property<int64_t> padLeft{0}, padTop{0}, padRight{0}, padBottom{0};
	Property<int64_t> clientLeft{0}, clientTop{0}, clientRight{0}, clientBottom{0};
	/* where the window shows the picture, told by the window for scripts:
	 * the picture area's size, the picture's place in it, and the part of
	 * the machine's picture shown ("w h x y w h x y w h", window pixels then
	 * the machine's) */
	Property<std::string> placement;
	/* 0 sharp (nearest), 1 smoothed (bilinear) */
	Property<int64_t> finalFilter{0};
	/* the window's place and size kept for the next start, and what was kept ("x,y,w,h") */
	Property<bool> saveWindowPosition{false};
	Property<std::string> windowGeometry;
	/* a double click on the picture goes full screen and back (Allow Double-Click Fullscreen) */
	Property<bool> allowDoubleClickFullscreen{true};
	/* the window's chrome, as Chimera's: windowed, the menu, the caption's buttons and the frame
	 * (0 none, 1 thin, 2 thick); full screen, the menu, the status bar, and the pointer hidden
	 * while it is still; the window kept over others */
	Property<bool> menuWindowed{true}, captionWindowed{true};
	Property<int64_t> frameWindowed{2};
	Property<bool> menuFullscreen{false}, statusBarFullscreen{false}, autohideMouseFullscreen{true};
	Property<bool> stayOnTop{false};
	/* Display Features' Minimal picked (Chimera draws everything for it, as for Full) */
	Property<bool> minimalDisplay{false};
	/* Keep window titles static, and the words every main window title ends with ("" for "Chimera") */
	Property<bool> staticWindowTitles{false};
	Property<std::string> titleSuffix;
	/* View > Window Size, as Chimera's: the picture's size times each
	 * system's own scale (2 at first, 1 to 10, remembered per system), the
	 * window following the picture's size unless Static Size is ticked */
	Property<bool> resizeWithPicture{true};
	Property<std::string> windowScales{"{}"};
	int64_t scaleFor(const std::string &system) const;

	DisplayFit fit() const;
};

} // namespace chimera::vm
