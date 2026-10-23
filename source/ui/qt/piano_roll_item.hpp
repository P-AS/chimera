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
	/* the frames picked, drawn on the frame numbers */
	Q_PROPERTY(qint64 selectFirst READ selectFirst WRITE setSelectFirst NOTIFY selectionChanged)
	Q_PROPERTY(qint64 selectLast READ selectLast WRITE setSelectLast NOTIFY selectionChanged)
	/* the analog cell picked for editing, outlined (-1: none) */
	Q_PROPERTY(qint64 pickedRow MEMBER pickedRow_ NOTIFY pickedChanged)
	Q_PROPERTY(int pickedColumn MEMBER pickedColumn_ NOTIFY pickedChanged)
	/* the rows a notch of the wheel scrolls */
	Q_PROPERTY(int wheelRows MEMBER wheelRows_)
	Q_PROPERTY(qint64 rowCount READ rowCount NOTIFY rowCountChanged)
	Q_PROPERTY(int visibleRows READ visibleRows NOTIFY visibleRowsChanged)
	Q_PROPERTY(int rowHeight READ rowHeight NOTIFY visibleRowsChanged)
	/* TAStudio's appearance: states and markers by colour and by icon, the
	 * roll's own colours over the theme's (JSON: a theme role each), and
	 * its font ({"family", "pointSize", "bold", "italic"}; "" the window's) */
	Q_PROPERTY(bool denoteStatesColor MEMBER denoteStatesColor_ NOTIFY lookChanged)
	Q_PROPERTY(bool denoteStatesIcons MEMBER denoteStatesIcons_ NOTIFY lookChanged)
	Q_PROPERTY(bool denoteMarkersColor MEMBER denoteMarkersColor_ NOTIFY lookChanged)
	Q_PROPERTY(bool denoteMarkersIcons MEMBER denoteMarkersIcons_ NOTIFY lookChanged)
	Q_PROPERTY(QString colors READ colors WRITE setColors NOTIFY lookChanged)
	Q_PROPERTY(QString rollFont READ rollFont WRITE setRollFont NOTIFY lookChanged)
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
	qint64 selectFirst() const { return selectFirst_; }
	qint64 selectLast() const { return selectLast_; }
	void setSelectFirst(qint64 row);
	void setSelectLast(qint64 row);
	qint64 rowCount() const;
	int visibleRows() const;
	int rowHeight() const { return rowHeight_; }
	double lastPaintMs() const { return lastPaintMs_; }

	void paint(QPainter *painter) override;

	QString colors() const { return colors_; }
	void setColors(const QString &json);
	QString rollFont() const { return rollFont_; }
	void setRollFont(const QString &font);

Q_SIGNALS:
	void lookChanged();
	void rowsChanged();
	void topRowChanged();
	void currentRowChanged();
	void rowCountChanged();
	void visibleRowsChanged();
	void painted();
	/* alt: Alt was held (TAStudio paints a column's pattern then) */
	void cellPressed(qint64 row, int column, bool alt);
	/* the right button on a column's name: its menu */
	void columnMenuRequested(int column, const QString &name, bool autoHeld, qreal x, qreal y);
	void cellDragged(qint64 row, int column);
	/* a press on a row's frame number */
	void frameClicked(qint64 row);
	/* the frame numbers pressed or dragged over, or Shift-pressed */
	void selectionRequested(qint64 first, qint64 last);
	void selectionChanged();
	void pickedChanged();
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
	qint64 selectFirst_ = -1, selectLast_ = -1, selectAnchor_ = -1;
	bool selecting_ = false;
	qint64 pickedRow_ = -1;
	int pickedColumn_ = -1;
	int wheelRows_ = 6;
	bool denoteStatesColor_ = true, denoteStatesIcons_ = false, denoteMarkersColor_ = true, denoteMarkersIcons_ = false;
	QString colors_, rollFont_;
	QFont font_;
	int rowHeight_ = 18, columnWidth_ = 26, numberWidth_ = 48;
	/* where a column starts and how wide it is: an analog control's column
	 * shows a number, and is wider */
	int columnX(int column) const;
	int columnW(int column) const;
	double lastPaintMs_ = 0;
	int dragColumn_ = -1;
	qint64 dragRow_ = -1;
};

} // namespace chimera::qt
