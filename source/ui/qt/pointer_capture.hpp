#pragma once
/* Chimera's Capture Mouse: the pointer kept over the picture while the
 * window is active (Chimera clips it there with Cursor.Clip). Qt has no
 * clip, so a pointer that leaves is put back inside, the nearest point,
 * a tick at a time. The window hides it; this keeps it in. */

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>

namespace chimera::qt {

class PointerCapture : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool active READ active NOTIFY activeChanged)
public:
	explicit PointerCapture(QObject *parent = nullptr);
	bool active() const { return !item_.isNull(); }
	/* keeps the pointer over this item until released */
	Q_INVOKABLE void confine(QQuickItem *item);
	Q_INVOKABLE void release();

Q_SIGNALS:
	void activeChanged();

private:
	void tick();
	QPointer<QQuickItem> item_;
	QTimer timer_;
};

} // namespace chimera::qt
