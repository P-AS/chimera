#include "piano_roll_item.hpp"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPalette>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "qml_view_model.hpp"
#include "theme_palette.hpp"

namespace chimera::qt {

PianoRollItem::PianoRollItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
	setOpaquePainting(true);
	setAntialiasing(false);
	setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
	connect(this, &PianoRollItem::pickedChanged, this, [this] { update(); });
	connect(this, &PianoRollItem::lookChanged, this, [this] { update(); });
}

void PianoRollItem::setColors(const QString &json)
{
	if (json == colors_) return;
	colors_ = json;
	Q_EMIT lookChanged();
}

void PianoRollItem::setRollFont(const QString &font)
{
	if (font == rollFont_) return;
	rollFont_ = font;
	font_ = QFont();
	/* {"family": "Arial", "pointSize": 8.25, "bold": true, "italic": false} */
	const QJsonObject f = QJsonDocument::fromJson(font.toUtf8()).object();
	if (f.contains(QLatin1String("family"))) font_.setFamily(f.value(QLatin1String("family")).toString());
	if (f.value(QLatin1String("pointSize")).toDouble() > 0) font_.setPointSizeF(f.value(QLatin1String("pointSize")).toDouble());
	font_.setBold(f.value(QLatin1String("bold")).toBool());
	font_.setItalic(f.value(QLatin1String("italic")).toBool());
	/* the rows as tall as the font needs */
	rowHeight_ = std::max(18, QFontMetrics(font_).height() + 4);
	Q_EMIT lookChanged();
	Q_EMIT visibleRowsChanged();
}

int PianoRollItem::columnW(int column) const
{
	if (rows_ == nullptr) return columnWidth_;
	/* a hidden column takes no room */
	if (rows_->rows().columnHidden(column)) return 0;
	return rows_->rows().columnIsNumber(column) ? numberWidth_ : columnWidth_;
}

qint64 PianoRollItem::rowOnLine(qint64 line) const
{
	if (rows_ == nullptr) return top_ + line;
	vm::RowModel &rows = rows_->rows();
	qint64 row = rows.nextShown(top_ - 1);
	for (qint64 i = 0; i < line; i++) row = rows.nextShown(row);
	return row;
}

int PianoRollItem::columnX(int column) const
{
	/* the machine's columns, then the script's */
	const int own = rows_ != nullptr ? rows_->rows().columnCount() : 0;
	int x = frameColumnWidth();
	for (int c = 0; c < column && c < own; c++) x += columnW(c);
	for (int e = 0; e < column - own && e < static_cast<int>(extras_.size()); e++) x += extras_[static_cast<size_t>(e)].width;
	return x;
}

void PianoRollItem::setExtraColumns(const QString &json)
{
	if (json == extraJson_) return;
	extraJson_ = json;
	extras_.clear();
	for (const QJsonValue v : QJsonDocument::fromJson(json.toUtf8()).array())
	{
		const QJsonObject o = v.toObject();
		extras_.push_back({o.value(QLatin1String("name")).toString(), o.value(QLatin1String("text")).toString(),
			std::max(8, o.value(QLatin1String("width")).toInt(30))});
	}
	Q_EMIT lookChanged();
	update();
}

QObject *PianoRollItem::rows() const { return rows_.data(); }

void PianoRollItem::setRows(QObject *rows)
{
	QmlRows *r = qobject_cast<QmlRows *>(rows);
	if (r == rows_) return;
	for (auto &c : watches_) disconnect(c);
	watches_.clear();
	rows_ = r;
	if (r != nullptr)
	{
		watches_.push_back(connect(r, &QmlRows::rowsEdited, this, &PianoRollItem::rowsTouched));
		watches_.push_back(connect(r, &QmlRows::rowsGrew, this, [this] {
			Q_EMIT rowCountChanged();
			update();
		}));
		watches_.push_back(connect(r, &QmlRows::rowsReset, this, [this] {
			Q_EMIT rowCountChanged();
			update();
		}));
	}
	Q_EMIT rowsChanged();
	Q_EMIT rowCountChanged();
	update();
}

qint64 PianoRollItem::rowCount() const { return rows_ != nullptr ? rows_->rows().rowCount() : 0; }

