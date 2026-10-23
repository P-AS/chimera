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

void PictureItem::setFit(const QVariantMap &fit)
{
	if (fit == fitMap_) return;
	fitMap_ = fit;
	auto number = [&](const char *name, double otherwise) {
		const auto it = fit.find(QString::fromLatin1(name));
		return it == fit.end() ? otherwise : it->toDouble();
	};
	vm::DisplayFit f;
	f.aspectMode = static_cast<int>(number("aspectMode", 1));
	f.customWidth = static_cast<int>(number("customWidth", 0));
	f.customHeight = static_cast<int>(number("customHeight", 0));
	f.ratioX = number("ratioX", 4);
	f.ratioY = number("ratioY", 3);
	f.letterbox = number("letterbox", 1) != 0;
	f.integerScale = number("integerScale", 0) != 0;
	f.cropLeft = static_cast<int>(number("cropLeft", 0));
	f.cropTop = static_cast<int>(number("cropTop", 0));
	f.cropRight = static_cast<int>(number("cropRight", 0));
	f.cropBottom = static_cast<int>(number("cropBottom", 0));
	fit_ = f;
	fitNew_ = true;
	Q_EMIT fitChanged();
	update();
}

void PictureItem::sizeSeen(int width, int height)
{
	if (width == pictureWidth_ && height == pictureHeight_) return;
	pictureWidth_ = width;
	pictureHeight_ = height;
	Q_EMIT pictureSizeChanged();
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
	/* the GUI's thread waits while this runs: its fit can be read */
	if (fresh || fitNew_)
	{
		/* told to the GUI's thread, the picture's size at 1x as it is fitted */
		double nw = 0, nh = 0;
		fit_.naturalSize(frame_.width, frame_.height, frame_.aspect, nw, nh);
		QMetaObject::invokeMethod(this, "sizeSeen", Qt::QueuedConnection, Q_ARG(int, static_cast<int>(nw + 0.5)),
			Q_ARG(int, static_cast<int>(nh + 0.5)));
		fitNew_ = false;
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

	const vm::DisplayFit::Rect from = fit_.source(frame_.width, frame_.height);
	node->setSourceRect(QRectF(from.x, from.y, from.w, from.h));
	const vm::DisplayFit::Rect to = fit_.place(frame_.width, frame_.height, frame_.aspect, width(), height());
	node->setRect(QRectF(to.x, to.y, to.w, to.h));
	return node;
}

} // namespace chimera::qt
