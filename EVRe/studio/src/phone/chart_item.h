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
 *   ChartLines  the legend's model: a row per line (its id, name, unit, colour, shown, and the latest value in view
 *               with its unit, as the desktop's legend writes it: ChartData::number), the values at most LEGEND_MS
 *               apart (a number that changes at every frame cannot be read).
 *
 * Fed from C++: addLine(id, colour, name, unit) with a colour of its own, or addThemedLine(id, index, ...) with the
 * theme's series colour `index` (colours, recoloured with the theme), a fast line with its stream's store and channel
 * (binned by ChartData::binFast through binViewSeries); appendSamples(id, times, values), removeLine(id), clear() (the
 * samples, the lines kept). A hidden line (setLineShown) is neither drawn nor counted in the Y range. Times in seconds
 * of the chart's clock (setClock; without one the newest sample's time is now). Y: Auto spans the values in view
 * (growing at once, shrinking gently, as the desktop's), or a manual range (setYRange). A pinch zooms the time
 * (zoomTime), a drag pans it and holds (panPixels), a double tap goes live again (ChartScreen.qml). Time runs left to
 * right in any language. */
#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QElapsedTimer>
#include <QQuickItem>
#include <QRectF>
#include <QVariantList>
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

class ChartLines : public QAbstractListModel {
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("the chart's own")

public:
	enum Role { IdRole = Qt::UserRole + 1, NameRole, UnitRole, ColourRole, ValueRole, ShownRole, FastRole };
	struct Entry {
		int id = 0;
		QString name, unit, value;
		QColor colour;
		bool shown = true, fast = false;
		bool operator==(const Entry &o) const {
			return id == o.id && name == o.name && unit == o.unit && value == o.value && colour == o.colour
					&& shown == o.shown && fast == o.fast;
		}
	};

	explicit ChartLines(QObject *parent = nullptr) : QAbstractListModel(parent) {}
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	const QVector<Entry> &entries() const { return entries_; }
	/* the new rows: the same lines in the same order tell their changes by dataChanged (the chips stay), others reset */
	void set(const QVector<Entry> &entries);

private:
	QVector<Entry> entries_;
};

class ChartItem : public QQuickItem {
	Q_OBJECT
	QML_ELEMENT
	Q_PROPERTY(double windowSeconds READ windowSeconds WRITE setWindowSeconds NOTIFY windowSecondsChanged)
	Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged)
	Q_PROPERTY(bool yAuto READ yAuto WRITE setYAuto NOTIFY yAutoChanged)
	Q_PROPERTY(double yLow READ yLow NOTIFY yRangeChanged)
	Q_PROPERTY(double yHigh READ yHigh NOTIFY yRangeChanged)
	Q_PROPERTY(QVariantList colours READ colours WRITE setColours NOTIFY colorsChanged)
	Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY colorsChanged)
	Q_PROPERTY(QColor frameColor READ frameColor WRITE setFrameColor NOTIFY colorsChanged)
	Q_PROPERTY(QRectF plotRect READ plotRect NOTIFY plotRectChanged)
	Q_PROPERTY(int lineCount READ lineCount NOTIFY linesChanged)
	Q_PROPERTY(ChartTicks *timeTicks READ timeTicks CONSTANT)
	Q_PROPERTY(ChartTicks *valueTicks READ valueTicks CONSTANT)
	Q_PROPERTY(ChartLines *legend READ legend CONSTANT)

