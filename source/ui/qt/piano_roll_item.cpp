#include "piano_roll_item.hpp"

#include <QElapsedTimer>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPalette>
#include <QPainter>
#include <QPen>

#include <algorithm>

#include "qml_view_model.hpp"
#include "theme_palette.hpp"

namespace chimera::qt {

PianoRollItem::PianoRollItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
	setOpaquePainting(true);
	setAntialiasing(false);
	setAcceptedMouseButtons(Qt::LeftButton);
	connect(this, &PianoRollItem::pickedChanged, this, [this] { update(); });
}

int PianoRollItem::columnW(int column) const
{
	if (rows_ == nullptr) return columnWidth_;
	/* a hidden column takes no room */
	if (rows_->rows().columnHidden(column)) return 0;
	return rows_->rows().columnIsNumber(column) ? numberWidth_ : columnWidth_;
}

int PianoRollItem::columnX(int column) const
{
	int x = frameColumnWidth();
	for (int c = 0; c < column; c++) x += columnW(c);
	return x;
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
	return std::max(0, static_cast<int>(height()) / rowHeight_ - 1); /* less the header */
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
	const qint64 last = top_ + visibleRows();
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
	if (last >= top_ && first <= top_ + visibleRows()) update();
}

/* TAStudio's two columns before the buttons: a narrow one a press in moves
 * the machine to its frame, then the frame numbers, which pick frames */
static constexpr int kCursorWidth = 14;

int PianoRollItem::frameColumnWidth() const
{
	return kCursorWidth + QFontMetrics(QFont()).horizontalAdvance(QStringLiteral("00000000")) + 8;
}

void PianoRollItem::geometryChange(const QRectF &now, const QRectF &before)
{
	QQuickPaintedItem::geometryChange(now, before);
	if (now.height() != before.height()) Q_EMIT visibleRowsChanged();
}

bool PianoRollItem::cellAt(QPointF p, qint64 &row, int &column) const
{
	if (rows_ == nullptr) return false;
	const int fx = frameColumnWidth();
	if (p.x() < fx || p.y() < rowHeight_) return false;
	const int columns = rows_->rows().columnCount();
	column = -1;
	for (int c = 0, x = fx; c < columns; x += columnW(c), c++)
		if (p.x() >= x && p.x() < x + columnW(c)) column = c;
	row = top_ + static_cast<qint64>((p.y() - rowHeight_) / rowHeight_);
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
	themed(picked, "RollSelection");
	themed(pickedText, "RollSelectionText");
	const int w = static_cast<int>(width()), h = static_cast<int>(height());
	p->fillRect(0, 0, w, h, plain);
	if (rows_ == nullptr) return;

	vm::RowModel &rows = rows_->rows();
	const int fx = frameColumnWidth();
	const int columns = rows.columnCount();
	const qint64 count = rows.rowCount();
	const int shown = visibleRows() + 1;

	/* the header: each button's name without the "P1 " a player's buttons
	 * share, or its letter in a movie when the name does not fit */
	p->fillRect(0, 0, w, rowHeight_, header);
	p->setPen(headerText);
	p->drawText(QRect(kCursorWidth + 4, 0, fx - kCursorWidth - 8, rowHeight_), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Frame"));
	for (int c = 0; c < columns; c++)
	{
		if (columnW(c) == 0) continue; /* hidden */
		QString name = QString::fromStdString(rows.columnName(c));
		const qsizetype space = name.indexOf(' ');
		if (space > 0 && space < 4) name = name.mid(space + 1);
		if (QFontMetrics(p->font()).horizontalAdvance(name) > columnW(c) - 2)
			name = QString::fromStdString(rows.columnShortName(c));
		const QRect cell(columnX(c), 0, columnW(c), rowHeight_);
		p->drawText(cell, Qt::AlignCenter, QFontMetrics(p->font()).elidedText(name, Qt::ElideRight, columnW(c) - 2));
	}

	for (int i = 0; i < shown; i++)
	{
		const qint64 row = top_ + i;
		if (row >= count) break;
		const int y = rowHeight_ * (i + 1);
		const uint32_t flags = rows.rowFlags(row);
		const QColor back = row == current_ ? now : (flags & 2) ? ((flags & 1) ? lag : green) : plain;
		p->fillRect(0, y, w, rowHeight_, (row % 2 && row != current_) ? (dark ? back.lighter(112) : back.darker(103)) : back);
		/* a marker: its frame number on the accent, its text after the buttons */
		if (flags & 4)
		{
			p->fillRect(0, y, fx, rowHeight_, marker);
			p->setPen(text);
			p->drawText(QRect(columnX(columns) + 6, y, w - columnX(columns) - 8, rowHeight_),
				Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(rows.rowNote(row)));
		}
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
		p->drawText(QRect(kCursorWidth + 4, y, fx - kCursorWidth - 8, rowHeight_), Qt::AlignVCenter | Qt::AlignRight, QString::number(row));
		for (int c = 0; c < columns; c++)
		{
			const int cx = columnX(c), cw = columnW(c);
			if (cw == 0) continue; /* hidden */
			if (rows.columnIsNumber(c))
			{
				int64_t value = 0;
				if (vm::fromValue(rows.cell(row, c), value))
				{
					p->setPen(text);
					p->drawText(QRect(cx + 2, y, cw - 6, rowHeight_), Qt::AlignVCenter | Qt::AlignRight, QString::number(value));
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
	}

	p->setPen(grid);
	for (int c = 0; c <= columns; c++) p->drawLine(columnX(c), 0, columnX(c), h);
	p->drawLine(0, rowHeight_, w, rowHeight_);

	lastPaintMs_ = timer.nsecsElapsed() / 1.0e6;
	Q_EMIT painted();
}

void PianoRollItem::mousePressEvent(QMouseEvent *e)
{
	qint64 row = 0;
	int column = 0;
	if (e->position().x() < frameColumnWidth() && e->position().y() >= rowHeight_ && rows_ != nullptr)
	{
		row = top_ + static_cast<qint64>((e->position().y() - rowHeight_) / rowHeight_);
		if (row >= rowCount())
		{
			e->accept();
			return;
		}
		if (e->position().x() < kCursorWidth)
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
	if (!cellAt(e->position(), row, column))
	{
		e->ignore();
		return;
	}
	dragColumn_ = column;
	dragRow_ = row;
	Q_EMIT cellPressed(row, column);
	e->accept();
}

void PianoRollItem::mouseMoveEvent(QMouseEvent *e)
{
	if (selecting_ && rows_ != nullptr && e->position().y() >= rowHeight_)
	{
		const qint64 row = std::min(rowCount() - 1, top_ + static_cast<qint64>((e->position().y() - rowHeight_) / rowHeight_));
		if (row >= 0) Q_EMIT selectionRequested(std::min(selectAnchor_, row), std::max(selectAnchor_, row));
		return;
	}
	if (dragColumn_ < 0) return;
	/* a drag stays in the column it began in, whatever the pointer's x */
	const QPointF at(columnX(dragColumn_) + 1, e->position().y());
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
	if (notches != 0) Q_EMIT topRowRequested(std::max<qint64>(0, top_ - notches * 3));
	e->accept();
}

} // namespace chimera::qt
