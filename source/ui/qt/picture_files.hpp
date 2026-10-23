#pragma once

#include <QDir>
#include <QImage>
#include <QObject>
#include <QStandardPaths>
#include <QString>

#include "chimera/vm/picture.hpp"

namespace chimera::qt {

/* The machine's picture as a PNG file, for QML: a bug report's game.png
 * (Chimera's Report an Issue puts the game as it is now in the zip). */
class PictureFiles : public QObject
{
	Q_OBJECT
public:
	explicit PictureFiles(vm::Picture &picture, QObject *parent = nullptr) : QObject(parent), picture_(picture) {}
	/* written where asked; "" when there is no picture or it could not be */
	Q_INVOKABLE QString savePng(const QString &path) const
	{
		vm::Picture::Frame f;
		if (!picture_.takeIfNewer(UINT64_MAX, f) || f.width <= 0 || f.height <= 0) return {};
		const QImage image(reinterpret_cast<const uchar *>(f.pixels.data()), f.width, f.height, f.width * 4, QImage::Format_RGB32);
		return image.save(path, "PNG") ? path : QString();
	}
	/* a file in the system's place for passing files, for one written and read straight away */
	Q_INVOKABLE QString temporaryFile(const QString &name) const
	{
		return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(name);
	}

private:
	vm::Picture &picture_;
};

} // namespace chimera::qt