int PianoRollItem::visibleRows() const
{
	return std::max(0, (logicalHeight() - headerSize()) / rowHeight_); /* less the header */
}

void PianoRollItem::setHorizontal(bool on)
{
	if (on == horizontal_) return;
	horizontal_ = on;
	Q_EMIT lookChanged();
	Q_EMIT visibleRowsChanged();
	update();
}

int PianoRollItem::headerSize() const
{
	if (!horizontal_) return rowHeight_;
	const QFontMetrics fm(font_);
	int widest = fm.horizontalAdvance(QStringLiteral("Frame"));
	if (rows_ != nullptr)
		for (int c = 0; c < rows_->rows().columnCount(); c++)
			widest = std::max(widest, fm.horizontalAdvance(QString::fromStdString(rows_->rows().columnName(c))));
	return widest + 12;
}

void PianoRollItem::setTopRow(qint64 row)
{
	if (row == top_) return;
	top_ = row;
	Q_EMIT topRowChanged();
	update();
}

void PianoRollItem::setCurrentRow(qint64 row)
{
	if (row == current_) return;
	const qint64 was = current_;
	current_ = row;
	Q_EMIT currentRowChanged();
	const qint64 last = lastShown();
	if ((was >= top_ && was <= last) || (row >= top_ && row <= last)) update();
}

void PianoRollItem::setSelectFirst(qint64 row)
{
	if (row == selectFirst_) return;
	selectFirst_ = row;
	Q_EMIT selectionChanged();
	update();
}

void PianoRollItem::setSelectLast(qint64 row)
{
	if (row == selectLast_) return;
	selectLast_ = row;
	Q_EMIT selectionChanged();
	update();
}

void PianoRollItem::rowsTouched(qint64 first, qint64 last)
{
	if (last >= top_ && first <= lastShown()) update();
}

/* TAStudio's two columns before the buttons: a narrow one a press in moves
 * the machine to its frame, then the frame numbers, which pick frames */
static constexpr int kCursorWidth = 14;

int PianoRollItem::frameColumnWidth() const
{
	return kCursorWidth + QFontMetrics(font_).horizontalAdvance(QStringLiteral("00000000")) + 8;
}

void PianoRollItem::geometryChange(const QRectF &now, const QRectF &before)
{
	QQuickPaintedItem::geometryChange(now, before);
	if (now.height() != before.height() || now.width() != before.width()) Q_EMIT visibleRowsChanged();
}

bool PianoRollItem::cellAt(QPointF p, qint64 &row, int &column) const
{
	if (rows_ == nullptr) return false;
	const int fx = frameColumnWidth();
	const int hs = headerSize();
	if (p.x() < fx || p.y() < hs) return false;
	const int columns = rows_->rows().columnCount();
	column = -1;
	for (int c = 0, x = fx; c < columns; x += columnW(c), c++)
		if (p.x() >= x && p.x() < x + columnW(c)) column = c;
	row = rowOnLine(static_cast<qint64>((p.y() - hs) / rowHeight_));
	return column >= 0 && row < rows_->rows().rowCount();
}

