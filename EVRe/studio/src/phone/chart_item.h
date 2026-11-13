/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's chart (android_plan 10.3, Q-1; STUDIO.md 25.6): a QQuickItem that the GPU draws from nodes of its own.
 *
 *   ChartItem   the lines (per line a ChartData::Series and a colour), the view (windowSeconds, live or held, the Y
 *               range automatic or held) and the plot inside the item, with the labels' room left of it and under it.
 *               updatePolish(), on the window's thread, bins each line for the view with the desktop's binning
 *               (ChartData::binViewSeries, as ChartView::binView does: a live view bins only the samples since the
 *               frame before), maps the bins through ChartData::Axes and makes each line's triangles: a column's
 *               min..max a quad (toPolyline's band), the line through the first and last values quads 1.5 dp wide
 *               (a GPU's line width is not portable: they draw 1 px lines). updatePaintNode(), on the render thread
 *               while the window's thread waits, copies them into one QSGGeometryNode per line (flat colour, clipped
 *               to the plot) and one for the grid, at 1-2-5 steps in time and value.
 *   ChartTicks  a ticks model, the labels of one axis (`at`: x or y in the item, `label`): QML draws them as Text
 *               over the item, so only a label that changes is laid out again.
 *
 * Fed from C++: addLine(id, colour), appendSamples(id, times, values), clear(). Times in seconds of the chart's clock
 * (setClock; without one the newest sample's time is now). A pinch zooms the time (zoomTime), a drag pans it and holds
 * (panPixels), a double tap goes live again (ChartScreen.qml). Time runs left to right in any language. */
#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QElapsedTimer>
#include <QQuickItem>
#include <QRectF>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "ui/chart_data.h"

class ChartTicks : public QAbstractListModel {
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("the chart's own")

public:
	enum Role { AtRole = Qt::UserRole + 1, TextRole };
	struct Tick {
		double at = 0; /* x (the time axis) or y (the value axis) in the item, logical pixels */
		QString text;
		bool operator==(const Tick &other) const { return at == other.at && text == other.text; }
	};

	explicit ChartTicks(QObject *parent = nullptr) : QAbstractListModel(parent) {}
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	const QVector<Tick> &ticks() const { return ticks_; }
	/* the new ticks: rows added or removed at the end, a changed row told by dataChanged (a Repeater keeps its Texts) */
	void set(const QVector<Tick> &ticks);

private:
	QVector<Tick> ticks_;
};

class ChartItem : public QQuickItem {
	Q_OBJECT
	QML_ELEMENT
	Q_PROPERTY(double windowSeconds READ windowSeconds WRITE setWindowSeconds NOTIFY windowSecondsChanged)
	Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged)
	Q_PROPERTY(bool yAuto READ yAuto WRITE setYAuto NOTIFY yAutoChanged)
	Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY colorsChanged)
	Q_PROPERTY(QColor frameColor READ frameColor WRITE setFrameColor NOTIFY colorsChanged)
	Q_PROPERTY(QRectF plotRect READ plotRect NOTIFY plotRectChanged)
	Q_PROPERTY(int lineCount READ lineCount NOTIFY linesChanged)
	Q_PROPERTY(ChartTicks *timeTicks READ timeTicks CONSTANT)
	Q_PROPERTY(ChartTicks *valueTicks READ valueTicks CONSTANT)

public:
	static constexpr double MIN_WINDOW = 0.01;  /* seconds: the shortest view a pinch gives */
	static constexpr double MAX_WINDOW = 300;   /* the longest, the window choice's 5 min */
	static constexpr double LINE_WIDTH = 1.5;   /* dp, the line through the columns */
	/* the labels' room around the plot, dp: the value labels left of it, the time labels under it */
	static constexpr double LEFT_PAD = 52, RIGHT_PAD = 10, TOP_PAD = 8, BOTTOM_PAD = 24;
	static constexpr int MAX_TICKS_X = 6, MAX_TICKS_Y = 6;

	explicit ChartItem(QQuickItem *parent = nullptr);
	~ChartItem() override;

	double windowSeconds() const { return window_; }
	void setWindowSeconds(double seconds);
	bool live() const { return live_; }
	void setLive(bool live);
	bool yAuto() const { return yAuto_; }
	void setYAuto(bool on);
	QColor gridColor() const { return gridColor_; }
	void setGridColor(const QColor &color);
	QColor frameColor() const { return frameColor_; }
	void setFrameColor(const QColor &color);
	QRectF plotRect() const { return plot_; }
	int lineCount() const { return int(lines_.size()); }
	ChartTicks *timeTicks() const { return timeTicks_; }
	ChartTicks *valueTicks() const { return valueTicks_; }

	/* the lines, for B.3b's feed (PhoneSession) and the demo: a line by its id, its samples in rising time (an older
	 * one than the line's newest is left out); clear() empties every line, keeping them */
	void addLine(int id, const QColor &colour);
	void removeLine(int id);
	void appendSamples(int id, const QVector<double> &times, const QVector<double> &values);
	void clear();
	/* now, in seconds of the samples' times; not set: the newest sample's time */
	void setClock(std::function<double()> clock);
	/* the samples kept per line, seconds (the longest window and a margin) */
	void setMemorySeconds(double seconds) { memory_ = seconds; }

	/* the touch, from ChartScreen.qml: a pinch's factor (> 1 zooms in: a shorter window, the same end), a drag by dx
	 * logical pixels (to the right: older times), which holds the view */
	Q_INVOKABLE void zoomTime(double factor);
	Q_INVOKABLE void panPixels(double dx);

	/* tests: the view as last drawn, a line's bins then, and what the frames binned */
	double viewStart() const { return axes_.t0; }
	double viewEnd() const { return axes_.t1; }
	double columns() const { return axes_.columns; }
	double yLow() const { return axes_.lo; }
	double yHigh() const { return axes_.hi; }
	QVector<ChartData::Bin> bins(int id) const;
	quint64 generation() const { return generation_; }
	qint64 polishes() const { return polishes_; }
	double polishSeconds() const { return polishSeconds_; } /* the time updatePolish took, all its frames */
	qint64 polledColumnsBinned() const { return polledBinned_; }

signals:
	void windowSecondsChanged();
	void liveChanged();
	void yAutoChanged();
	void colorsChanged();
	void plotRectChanged();
	void linesChanged();
	void framed(); /* a frame's lines were made (updatePolish): tests wait for it */

protected:
	void updatePolish() override;
	QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;
	void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
	void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
	struct Line { /* held by pointer: its BinnedLine points at its Series */
		int id = 0;
		ChartData::Series series;
		ChartData::BinnedLine binned;
		QVector<float> vertices; /* x, y pairs of this frame's triangles (updatePolish), read by updatePaintNode */
	};
	Line *find(int id);
	const Line *find(int id) const;
	double now() const;
	void trim(ChartData::Series &s);
	void updateYRange();
	void makeTicks(QVector<float> &grid);
	void frameTick(); /* after each frame while live and shown: the next one polished */

	std::vector<std::unique_ptr<Line>> lines_;
	std::function<double()> clock_;
	double window_ = 60, viewEnd_ = 0, memory_ = MAX_WINDOW * 1.1 + 5;
	bool live_ = true, yAuto_ = true;
	double yLo_ = 0, yHi_ = 1;
	bool yHeld_ = false; /* a range has been taken (yAuto) */
	QColor gridColor_ = Qt::gray, frameColor_ = Qt::darkGray;
	QRectF plot_;
	ChartData::Axes axes_;
	quint64 generation_ = 1;
	std::atomic<qint64> polledBinned_{ 0 }, fastBinned_{ 0 };
	qint64 polishes_ = 0;
	double polishSeconds_ = 0;
	QVector<float> grid_, frame_; /* the grid's triangles and the plot's edge, as a line's */
	bool linesDirty_ = true, colorsDirty_ = true;
	ChartTicks *timeTicks_ = nullptr, *valueTicks_ = nullptr;
	QMetaObject::Connection frameConnection_;
};
