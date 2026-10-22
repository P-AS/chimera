#pragma once
/* Draws a view model's picture, kept to the machine's shape and centred. */

#include <QPointer>
#include <QQuickItem>

#include <atomic>

#include "chimera/vm/picture.hpp"

namespace chimera::qt {

class QmlPicture;

class PictureItem : public QQuickItem
{
	Q_OBJECT
	Q_PROPERTY(QObject *source READ source WRITE setSource NOTIFY sourceChanged)
	Q_PROPERTY(bool smooth READ smoothScaling WRITE setSmoothScaling NOTIFY smoothScalingChanged)

public:
	explicit PictureItem(QQuickItem *parent = nullptr);

	QObject *source() const;
	void setSource(QObject *source);
	bool smoothScaling() const { return smooth_; }
	void setSmoothScaling(bool on);

	/* pictures handed to the GPU so far, for measuring (any thread) */
	qint64 framesShown() const { return shown_.load(); }

Q_SIGNALS:
	void sourceChanged();
	void smoothScalingChanged();

protected:
	QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
	QPointer<QmlPicture> source_;
	QMetaObject::Connection watch_;
	bool smooth_ = false;
	std::atomic<qint64> shown_{0};
	/* the render thread's copy of the newest frame */
	vm::Picture::Frame frame_;
};

} // namespace chimera::qt
