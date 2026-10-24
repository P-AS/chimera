#pragma once
/* What is drawn over the picture, painted into a picture of the machine's own
 * size, for Encode Video's Capture Lua and Capture OSD (Chimera's): the shapes
 * scripts draw with the gui library, as Shapes.js paints them on the window's
 * canvas, in the machine's pixels; and lines of text placed over them (the
 * counters and messages, as the window lays them out for that size). */

#include <QColor>
#include <QImage>
#include <QString>

#include <vector>

namespace chimera::qt {

struct OverlayText
{
	QString text;
	int x = 0, y = 0;
	QColor color;
};

/* the gui overlay's shapes (its JSON list) */
void paintShapes(QImage &image, const QString &shapes);
/* lines of 12-pixel monospace text with a black outline, as the window's counters */
void paintTexts(QImage &image, const std::vector<OverlayText> &texts);

} // namespace chimera::qt
