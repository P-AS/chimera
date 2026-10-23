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

	/* the part of the picture shown, in its pixels (the crop taken off) */
	struct Rect
	{
		double x = 0, y = 0, w = 0, h = 0;
	};
	Rect source(int width, int height) const;
	/* its size shown at 1x: the height kept, the width its shape gives (a
	 * custom size is that size) */
	void naturalSize(int width, int height, double machineAspect, double &w, double &h) const;
	/* where it goes in an area of areaW x areaH */
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
	/* 0 sharp (nearest), 1 smoothed (bilinear) */
	Property<int64_t> finalFilter{0};
	/* the window's place and size kept for the next start, and what was kept ("x,y,w,h") */
	Property<bool> saveWindowPosition{false};
	Property<std::string> windowGeometry;

	DisplayFit fit() const;
};

} // namespace chimera::vm
