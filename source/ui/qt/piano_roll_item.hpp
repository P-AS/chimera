#pragma once
/* The piano roll's grid: it draws only the rows in view, asking the rows
 * model for those cells alone, so a million frames scroll as fast as forty.
 * It edits nothing itself: a press or a drag is a signal, and the window
 * turns it into a command of the piano roll's view model. */

#include <QPointer>
#include <QQuickPaintedItem>

namespace chimera::qt {

class QmlRows;

class PianoRollItem : public QQuickPaintedItem
{
	Q_OBJECT
	Q_PROPERTY(QObject *rows READ rows WRITE setRows NOTIFY rowsChanged)
	Q_PROPERTY(qint64 topRow READ topRow WRITE setTopRow NOTIFY topRowChanged)
	Q_PROPERTY(qint64 currentRow READ currentRow WRITE setCurrentRow NOTIFY currentRowChanged)
	Q_PROPERTY(qint64 rowCount READ rowCount NOTIFY rowCountChanged)
	Q_PROPERTY(int visibleRows READ visibleRows NOTIFY visibleRowsChanged)
	Q_PROPERTY(int rowHeight READ rowHeight CONSTANT)
	/* how long the last paint took, in milliseconds, for measuring */
	Q_PROPERTY(double lastPaintMs READ lastPaintMs NOTIFY painted)

public:
	explicit PianoRollItem(QQuickItem *parent = nullptr);

	QObject *rows() const;
	void setRows(QObject *rows);
	qint64 topRow() const { return top_; }
	void setTopRow(qint64 row);
	qint64 currentRow() const { return current_; }
	void setCurrentRow(qint64 row);
	qint64 rowCount() const;
	int visibleRows() const;
	int rowHeight() const { return rowHeight_; }
	double lastPaintMs() const { return lastPaintMs_; }

	void paint(QPainter *painter) override;

Q_SIGNALS:
	void rowsChanged();
	void topRowChanged();
	void currentRowChanged();
	void rowCountChanged();
	void visibleRowsChanged();
	void painted();
	void cellPressed(qint64 row, int column);
	void cellDragged(qint64 row, int column);
	/* a press on a row's frame number */
	void frameClicked(qint64 row);
	/* the wheel asks for another first row; whoever owns topRow decides */
	void topRowRequested(qint64 row);

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void geometryChange(const QRectF &now, const QRectF &before) override;

private:
	bool cellAt(QPointF p, qint64 &row, int &column) const;
	int frameColumnWidth() const;
	void rowsTouched(qint64 first, qint64 last);

	QPointer<QmlRows> rows_;
	std::vector<QMetaObject::Connection> watches_;
	qint64 top_ = 0, current_ = 0;
	int rowHeight_ = 18, columnWidth_ = 26;
	double lastPaintMs_ = 0;
	int dragColumn_ = -1;
	qint64 dragRow_ = -1;
};

} // namespace chimera::qt
