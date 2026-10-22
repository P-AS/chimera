#include "picture_item.hpp"

#include <QImage>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>

#include <cstring>

#include "qml_view_model.hpp"

namespace chimera::qt {

PictureItem::PictureItem(QQuickItem *parent) : QQuickItem(parent)
{
	setFlag(ItemHasContents, true);
}

QObject *PictureItem::source() const { return source_.data(); }

void PictureItem::setSource(QObject *source)
{
	QmlPicture *p = qobject_cast<QmlPicture *>(source);
	if (p == source_) return;
	disconnect(watch_);
	source_ = p;
	if (p != nullptr) watch_ = connect(p, &QmlPicture::changed, this, &QQuickItem::update);
	Q_EMIT sourceChanged();
	update();
}

void PictureItem::setSmoothScaling(bool on)
{
	if (on == smooth_) return;
	smooth_ = on;
	Q_EMIT smoothScalingChanged();
	update();
}

QSGNode *PictureItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
	auto *node = static_cast<QSGSimpleTextureNode *>(old);
	const bool fresh = source_ != nullptr && source_->picture().takeIfNewer(frame_.serial, frame_);
	if (frame_.width <= 0 || frame_.height <= 0)
	{
		delete node;
		return nullptr;
	}
	if (node == nullptr)
	{
		node = new QSGSimpleTextureNode;
		node->setOwnsTexture(true);
	}
	if (fresh || node->texture() == nullptr)
	{
		/* the image owns its pixels: the texture may be uploaded after this
		 * copy of the frame has been overwritten by the next one */
		QImage image(frame_.width, frame_.height, QImage::Format_RGB32);
		const size_t row = static_cast<size_t>(frame_.width) * sizeof(uint32_t);
		for (int y = 0; y < frame_.height; y++)
			std::memcpy(image.scanLine(y), frame_.pixels.data() + static_cast<size_t>(y) * frame_.width, row);
		node->setTexture(window()->createTextureFromImage(image));
		shown_++;
	}
	node->setFiltering(smooth_ ? QSGTexture::Linear : QSGTexture::Nearest);

	const double w = width(), h = height();
	const double aspect = frame_.aspect > 0 ? frame_.aspect : static_cast<double>(frame_.width) / frame_.height;
	double pw = w, ph = w / aspect;
	if (ph > h)
	{
		ph = h;
		pw = h * aspect;
	}
	node->setRect(QRectF((w - pw) / 2, (h - ph) / 2, pw, ph));
	return node;
}

} // namespace chimera::qt
