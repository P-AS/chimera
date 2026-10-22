#include "piano_roll_item.hpp"

#include <QElapsedTimer>
#include <QFontMetrics>
#include <QPainter>

#include <algorithm>

#include "qml_view_model.hpp"

namespace chimera::qt {

PianoRollItem::PianoRollItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
	setOpaquePainting(true);
	setAntialiasing(false);
	setAcceptedMouseButtons(Qt::LeftButton);
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

void PianoRollItem::rowsTouched(qint64 first, qint64 last)
{
	if (last >= top_ && first <= top_ + visibleRows()) update();
}

int PianoRollItem::frameColumnWidth() const
{
	return QFontMetrics(QFont()).horizontalAdvance(QStringLiteral("00000000")) + 8;
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
	column = static_cast<int>((p.x() - fx) / columnWidth_);
	row = top_ + static_cast<qint64>((p.y() - rowHeight_) / rowHeight_);
	return column < rows_->rows().columnCount() && row < rows_->rows().rowCount();
}

void PianoRollItem::paint(QPainter *p)
{
	QElapsedTimer timer;
	timer.start();

	const QColor base(0xff, 0xff, 0xff), stripe(0xf3, 0xf5, 0xf8), grid(0xd8, 0xdc, 0xe2),
		header(0xe8, 0xeb, 0xf0), text(0x20, 0x24, 0x2a), dim(0x80, 0x88, 0x92),
		lit(0x2f, 0x6f, 0xd6), now(0xc9, 0xf2, 0xc7), future(0xff, 0xff, 0xff);
	const int w = static_cast<int>(width()), h = static_cast<int>(height());
	p->fillRect(0, 0, w, h, base);
	if (rows_ == nullptr) return;

	vm::RowModel &rows = rows_->rows();
	const int fx = frameColumnWidth();
	const int columns = rows.columnCount();
	const qint64 count = rows.rowCount();
	const int shown = visibleRows() + 1;

	/* the header: each button's name without the "P1 " a player's buttons
	 * share, or its letter in a movie when the name does not fit */
	p->fillRect(0, 0, w, rowHeight_, header);
	p->setPen(text);
	p->drawText(QRect(4, 0, fx - 8, rowHeight_), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Frame"));
	for (int c = 0; c < columns; c++)
	{
		QString name = QString::fromStdString(rows.columnName(c));
		const qsizetype space = name.indexOf(' ');
		if (space > 0 && space < 4) name = name.mid(space + 1);
		if (QFontMetrics(p->font()).horizontalAdvance(name) > columnWidth_ - 2)
			name = QString::fromStdString(rows.columnShortName(c));
		const QRect cell(fx + c * columnWidth_, 0, columnWidth_, rowHeight_);
		p->drawText(cell, Qt::AlignCenter, QFontMetrics(p->font()).elidedText(name, Qt::ElideRight, columnWidth_ - 2));
	}

	for (int i = 0; i < shown; i++)
	{
		const qint64 row = top_ + i;
		if (row >= count) break;
		const int y = rowHeight_ * (i + 1);
		const QColor back = row == current_ ? now : (row > current_ ? future : stripe);
		p->fillRect(0, y, w, rowHeight_, (row % 2 && row != current_) ? back.darker(103) : back);
		p->setPen(row < current_ ? dim : text);
		p->drawText(QRect(4, y, fx - 8, rowHeight_), Qt::AlignVCenter | Qt::AlignRight, QString::number(row));
		for (int c = 0; c < columns; c++)
		{
			bool held = false;
			vm::fromValue(rows.cell(row, c), held);
			if (held) p->fillRect(fx + c * columnWidth_ + 2, y + 2, columnWidth_ - 4, rowHeight_ - 4, lit);
		}
	}

	p->setPen(grid);
	for (int c = 0; c <= columns; c++) p->drawLine(fx + c * columnWidth_, 0, fx + c * columnWidth_, h);
	p->drawLine(0, rowHeight_, w, rowHeight_);

	lastPaintMs_ = timer.nsecsElapsed() / 1.0e6;
	Q_EMIT painted();
}

void PianoRollItem::mousePressEvent(QMouseEvent *e)
{
	qint64 row = 0;
	int column = 0;
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
	if (dragColumn_ < 0) return;
	const int fx = frameColumnWidth();
	/* a drag stays in the column it began in, whatever the pointer's x */
	const QPointF at(fx + dragColumn_ * columnWidth_ + 1, e->position().y());
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
