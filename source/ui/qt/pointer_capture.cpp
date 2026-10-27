#include "pointer_capture.hpp"

#include <QCursor>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>

namespace chimera::qt {

PointerCapture::PointerCapture(QObject *parent) : QObject(parent)
{
	timer_.setInterval(8);
	connect(&timer_, &QTimer::timeout, this, &PointerCapture::tick);
}

void PointerCapture::confine(QQuickItem *item)
{
	const bool was = active();
	item_ = item;
	if (item_) timer_.start();
	else timer_.stop();
	if (was != active()) Q_EMIT activeChanged();
}

void PointerCapture::release()
{
	confine(nullptr);
}

void PointerCapture::tick()
{
	if (!item_ || item_->window() == nullptr || !item_->window()->isActive()) return;
	/* the item's place on the screen, a pixel in from each edge */
	const QPointF topLeft = item_->mapToGlobal(QPointF(0, 0));
	const QPointF bottomRight = item_->mapToGlobal(QPointF(item_->width(), item_->height()));
	const int left = static_cast<int>(std::ceil(topLeft.x())) + 1, top = static_cast<int>(std::ceil(topLeft.y())) + 1;
	const int right = static_cast<int>(std::floor(bottomRight.x())) - 2, bottom = static_cast<int>(std::floor(bottomRight.y())) - 2;
	if (right <= left || bottom <= top) return;
	const QPoint at = QCursor::pos();
	const QPoint inside(std::clamp(at.x(), left, right), std::clamp(at.y(), top, bottom));
	if (inside != at) QCursor::setPos(inside);
}

} // namespace chimera::qt