void PianoRollItem::paint(QPainter *p)
{
	QElapsedTimer timer;
	timer.start();

	/* TAStudio's meanings: the current frame blue, the greenzone green, a
	 * lag frame in it red, frames not run with their inputs plain. The
	 * colours follow the system's light or dark theme: each meaning is a
	 * tint mixed into the theme's own background. */
	const QPalette pal = QGuiApplication::palette();
	QColor base = pal.color(QPalette::Base), text = pal.color(QPalette::Text);
	const bool dark = base.lightnessF() < 0.5;
	auto tint = [&](QColor c, double amount) {
		return QColor::fromRgbF(base.redF() + (c.redF() - base.redF()) * amount,
			base.greenF() + (c.greenF() - base.greenF()) * amount,
			base.blueF() + (c.blueF() - base.blueF()) * amount);
	};
	const double a = dark ? 0.32 : 0.22;
	QColor plain = base, header = pal.color(QPalette::Button), headerText = text, grid = tint(text, 0.18),
		lit = pal.color(QPalette::Highlight), now = tint(QColor(0x3d, 0x8b, 0xff), a + 0.08),
		green = tint(QColor(0x3c, 0xc0, 0x3c), a), lag = tint(QColor(0xe0, 0x40, 0x30), a),
		marker = tint(QColor(0xe0, 0xa0, 0x20), dark ? 0.5 : 0.45), picked = lit, pickedText = pal.color(QPalette::HighlightedText);
	/* a Chimera theme says TAStudio's colours itself */
	auto themed = [](QColor &c, const char *role) {
		const QColor t = themeColor(role);
		if (t.isValid()) c = t;
	};
	themed(plain, "RollBackground");
	themed(text, "RollText");
	themed(header, "HeaderBackground");
	themed(headerText, "HeaderText");
	themed(grid, "RollGridLines");
	themed(now, "TasCurrentFrame");
	themed(green, "TasGreenZoneInput");
	themed(lag, "TasLagZoneInput");
	themed(marker, "TasMarker");
	/* the run's own markers (start, last input, end) are not the person's and
	 * do not look like them: Chimera's light and dark themes' colour */
	QColor permanent = dark ? QColor(0x1F, 0x33, 0x50) : QColor(0xC9, 0xE4, 0xFF);
	themed(permanent, "TasPermanentMarker");
	themed(picked, "RollSelection");
	themed(pickedText, "RollSelectionText");
	/* the greenzone's frames with a state held, and those an edit sent the
	 * greenzone back past (Chimera's "invalidated"); the frame column's
	 * wash; every second player's columns */
	QColor greenStated = tint(QColor(0x3c, 0xc0, 0x3c), a + 0.08), lagStated = tint(QColor(0xe0, 0x40, 0x30), a + 0.08),
		greenGone = tint(QColor(0x3c, 0xc0, 0x3c), a * 0.4), lagGone = tint(QColor(0xe0, 0x40, 0x30), a * 0.4),
		frameWash, altPlayer = QColor(text.red(), text.green(), text.blue(), 14);
	themed(greenStated, "TasGreenZoneInputStated");
	themed(lagStated, "TasLagZoneInputStated");
	themed(greenGone, "TasGreenZoneInputInvalidated");
	themed(lagGone, "TasLagZoneInputInvalidated");
	themed(frameWash, "TasFrameColumnWash");
	themed(altPlayer, "TasAlternatePlayer");
	/* the roll's own colours, over the theme's (TAStudio's Set Colors) */
	{
		const QJsonObject own = QJsonDocument::fromJson(colors_.toUtf8()).object();
		const std::pair<const char *, QColor *> roles[] = {{"TasCurrentFrame", &now}, {"TasGreenZoneInput", &green},
			{"TasGreenZoneInputStated", &greenStated}, {"TasGreenZoneInputInvalidated", &greenGone}, {"TasLagZoneInput", &lag},
			{"TasLagZoneInputStated", &lagStated}, {"TasLagZoneInputInvalidated", &lagGone}, {"TasMarker", &marker},
			{"TasDefaultRow", &plain}, {"TasFrameColumnWash", &frameWash}, {"TasAlternatePlayer", &altPlayer}};
		for (const auto &[role, color] : roles)
			if (const QColor c(own.value(QLatin1String(role)).toString()); c.isValid()) *color = c;
	}
	p->setFont(font_);
	p->fillRect(0, 0, static_cast<int>(width()), static_cast<int>(height()), plain);
	if (rows_ == nullptr) return;
	/* drawn upright; turned on its side, x and y change places, and text is
	 * drawn the right way round: names level, numbers turned, as Chimera's */
	const int w = logicalWidth(), h = logicalHeight(), hs = headerSize();
	if (horizontal_) p->setTransform(QTransform(0, 1, 1, 0, 0, 0));
	auto label = [&](const QRect &r, int flags, const QString &s, bool turn) {
		if (!horizontal_)
		{
			p->drawText(r, flags, s);
			return;
		}
		p->save();
		p->resetTransform();
		const QRect at(r.y(), r.x(), r.height(), r.width());
		if (turn)
		{
			p->translate(at.left(), at.bottom() + 1);
			p->rotate(-90);
			p->drawText(QRect(0, 0, at.height(), at.width()), flags, s);
		}
		else
			p->drawText(at, Qt::AlignVCenter | Qt::AlignRight, s);
		p->restore();
	};

	vm::RowModel &rows = rows_->rows();
	const int fx = frameColumnWidth();
	const int columns = rows.columnCount();
	const qint64 count = rows.rowCount();
	const int shown = visibleRows() + 1;

	/* the header: each button's name without the "P1 " a player's buttons
	 * share, or its letter in a movie when the name does not fit */
	p->fillRect(0, 0, w, hs, header);
	p->setPen(headerText);
	label(QRect(kCursorWidth + 4, 0, fx - kCursorWidth - 8, hs), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Frame"), false);
	for (int c = 0; c < columns; c++)
	{
		if (columnW(c) == 0) continue; /* hidden */
		QString name = QString::fromStdString(rows.columnName(c));
		const qsizetype space = name.indexOf(' ');
		if (space > 0 && space < 4) name = name.mid(space + 1);
		if (!horizontal_ && QFontMetrics(p->font()).horizontalAdvance(name) > columnW(c) - 2)
			name = QString::fromStdString(rows.columnShortName(c));
		const QRect cell(columnX(c), 0, columnW(c), hs);
		/* an Auto-hold column, marked out as Chimera's emphasis */
		if (rows.columnEmphasized(c))
		{
			QColor mark = pal.color(QPalette::Highlight);
			mark.setAlpha(110);
			p->fillRect(cell, mark);
		}
		label(cell, Qt::AlignCenter, horizontal_ ? name : QFontMetrics(p->font()).elidedText(name, Qt::ElideRight, columnW(c) - 2), false);
	}

	const int extraCount = static_cast<int>(extras_.size());
	for (int e = 0; e < extraCount; e++)
		label(QRect(columnX(columns + e), 0, extras_[static_cast<size_t>(e)].width, hs), Qt::AlignCenter, extras_[static_cast<size_t>(e)].text, false);
	/* what the script says of a cell, when it says anything */
	auto ask = [&](int kind, qint64 row, const QString &column) { return queries_ && query_ ? query_(kind, row, column) : QString(); };

	/* the rows shown, hidden ones (lag frames TAStudio hides) passed over */
	qint64 next = rows.nextShown(top_ - 1);
	for (int i = 0; i < shown; i++)
	{
		const qint64 row = next;
		if (row >= count) break;
		next = rows.nextShown(row);
		const int y = hs + rowHeight_ * i;
		const uint32_t flags = rows.rowFlags(row);
		const bool isLag = flags & 1, inGreen = flags & 2, isMarker = flags & 4, stated = flags & 8, ran = flags & 16, isPermanent = flags & 32;
		/* as Chimera's TAStudio colours a row: the current frame; the
		 * greenzone's, apart where a state is held; those an edit took out
		 * of it; the rest */
		QColor back = plain;
		if (row == current_) back = now;
		else if (inGreen) back = !stated && denoteStatesColor_ ? (isLag ? lag : green) : (isLag ? lagStated : greenStated);
		else if (ran) back = isLag ? lagGone : greenGone;
		p->fillRect(0, y, w, rowHeight_, (row % 2 && row != current_) ? (dark ? back.lighter(112) : back.darker(103)) : back);
		/* every second player's columns, striped */
		for (int c = 0; c < columns; c++)
		{
			const int cw = columnW(c);
			if (cw == 0) continue;
			const std::string name = rows.columnName(c);
			if (name.size() > 2 && name[0] == 'P' && std::isdigit(static_cast<unsigned char>(name[1])) && (std::atoi(name.c_str() + 1) % 2) == 0)
				p->fillRect(columnX(c), y, cw, rowHeight_, altPlayer);
		}
		if (frameWash.isValid()) p->fillRect(kCursorWidth, y, fx - kCursorWidth, rowHeight_, frameWash);
		/* a marker: its frame number on the accent (when markers are told by
		 * colour), its text after the buttons */
		if (isMarker && denoteMarkersColor_ && row != current_) p->fillRect(0, y, fx, rowHeight_, isPermanent ? permanent : marker);
		/* the icons: a marker's flag, else a held state's anchor */
		if (isMarker && denoteMarkersIcons_)
		{
			p->setPen(Qt::NoPen);
			p->setBrush(marker.darker(dark ? 80 : 160));
			const QPointF flag[3] = {QPointF(kCursorWidth + 3, y + 3), QPointF(kCursorWidth + 11, y + rowHeight_ / 2.0 - 1),
				QPointF(kCursorWidth + 3, y + rowHeight_ - 5)};
			p->drawPolygon(flag, 3);
			p->setBrush(Qt::NoBrush);
		}
		else if (stated && denoteStatesIcons_)
		{
			p->setPen(Qt::NoPen);
			p->setBrush((isLag ? lagStated : greenStated).darker(dark ? 60 : 170));
			p->drawEllipse(QRectF(kCursorWidth + 3, y + rowHeight_ / 2.0 - 4, 8, 8));
			p->setBrush(Qt::NoBrush);
		}
		if (isMarker)
		{
			p->setPen(text);
			const int after = columnX(columns + extraCount);
			label(QRect(after + 6, y, w - after - 8, rowHeight_), Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(rows.rowNote(row)), true);
		}
		/* the frame number's cell: the script's colour over it, when it gives one */
		if (const QColor c(ask(0, row, QStringLiteral("FrameColumn"))); c.isValid()) p->fillRect(kCursorWidth, y, fx - kCursorWidth, rowHeight_, c);
		/* picked frames: their numbers on the highlight */
		const bool isPicked = selectFirst_ >= 0 && row >= selectFirst_ && row <= selectLast_;
		if (isPicked) p->fillRect(kCursorWidth, y, fx - kCursorWidth, rowHeight_, picked);
		/* the frame the machine is on: an arrow in the cursor column */
		if (row == current_)
		{
			p->setPen(Qt::NoPen);
			p->setBrush(text);
			const QPointF arrow[3] = {QPointF(3, y + 3), QPointF(kCursorWidth - 3, y + rowHeight_ / 2.0), QPointF(3, y + rowHeight_ - 3)};
			p->drawPolygon(arrow, 3);
			p->setBrush(Qt::NoBrush);
		}
		p->setPen(isPicked ? pickedText : text);
		label(QRect(kCursorWidth + 4, y, fx - kCursorWidth - 8, rowHeight_), Qt::AlignVCenter | Qt::AlignRight, QString::number(row), true);
		for (int c = 0; c < columns; c++)
		{
			const int cx = columnX(c), cw = columnW(c);
			if (cw == 0) continue; /* hidden */
			const QString columnName = queries_ ? QString::fromStdString(rows.columnName(c)) : QString();
			if (const QColor b(ask(0, row, columnName)); b.isValid()) p->fillRect(cx, y, cw, rowHeight_, b);
			if (const QString said = ask(1, row, columnName); !said.isEmpty())
			{
				p->setPen(text);
				label(QRect(cx + 2, y, cw - 4, rowHeight_), Qt::AlignCenter, said, true);
				continue;
			}
			if (rows.columnIsNumber(c))
			{
				int64_t value = 0;
				if (vm::fromValue(rows.cell(row, c), value))
				{
					p->setPen(text);
					label(QRect(cx + 2, y, cw - 6, rowHeight_), Qt::AlignVCenter | Qt::AlignRight, QString::number(value), true);
				}
			}
			else
			{
				bool held = false;
				vm::fromValue(rows.cell(row, c), held);
				if (held) p->fillRect(cx + 2, y + 2, cw - 4, rowHeight_ - 4, lit);
			}
			if (row == pickedRow_ && c == pickedColumn_)
			{
				p->setPen(QPen(lit, 2));
				p->drawRect(cx + 1, y + 1, cw - 2, rowHeight_ - 2);
			}
		}
		/* the script's own columns: what it says, nothing else */
		for (int e = 0; e < extraCount; e++)
		{
			const Extra &x = extras_[static_cast<size_t>(e)];
			const int cx = columnX(columns + e);
			if (const QColor b(ask(0, row, x.name)); b.isValid()) p->fillRect(cx, y, x.width, rowHeight_, b);
			if (const QString said = ask(1, row, x.name); !said.isEmpty())
			{
				p->setPen(text);
				label(QRect(cx + 2, y, x.width - 4, rowHeight_), Qt::AlignVCenter | Qt::AlignRight, said, true);
			}
		}
	}

	p->setPen(grid);
	for (int c = 0; c <= columns + static_cast<int>(extras_.size()); c++) p->drawLine(columnX(c), 0, columnX(c), h);
	p->drawLine(0, hs, w, hs);

	lastPaintMs_ = timer.nsecsElapsed() / 1.0e6;
	Q_EMIT painted();
}

void PianoRollItem::mousePressEvent(QMouseEvent *e)
{
	qint64 row = 0;
	int column = 0;
	/* where it is in the roll as laid out upright */
	const QPointF at = logical(e->position());
	const int hs = headerSize();
	/* the right button: a column's menu on its name, else for whoever is under the roll */
	if (e->button() == Qt::RightButton)
	{
		if (rows_ != nullptr && at.y() < hs && cellAt(QPointF(at.x(), hs), row, column))
		{
			Q_EMIT columnMenuRequested(column, QString::fromStdString(rows_->rows().columnName(column)), rows_->rows().columnEmphasized(column),
				e->position().x(), e->position().y());
			e->accept();
			return;
		}
		if (rows_ != nullptr && at.y() >= hs)
		{
			Q_EMIT rowMenuRequested(rowOnLine(static_cast<qint64>((at.y() - hs) / rowHeight_)));
			e->accept();
			return;
		}
		e->ignore();
		return;
	}
	if (at.x() < frameColumnWidth() && at.y() >= hs && rows_ != nullptr)
	{
		row = rowOnLine(static_cast<qint64>((at.y() - hs) / rowHeight_));
		if (row >= rowCount())
		{
			e->accept();
			return;
		}
		if (at.x() < kCursorWidth)
			Q_EMIT frameClicked(row);
		else
		{
			/* Shift extends from where picking began; a drag picks what it passes */
			if (!(e->modifiers() & Qt::ShiftModifier) || selectAnchor_ < 0) selectAnchor_ = row;
			selecting_ = true;
			Q_EMIT selectionRequested(std::min(selectAnchor_, row), std::max(selectAnchor_, row));
		}
		e->accept();
		return;
	}
	if (!cellAt(at, row, column))
	{
		e->ignore();
		return;
	}
	dragColumn_ = column;
	dragRow_ = row;
	Q_EMIT cellPressed(row, column, (e->modifiers() & Qt::AltModifier) != 0);
	e->accept();
}

void PianoRollItem::mouseMoveEvent(QMouseEvent *e)
{
	const QPointF where = logical(e->position());
	const int hs = headerSize();
	if (selecting_ && rows_ != nullptr && where.y() >= hs)
	{
		const qint64 row = std::min(rowCount() - 1, rowOnLine(static_cast<qint64>((where.y() - hs) / rowHeight_)));
		if (row >= 0) Q_EMIT selectionRequested(std::min(selectAnchor_, row), std::max(selectAnchor_, row));
		return;
	}
	if (dragColumn_ < 0) return;
	/* a drag stays in the column it began in, whatever the pointer's x */
	const QPointF at(columnX(dragColumn_) + 1, where.y());
	qint64 row = 0;
	int column = 0;
	if (cellAt(at, row, column) && row != dragRow_)
	{
		/* every row passed over, even when the pointer moved fast */
		const qint64 step = row > dragRow_ ? 1 : -1;
		for (qint64 r = dragRow_ + step; r != row + step; r += step) Q_EMIT cellDragged(r, dragColumn_);
		dragRow_ = row;
	}
}

void PianoRollItem::mouseReleaseEvent(QMouseEvent *)
{
	selecting_ = false;
	dragColumn_ = -1;
	dragRow_ = -1;
}

void PianoRollItem::wheelEvent(QWheelEvent *e)
{
	const int notches = e->angleDelta().y() / 120;
	if (notches != 0) Q_EMIT topRowRequested(std::max<qint64>(0, top_ - notches * std::max(1, wheelRows_)));
	e->accept();
}

} // namespace chimera::qt