public:
	static constexpr double MIN_WINDOW = 0.01;  /* seconds: the shortest view a pinch gives */
	static constexpr double MAX_WINDOW = 300;   /* the longest, the window choice's 5 min */
	static constexpr double LINE_WIDTH = 1.5;   /* dp, the line through the columns */
	/* the labels' room around the plot, dp: the value labels left of it, the time labels under it */
	static constexpr double LEFT_PAD = 52, RIGHT_PAD = 10, TOP_PAD = 8, BOTTOM_PAD = 24;
	static constexpr int MAX_TICKS_X = 6, MAX_TICKS_Y = 6;
	static constexpr int LEGEND_MS = 250; /* the legend's values at most this often (4 a second) */

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
	QVariantList colours() const { return colours_; }
	void setColours(const QVariantList &colours); /* the theme's series colours, for the lines added by index */
	QRectF plotRect() const { return plot_; }
	int lineCount() const { return int(lines_.size()); }
	ChartTicks *timeTicks() const { return timeTicks_; }
	ChartTicks *valueTicks() const { return valueTicks_; }
	ChartLines *legend() const { return legend_; }

	/* the lines, for the session's feed (PhoneSession) and the demo: a line by its id, its samples in rising time (an
	 * older one than the line's newest is left out); clear() empties every line, keeping them (a fast line's store
	 * too); removeAll() takes them all off */
	void addLine(int id, const QColor &colour, const QString &name = QString(), const QString &unit = QString());
	void addThemedLine(int id, int colourIndex, const QString &name, const QString &unit,
			std::shared_ptr<fast::Store> fast = nullptr, int channel = 0);
	void removeLine(int id);
	void removeAll();
	bool hasLine(int id) const { return find(id) != nullptr; }
	void appendSamples(int id, const QVector<double> &times, const QVector<double> &values);
	Q_INVOKABLE void clear();
	/* a line drawn or not (the legend's chip): hidden, it is not drawn and not in the Y range */
	Q_INVOKABLE void setLineShown(int id, bool shown);
	Q_INVOKABLE bool lineShown(int id) const;
	/* Y held between lo and hi (Auto off); false when hi is not above lo */
	Q_INVOKABLE bool setYRange(double lo, double hi);
	/* a fast line's records came into its store (the session's): the next frame draws them */
	void storeChanged() { polish(); }
	/* now, in seconds of the samples' times; not set: the newest sample's time */
	void setClock(std::function<double()> clock);
	/* the samples kept per line, seconds (the longest window and a margin) */
	void setMemorySeconds(double seconds) { memory_ = seconds; }
	double memorySeconds() const { return memory_; }

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
	const ChartData::Series *series(int id) const; /* the line's samples (null: no such line) */
	QColor lineColour(int id) const;
	quint64 generation() const { return generation_; }
	qint64 polishes() const { return polishes_; }
	double polishSeconds() const { return polishSeconds_; } /* the time updatePolish took, all its frames */
	qint64 polledColumnsBinned() const { return polledBinned_; }

signals:
	void windowSecondsChanged();
	void liveChanged();
	void yAutoChanged();
	void yRangeChanged();
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
		int colourIndex = -1; /* the theme's series colour; -1: its own */
		bool shown = true;
		QString value;        /* the legend's text */
		QVector<float> vertices; /* x, y pairs of this frame's triangles (updatePolish), read by updatePaintNode */
	};
	Line *find(int id);
	const Line *find(int id) const;
	double now() const;
	void trim(ChartData::Series &s);
	void updateYRange();
	void makeTicks(QVector<float> &grid);
	void frameTick(); /* after each frame while live and shown: the next one polished */
	void addLine(std::unique_ptr<Line> line);
	QColor colourOf(const Line &line) const;
	void updateLegend(bool values);

	std::vector<std::unique_ptr<Line>> lines_;
	std::function<double()> clock_;
	double window_ = 60, viewEnd_ = 0, memory_ = MAX_WINDOW * 1.1 + 5;
	bool live_ = true, yAuto_ = true;
	double yLo_ = 0, yHi_ = 1;
	bool yHeld_ = false; /* a range has been taken (yAuto) */
	QColor gridColor_ = Qt::gray, frameColor_ = Qt::darkGray;
	QVariantList colours_;
	QElapsedTimer legendClock_;
	QRectF plot_;
	ChartData::Axes axes_;
	quint64 generation_ = 1;
	std::atomic<qint64> polledBinned_{ 0 }, fastBinned_{ 0 };
	qint64 polishes_ = 0;
	double polishSeconds_ = 0;
	QVector<float> grid_, frame_; /* the grid's triangles and the plot's edge, as a line's */
	bool linesDirty_ = true, colorsDirty_ = true;
	ChartTicks *timeTicks_ = nullptr, *valueTicks_ = nullptr;
	ChartLines *legend_ = nullptr;
	QMetaObject::Connection frameConnection_;
};
