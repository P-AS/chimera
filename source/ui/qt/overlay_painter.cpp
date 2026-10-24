#include "overlay_painter.hpp"

#include <QFont>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace chimera::qt {

namespace {
QFont monospace(int pixels)
{
	QFont f(QStringLiteral("monospace"));
	f.setStyleHint(QFont::Monospace);
	f.setPixelSize(pixels > 0 ? pixels : 12);
	return f;
}

QColor colorOf(const QJsonValue &v)
{
	const QString s = v.toString();
	return s.isEmpty() ? QColor() : QColor::fromString(s);
}

/* an image file read once, again for a new epoch (clearImageCache), as the canvas loads them */
const QImage &imageOf(const QString &file, int epoch)
{
	static QMap<QString, QImage> cache;
	const QString key = file + QLatin1Char('\n') + QString::number(epoch);
	auto it = cache.find(key);
	if (it == cache.end()) it = cache.insert(key, QImage(file));
	return *it;
}

void strokeAndFill(QPainter &p, const QPainterPath &path, const QJsonObject &s)
{
	const QColor fill = colorOf(s.value(QStringLiteral("fill"))), line = colorOf(s.value(QStringLiteral("line")));
	if (fill.isValid()) p.fillPath(path, fill);
	if (line.isValid()) p.strokePath(path, QPen(line, 1));
}
} // namespace

void paintShapes(QImage &image, const QString &shapes)
{
	const QJsonArray list = QJsonDocument::fromJson(shapes.toUtf8()).array();
	if (list.isEmpty()) return;
	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);
	for (const QJsonValue &value : list)
	{
		const QJsonObject s = value.toObject();
		const QString k = s.value(QStringLiteral("k")).toString();
		auto num = [&s](const char *name) { return s.value(QLatin1String(name)).toDouble(); };
		const double x = num("x"), y = num("y"), w = num("w"), h = num("h");
		p.save();
		if (k == QLatin1String("rect"))
		{
			const QColor fill = colorOf(s.value(QStringLiteral("fill"))), line = colorOf(s.value(QStringLiteral("line")));
			if (fill.isValid()) p.fillRect(QRectF(x, y, w, h), fill);
			if (line.isValid())
			{
				p.setPen(QPen(line, 1));
				p.drawRect(QRectF(x + 0.5, y + 0.5, w, h));
			}
		}
		else if (k == QLatin1String("ellipse"))
		{
			QPainterPath path;
			path.addEllipse(QRectF(x, y, w, h));
			strokeAndFill(p, path, s);
		}
		else if (k == QLatin1String("line"))
		{
			p.setPen(QPen(colorOf(s.value(QStringLiteral("line"))), 1));
			p.drawLine(QPointF(x + 0.5, y + 0.5), QPointF(num("x2") + 0.5, num("y2") + 0.5));
		}
		else if (k == QLatin1String("path") || k == QLatin1String("bezier"))
		{
			const QJsonArray points = s.value(QStringLiteral("points")).toArray();
			auto at = [&points](int i) { return QPointF(points[i].toArray()[0].toDouble() + 0.5, points[i].toArray()[1].toDouble() + 0.5); };
			if (!points.isEmpty())
			{
				QPainterPath path(at(0));
				if (k == QLatin1String("bezier") && points.size() >= 4)
					path.cubicTo(at(1), at(2), at(3));
				else
					for (int i = 1; i < points.size(); i++) path.lineTo(at(i));
				if (s.value(QStringLiteral("closed")).toBool()) path.closeSubpath();
				strokeAndFill(p, path, s);
			}
		}
		else if (k == QLatin1String("pie"))
		{
			/* the canvas turns clockwise from three o'clock; Qt counts the other way */
			const QRectF box(x, y, w, h);
			const double start = num("start"), sweep = num("sweep");
			QPainterPath path;
			if (!s.value(QStringLiteral("arc")).toBool()) path.moveTo(box.center());
			path.arcMoveTo(box, -start);
			path.arcTo(box, -start, -sweep);
			if (!s.value(QStringLiteral("arc")).toBool())
			{
				path.lineTo(box.center());
				path.closeSubpath();
			}
			strokeAndFill(p, path, s);
		}
		else if (k == QLatin1String("image"))
		{
			const QImage &img = imageOf(s.value(QStringLiteral("file")).toString(), s.value(QStringLiteral("epoch")).toInt());
			if (!img.isNull())
			{
				if (s.contains(QStringLiteral("sw")))
					p.drawImage(QRectF(x, y, w, h), img, QRectF(num("sx"), num("sy"), num("sw"), num("sh")));
				else if (w >= 0 && h >= 0 && s.contains(QStringLiteral("w")))
					p.drawImage(QRectF(x, y, w, h), img);
				else
					p.drawImage(QPointF(x, y), img);
			}
		}
		else if (k == QLatin1String("text") || k == QLatin1String("screentext"))
		{
			const bool screen = k == QLatin1String("screentext");
			const int size = screen ? 12 : static_cast<int>(num("size"));
			const QFont font = monospace(size);
			const QString text = s.value(QStringLiteral("text")).toString();
			const QFontMetrics metrics(font);
			const int tw = metrics.horizontalAdvance(text);
			double tx = x, ty = y;
			if (screen)
			{
				/* from a corner of the picture, as the window places it in its own */
				const QString a = s.value(QStringLiteral("anchor")).toString();
				const bool right = a == QLatin1String("topright") || a == QLatin1String("bottomright") || a == QLatin1String("1") || a == QLatin1String("3");
				const bool bottom = a == QLatin1String("bottomleft") || a == QLatin1String("bottomright") || a == QLatin1String("2") || a == QLatin1String("3");
				if (right) tx = image.width() - x - tw;
				if (bottom) ty = image.height() - y - 12;
			}
			const QColor back = colorOf(s.value(QStringLiteral("back")));
			if (!screen && back.isValid()) p.fillRect(QRectF(tx, ty, tw, size), back);
			p.setFont(font);
			p.setPen(colorOf(s.value(QStringLiteral("color"))));
			p.drawText(QPointF(tx, ty + metrics.ascent()), text);
		}
		p.restore();
	}
}

void paintTexts(QImage &image, const std::vector<OverlayText> &texts)
{
	if (texts.empty()) return;
	QPainter p(&image);
	const QFont font = monospace(12);
	p.setFont(font);
	const int ascent = QFontMetrics(font).ascent();
	for (const OverlayText &t : texts)
	{
		const QPointF at(t.x, t.y + ascent);
		/* the outline Qt Quick's Text draws, a pixel of black all round */
		p.setPen(Qt::black);
		for (int dx = -1; dx <= 1; dx++)
			for (int dy = -1; dy <= 1; dy++)
				if (dx != 0 || dy != 0) p.drawText(at + QPointF(dx, dy), t.text);
		p.setPen(t.color);
		p.drawText(at, t.text);
	}
}

} // namespace chimera::qt
