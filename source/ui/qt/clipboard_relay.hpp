#pragma once

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVariant>

namespace chimera::qt {

/* The system's clipboard, for QML: the piano roll's Copy and Cut put their
 * lines there and Paste reads lines back from it, as Chimera's do, so frames
 * go between projects and to and from a text editor. */
class ClipboardRelay : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool hasText READ hasText NOTIFY changed)
public:
	explicit ClipboardRelay(QObject *parent = nullptr) : QObject(parent)
	{
		connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ClipboardRelay::changed);
	}
	bool hasText() const { return !QGuiApplication::clipboard()->text().isEmpty(); }
	Q_INVOKABLE QString text() const { return QGuiApplication::clipboard()->text(); }
	Q_INVOKABLE void setText(const QString &text) { QGuiApplication::clipboard()->setText(text); }
	/* a picture (an item grabbed) on the clipboard: the Surface Viewer's Copy to Clipboard */
	Q_INVOKABLE void setImage(const QVariant &image) { QGuiApplication::clipboard()->setImage(image.value<QImage>()); }

Q_SIGNALS:
	void changed();
};

} // namespace chimera::qt
