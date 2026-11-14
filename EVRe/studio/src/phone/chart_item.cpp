/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's chart: see chart_item.h. */
#include "phone/chart_item.h"

#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGNode>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

/* ------------------------------------------------------------------ the ticks */

int ChartTicks::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(ticks_.size()); }

QVariant ChartTicks::data(const QModelIndex &index, int role) const {
	if (!index.isValid() || index.row() >= ticks_.size()) return {};
	const Tick &tick = ticks_[index.row()];
	if (role == AtRole) return tick.at;
	if (role == TextRole) return tick.text;
	return {};
}

QHash<int, QByteArray> ChartTicks::roleNames() const { return { { AtRole, "at" }, { TextRole, "label" } }; }

void ChartTicks::set(const QVector<Tick> &ticks) {
	const int had = int(ticks_.size()), has = int(ticks.size());
	if (has < had) {
		beginRemoveRows(QModelIndex(), has, had - 1);
		ticks_.resize(has);
		endRemoveRows();
	}
	/* the rows both have: one dataChanged over the span that changed */
	int first = -1, last = -1;
	for (int i = 0; i < std::min(had, has); ++i) {
		if (ticks_[i] == ticks[i]) continue;
		ticks_[i] = ticks[i];
		if (first < 0) first = i;
		last = i;
	}
	if (first >= 0) emit dataChanged(index(first), index(last), { AtRole, TextRole });
	if (has > had) {
		beginInsertRows(QModelIndex(), had, has - 1);
		ticks_ = ticks;
		endInsertRows();
	}
}

/* ------------------------------------------------------------------ the legend */

int ChartLines::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(entries_.size()); }

QVariant ChartLines::data(const QModelIndex &index, int role) const {
	if (!index.isValid() || index.row() >= entries_.size()) return {};
	const Entry &entry = entries_[index.row()];
	switch (role) {
	case IdRole: return entry.id;
	case NameRole: return entry.name;
	case UnitRole: return entry.unit;
	case ColourRole: return entry.colour;
	case ValueRole: return entry.value;
	case ShownRole: return entry.shown;
	case FastRole: return entry.fast;
	default: return {};
	}
}

QHash<int, QByteArray> ChartLines::roleNames() const {
	return { { IdRole, "lineId" }, { NameRole, "name" }, { UnitRole, "unit" }, { ColourRole, "colour" },
		{ ValueRole, "value" }, { ShownRole, "shown" }, { FastRole, "fast" } };
}

void ChartLines::set(const QVector<Entry> &entries) {
	bool sameLines = entries.size() == entries_.size();
	for (qsizetype i = 0; sameLines && i < entries.size(); ++i) sameLines = entries[i].id == entries_[i].id;
	if (!sameLines) {
		beginResetModel();
		entries_ = entries;
		endResetModel();
		return;
	}
	for (qsizetype i = 0; i < entries.size(); ++i) {
		if (entries[i] == entries_[i]) continue;
		entries_[i] = entries[i];
		emit dataChanged(index(int(i)), index(int(i)));
	}
}

/* ------------------------------------------------------------------ the cursors' readouts */

int CursorReadouts::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(rows_.size()); }

QVariant CursorReadouts::data(const QModelIndex &index, int role) const {
	if (!index.isValid() || index.row() >= rows_.size()) return {};
	const Row &row = rows_[index.row()];
	switch (role) {
	case IdRole: return row.id;
	case NameRole: return row.name;
	case ColourRole: return row.colour;
	case AtARole: return row.atA;
	case AtBRole: return row.atB;
	case DeltaRole: return row.delta;
	case MinRole: return row.min;
	case MaxRole: return row.max;
	case MeanRole: return row.mean;
	default: return {};
	}
}

QHash<int, QByteArray> CursorReadouts::roleNames() const {
	return { { IdRole, "lineId" }, { NameRole, "name" }, { ColourRole, "colour" }, { AtARole, "atA" },
		{ AtBRole, "atB" }, { DeltaRole, "delta" }, { MinRole, "min" }, { MaxRole, "max" }, { MeanRole, "mean" } };
}

void CursorReadouts::set(const QVector<Row> &rows) {
	bool same = rows.size() == rows_.size();
	for (qsizetype i = 0; same && i < rows.size(); ++i) same = rows[i].id == rows_[i].id;
	if (!same) {
		beginResetModel();
		rows_ = rows;
		endResetModel();
		return;
	}
	for (qsizetype i = 0; i < rows.size(); ++i) {
		if (rows[i] == rows_[i]) continue;
		rows_[i] = rows[i];
		emit dataChanged(index(int(i)), index(int(i)));
	}
}

/* ------------------------------------------------------------------ helpers */

namespace {

/* the step of 1, 2 or 5 times a power of ten nearest to `rough` (by ratio) */
double niceStep(double rough) {
	if (!(rough > 0) || !std::isfinite(rough)) return 1;
	const double power = std::pow(10.0, std::floor(std::log10(rough)));
	double best = power;
	for (const double m : { 1.0, 2.0, 5.0, 10.0 })
		if (std::fabs(std::log(m * power / rough)) < std::fabs(std::log(best / rough))) best = m * power;
	return best;
}

/* the decimals a step needs (0.2: 1, 0.05: 2, 5: 0) */
int decimalsFor(double step) { return std::clamp(int(std::ceil(-std::log10(step) - 1e-9)), 0, 9); }

/* a time of the chart's clock as m:ss (h:mm:ss past an hour), with the decimals a step under a second needs; Western
 * digits in any language (as formatValue writes numbers) */
QString timeLabel(double t, double step) {
	const int decimals = step < 1 ? decimalsFor(step) : 0;
	const double scale = std::pow(10.0, decimals);
	const double rounded = std::round(std::fabs(t) * scale) / scale;
	const qint64 whole = qint64(std::floor(rounded));
	const double fraction = rounded - double(whole);
	const qint64 h = whole / 3600, m = (whole / 60) % 60, s = whole % 60;
	QString text = h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
	                     : QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
	if (decimals > 0) text += QString::number(fraction, 'f', decimals).mid(1); /* ".25" */
	return t < 0 && rounded > 0 ? QLatin1Char('-') + text : text;
}

QString valueLabel(double v, double step) {
	if (std::fabs(v) < step * 1e-6) v = 0; /* no "-0" */
	if (std::fabs(v) >= 1e6 || (std::fabs(v) > 0 && std::fabs(v) < 1e-4)) return QString::number(v, 'g', 4);
	return QString::number(v, 'f', decimalsFor(step));
}

/* two triangles, a quad of the corners a, b, c, d (in order round it) */
void quad(QVector<float> &out, QPointF a, QPointF b, QPointF c, QPointF d) {
	for (const QPointF &p : { a, b, c, a, c, d }) {
		out << float(p.x()) << float(p.y());
	}
}

void rect(QVector<float> &out, double x0, double y0, double x1, double y1) {
	quad(out, QPointF(x0, y0), QPointF(x1, y0), QPointF(x1, y1), QPointF(x0, y1));
}

/* a segment from a to b as a quad `width` wide, its ends squared off half the width past a and b: the next segment's
 * end covers the corner, so the line has no notch where it turns */
void segment(QVector<float> &out, QPointF a, QPointF b, double width) {
	const double dx = b.x() - a.x(), dy = b.y() - a.y();
	const double length = std::hypot(dx, dy);
	const double half = width / 2;
	if (length < 1e-9) { /* a dot */
		rect(out, a.x() - half, a.y() - half, a.x() + half, a.y() + half);
		return;
	}
	const QPointF along(dx / length * half, dy / length * half), across(-dy / length * half, dx / length * half);
	const QPointF from = a - along, to = b + along;
	quad(out, from + across, to + across, to - across, from - across);
}

QSGGeometryNode *flatNode(const QColor &color) {
	auto *node = new QSGGeometryNode;
	auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
	geometry->setDrawingMode(QSGGeometry::DrawTriangles);
	geometry->setVertexDataPattern(QSGGeometry::StreamPattern);
	node->setGeometry(geometry);
	node->setFlag(QSGNode::OwnsGeometry);
	auto *material = new QSGFlatColorMaterial;
	material->setColor(color);
	node->setMaterial(material);
	node->setFlag(QSGNode::OwnsMaterial);
	return node;
}

/* a node's triangles from x, y pairs */
void fill(QSGGeometryNode *node, const QVector<float> &vertices) {
	QSGGeometry *geometry = node->geometry();
	const int count = int(vertices.size() / 2);
	if (geometry->vertexCount() != count) geometry->allocate(count);
	if (count > 0) std::memcpy(geometry->vertexData(), vertices.constData(), size_t(count) * 2 * sizeof(float));
	node->markDirty(QSGNode::DirtyGeometry);
}

void recolor(QSGGeometryNode *node, const QColor &color) {
	auto *material = static_cast<QSGFlatColorMaterial *>(node->material());
	if (material->color() == color) return;
	material->setColor(color);
	node->markDirty(QSGNode::DirtyMaterial);
}

} // namespace

/* ------------------------------------------------------------------ the item */

ChartItem::ChartItem(QQuickItem *parent) : QQuickItem(parent) {
	setFlag(ItemHasContents);
	timeTicks_ = new ChartTicks(this);
	valueTicks_ = new ChartTicks(this);
	legend_ = new ChartLines(this);
	readouts_ = new CursorReadouts(this);
}

/* ------------------------------------------------------------------ the cursors */

double ChartItem::cursorX(double t) const {
	if (!std::isfinite(t) || !(axes_.span > 0) || plot_.isEmpty() || t < axes_.t0 || t > axes_.t1) return NAN;
	return axes_.x(t);
}

QString ChartItem::cursorAText() const { return std::isfinite(cursorA_) ? timeLabel(cursorA_, 0.01) : QString(); }
QString ChartItem::cursorBText() const { return std::isfinite(cursorB_) ? timeLabel(cursorB_, 0.01) : QString(); }

QString ChartItem::cursorSpanText() const {
	if (!cursorsOn()) return QString();
	return ChartData::number(cursorB_ - cursorA_) + QStringLiteral(" s");
}

/* where a line has samples in [t0, t1]: its first and last there (a fast line's whole records only: before them its
 * summaries have no values at a time); false when it has none */
static bool samplesInView(const ChartData::Series &s, double t0, double t1, double &first, double &last) {
	if (s.fast) {
		if (!s.fast->hasTime()) return false;
		const qsizetype k0 = std::max(s.fast->lowerBound(t0), s.fast->recordsFrom()), k1 = s.fast->upperBound(t1) - 1;
		if (k0 > k1) return false;
		first = s.fast->timeAt(k0);
		last = s.fast->timeAt(k1);
		return true;
	}
	const qsizetype k0 = std::lower_bound(s.times.begin(), s.times.end(), t0) - s.times.begin();
	const qsizetype k1 = std::upper_bound(s.times.begin(), s.times.end(), t1) - s.times.begin() - 1;
	if (k0 > k1) return false;
	first = s.times[k0];
	last = s.times[k1];
	return true;
}

/* A and B where the lines have values: at a quarter and three quarters of the span every shown line has samples in, in
 * view (a view whose data starts late put A before the first sample, and every readout read a dash); when the lines
 * share no span, of the span any of them has; with no samples in view, of the view */
void ChartItem::placeCursors() {
	if (!(axes_.span > 0)) return;
	setLive(false); /* the readouts hold still */
	double lo = axes_.t0, hi = axes_.t1;
	double shareFrom = -INFINITY, shareTo = INFINITY, anyFrom = INFINITY, anyTo = -INFINITY;
	bool any = false;
	for (const auto &line : lines_) {
		double first = 0, last = 0;
		if (!line->shown || !samplesInView(line->series, axes_.t0, axes_.t1, first, last)) continue;
		any = true;
		shareFrom = std::max(shareFrom, first);
		shareTo = std::min(shareTo, last);
		anyFrom = std::min(anyFrom, first);
		anyTo = std::max(anyTo, last);
	}
	if (any && shareTo > shareFrom) {
		lo = shareFrom;
		hi = shareTo;
	} else if (any && anyTo > anyFrom) {
		lo = anyFrom;
		hi = anyTo;
	}
	setCursors(lo + (hi - lo) / 4, lo + (hi - lo) * 3 / 4);
}

void ChartItem::clearCursors() { setCursors(NAN, NAN); }

void ChartItem::moveCursor(int which, double x) {
	if (!cursorsOn() || plot_.width() <= 0) return;
	const double clamped = std::clamp(x, plot_.left(), plot_.right());
	const double t = axes_.t0 + (clamped - plot_.left()) / plot_.width() * axes_.span;
	if (which == 0) setCursors(t, cursorB_);
	else setCursors(cursorA_, t);
}

void ChartItem::setCursors(double a, double b) {
	const bool same = (a == cursorA_ || (std::isnan(a) && std::isnan(cursorA_)))
			&& (b == cursorB_ || (std::isnan(b) && std::isnan(cursorB_)));
	if (same) return;
	cursorA_ = a;
	cursorB_ = b;
	emit cursorsChanged();
	updateReadouts();
	polish();
}

ChartData::Stats ChartItem::cursorStats(int id) const {
	const Line *line = find(id);
	if (!line || !cursorsOn()) return ChartData::Stats();
	const ChartData::Series &s = line->series;
	const double t0 = std::min(cursorA_, cursorB_), t1 = std::max(cursorA_, cursorB_);
	if (s.fast) return s.fast->hasTime() ? ChartData::statsOfFast(*s.fast, s.channel, t0, t1, cursorA_, cursorB_)
										 : ChartData::Stats();
	return s.times.isEmpty() ? ChartData::Stats() : ChartData::statsOf(s.times, s.values, t0, t1, cursorA_, cursorB_);
}

/* each shown line's values at A and B and over A..B (the desktop's measurements, ChartView::stats), as the legend
 * writes a value; a value the line has not there (outside its samples, a fast line's summaries): an em dash (an en dash
 * beside "B - A" read as one more minus, in Arabic most of all) */
void ChartItem::updateReadouts() {
	QVector<CursorReadouts::Row> rows;
	if (cursorsOn())
		for (const auto &line : lines_) {
			if (!line->shown) continue;
			const ChartData::Stats st = cursorStats(line->id);
			const QString unit = line->series.unit.isEmpty() ? QString() : QLatin1Char(' ') + line->series.unit;
			const auto text = [&unit](double v, bool ok) {
				return ok && std::isfinite(v) ? ChartData::number(v) + unit : QStringLiteral("\u2014");
			};
			CursorReadouts::Row row;
			row.id = line->id;
			row.name = line->series.name;
			row.colour = colourOf(*line);
			row.atA = text(st.atA, true);
			row.atB = text(st.atB, true);
			row.delta = text(st.atB - st.atA, true);
			row.min = text(st.min, st.ok);
			row.max = text(st.max, st.ok);
			row.mean = text(st.mean, st.ok && !st.summaries);
			rows << row;
		}
	readouts_->set(rows);
}

ChartItem::~ChartItem() = default;

ChartItem::Line *ChartItem::find(int id) {
	for (auto &line : lines_)
		if (line->id == id) return line.get();
	return nullptr;
}

const ChartItem::Line *ChartItem::find(int id) const { return const_cast<ChartItem *>(this)->find(id); }

void ChartItem::setWindowSeconds(double seconds) {
	seconds = std::clamp(seconds, MIN_WINDOW, MAX_WINDOW);
	if (seconds == window_) return;
	if (!live_) viewEnd_ = axes_.t1; /* a held view keeps its end */
	window_ = seconds;
	emit windowSecondsChanged();
	polish();
}

void ChartItem::setLive(bool live) {
	if (live == live_) return;
	if (!live) viewEnd_ = axes_.t1 > axes_.t0 ? axes_.t1 : now(); /* held where it was drawn last */
	live_ = live;
	emit liveChanged();
	polish();
}

void ChartItem::setYAuto(bool on) {
	if (on == yAuto_) return;
	yAuto_ = on;
	if (on) yHeld_ = false; /* Auto again: the values in view at once, not by a twentieth a frame from the manual range */
	emit yAutoChanged();
	polish();
}

bool ChartItem::setYRange(double lo, double hi) {
	if (!std::isfinite(lo) || !std::isfinite(hi) || !(hi > lo)) return false;
	yLo_ = lo;
	yHi_ = hi;
	setYAuto(false);
	polish();
	return true;
}

void ChartItem::setColours(const QVariantList &colours) {
	if (colours == colours_) return;
	colours_ = colours;
	colorsDirty_ = true;
	emit colorsChanged();
	updateLegend(false);
	update();
}

QColor ChartItem::colourOf(const Line &line) const {
	if (line.colourIndex < 0 || colours_.isEmpty()) return line.series.color;
	return colours_[line.colourIndex % colours_.size()].value<QColor>();
}

void ChartItem::setGridColor(const QColor &color) {
	if (color == gridColor_) return;
	gridColor_ = color;
	colorsDirty_ = true;
	emit colorsChanged();
	update();
}

void ChartItem::setFrameColor(const QColor &color) {
	if (color == frameColor_) return;
	frameColor_ = color;
	colorsDirty_ = true;
	emit colorsChanged();
	update();
}

void ChartItem::addLine(int id, const QColor &colour, const QString &name, const QString &unit) {
	auto line = std::make_unique<Line>();
	line->id = id;
	line->series.color = colour;
	line->series.name = name;
	line->series.unit = unit;
	addLine(std::move(line));
}

/* a line in the theme's series colour `colourIndex` (as the desktop gives each new line the palette's next); a fast one
 * reads its stream's store, channel `channel` */
void ChartItem::addThemedLine(int id, int colourIndex, const QString &name, const QString &unit,
		std::shared_ptr<fast::Store> fast, int channel) {
	auto line = std::make_unique<Line>();
	line->id = id;
	line->colourIndex = colourIndex;
	line->series.name = name;
	line->series.unit = unit;
	line->series.fast = std::move(fast);
	line->series.channel = channel;
	line->series.color = colourOf(*line);
	addLine(std::move(line));
}

/* a line already there takes the new colour and names, its samples kept */
void ChartItem::addLine(std::unique_ptr<Line> line) {
	if (Line *had = find(line->id)) {
		had->series.color = line->series.color;
		had->series.name = line->series.name;
		had->series.unit = line->series.unit;
		had->colourIndex = line->colourIndex;
		colorsDirty_ = true;
		updateLegend(false);
		update();
		return;
	}
	line->series.spread = int(lines_.size() % 8);
	lines_.push_back(std::move(line));
	++generation_;
	linesDirty_ = true;
	updateLegend(true);
	emit linesChanged();
	polish();
}

void ChartItem::removeLine(int id) {
	const auto it = std::find_if(lines_.begin(), lines_.end(), [id](const auto &line) { return line->id == id; });
	if (it == lines_.end()) return;
	lines_.erase(it);
	++generation_;
	linesDirty_ = true;
	updateLegend(false);
	emit linesChanged();
	polish();
}

void ChartItem::removeAll() {
	if (lines_.empty()) return;
	lines_.clear();
	++generation_;
	linesDirty_ = true;
	yHeld_ = false;
	updateLegend(false);
	emit linesChanged();
	polish();
}

void ChartItem::setLineShown(int id, bool shown) {
	Line *line = find(id);
	if (!line || line->shown == shown) return;
	line->shown = shown;
	line->binned = ChartData::BinnedLine();
	line->vertices.clear();
	updateLegend(false);
	updateReadouts();
	polish();
}

bool ChartItem::lineShown(int id) const {
	const Line *line = find(id);
	return line && line->shown;
}

QColor ChartItem::lineColour(int id) const {
	const Line *line = find(id);
	return line ? colourOf(*line) : QColor();
}

const ChartData::Series *ChartItem::series(int id) const {
	const Line *line = find(id);
	return line ? &line->series : nullptr;
}

/* the legend's rows; values: the latest value in view of each line again (else the texts kept) */
void ChartItem::updateLegend(bool values) {
	QVector<ChartLines::Entry> entries;
	entries.reserve(qsizetype(lines_.size()));
	for (auto &line : lines_) {
		if (values) {
			double v = 0;
			/* a fast line's records have times once its stream's first mark came */
			const bool timed = !line->series.fast || line->series.fast->hasTime();
			const bool any = timed && axes_.t1 > axes_.t0 && ChartData::latestInView(line->series, axes_.t0, axes_.t1, v);
			line->value = any ? ChartData::number(v) + (line->series.unit.isEmpty() ? QString() : QLatin1Char(' ')
					+ line->series.unit) : QString();
		}
		ChartLines::Entry entry;
		entry.id = line->id;
		entry.name = line->series.name;
		entry.unit = line->series.unit;
		entry.value = line->value;
		entry.colour = colourOf(*line);
		entry.shown = line->shown;
		entry.fast = line->series.fast != nullptr;
		entries << entry;
	}
	legend_->set(entries);
}

void ChartItem::clear() {
	for (auto &line : lines_) {
		ChartData::Series kept;
		kept.color = line->series.color;
		kept.spread = line->series.spread;
		kept.name = line->series.name;
		kept.unit = line->series.unit;
		kept.fast = line->series.fast;
		kept.channel = line->series.channel;
		if (kept.fast) kept.fast->clear(); /* its stream's records, the desktop's Clear (ChartView::clearData) */
		line->series = std::move(kept);
		line->binned = ChartData::BinnedLine();
		line->vertices.clear();
		line->value.clear();
	}
	++generation_;
	yHeld_ = false;
	updateLegend(false);
	polish();
}

void ChartItem::setClock(std::function<double()> clock) {
	clock_ = std::move(clock);
	polish();
}

double ChartItem::now() const {
	if (clock_) return clock_();
	double newest = 0;
	bool any = false;
	for (const auto &line : lines_)
		if (!line->series.times.isEmpty()) {
			newest = any ? std::max(newest, line->series.times.last()) : line->series.times.last();
			any = true;
		}
	return newest;
}

/* as the desktop's chart takes a sample (ChartView::appendNow): room for it, then the chunks it completes */
void ChartItem::appendSamples(int id, const QVector<double> &times, const QVector<double> &values) {
	Line *line = find(id);
	if (!line) return;
	ChartData::Series &s = line->series;
	const qsizetype most = qsizetype(memory_ * 20000) + ChartData::CHUNK_SIZE[ChartData::LEVELS - 1] * 2;
	for (qsizetype i = 0; i < std::min(times.size(), values.size()); ++i) {
		const double t = times[i], v = values[i];
		if (!std::isfinite(v) || !std::isfinite(t) || (!s.times.isEmpty() && t < s.times.last())) continue;
		ChartData::roomForOne(s.times, most, s.spread);
		ChartData::roomForOne(s.values, most, s.spread);
		s.times.push_back(t);
		s.values.push_back(v);
		ChartData::addChunks(s, most);
		s.last = v;
		s.hasLast = true;
	}
	trim(s);
	polish();
}

/* past the memory: the oldest samples go, in whole chunks of the largest level as the desktop's memory drops them
 * (ChartView::dropExpired), so chunk k of each level still covers samples k times its size on */
void ChartItem::trim(ChartData::Series &s) {
	if (s.times.isEmpty() || s.times.last() - s.times.first() <= memory_ * 1.05 + 0.5) return;
	const qsizetype largest = ChartData::CHUNK_SIZE[ChartData::LEVELS - 1];
	qsizetype drop = std::lower_bound(s.times.begin(), s.times.end(), s.times.last() - memory_) - s.times.begin();
	drop = drop / largest * largest;
	if (drop <= 0) return;
	s.times.remove(0, drop);
	s.values.remove(0, drop);
	s.dropped += drop;
	for (int level = 0; level < ChartData::LEVELS; ++level)
		s.chunks[level].remove(0, std::min(s.chunks[level].size(), drop / ChartData::CHUNK_SIZE[level]));
}

void ChartItem::zoomTime(double factor) {
	if (!(factor > 0) || !std::isfinite(factor)) return;
	setWindowSeconds(window_ / factor);
}

void ChartItem::panPixels(double dx) {
	if (plot_.width() <= 0) return;
	setLive(false);
	viewEnd_ -= dx / plot_.width() * window_;
	polish();
}

QVector<ChartData::Bin> ChartItem::bins(int id) const {
	const Line *line = find(id);
	return line ? line->binned.bins : QVector<ChartData::Bin>();
}

void ChartItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
	QQuickItem::geometryChange(newGeometry, oldGeometry);
	const QRectF plot(LEFT_PAD, TOP_PAD, std::max(0.0, width() - LEFT_PAD - RIGHT_PAD),
			std::max(0.0, height() - TOP_PAD - BOTTOM_PAD));
	if (plot != plot_) {
		plot_ = plot;
		emit plotRectChanged();
	}
	polish();
}

void ChartItem::itemChange(ItemChange change, const ItemChangeData &value) {
	QQuickItem::itemChange(change, value);
	if (change == ItemSceneChange) {
		disconnect(frameConnection_);
		/* frameSwapped comes on the render thread: queued, the next frame is asked for on this one */
		if (value.window)
			frameConnection_ = connect(value.window, &QQuickWindow::frameSwapped, this, &ChartItem::frameTick,
					Qt::QueuedConnection);
	}
	if (change == ItemVisibleHasChanged && value.boolValue) polish();
}

void ChartItem::frameTick() {
	if (live_ && isVisible() && !lines_.empty()) polish();
}

/* the Y range of this frame: Auto grows at once and shrinks gently (a twentieth of the way a frame), as the
 * desktop's; held, it stays */
void ChartItem::updateYRange() {
	double lo = std::numeric_limits<double>::max(), hi = -lo;
	/* a fast line's float channel may hold an infinity (fast::Store passes it on): such a range is left out, or the
	 * ticks' loop never ended */
	for (const auto &line : lines_)
		if (line->shown && !line->binned.bins.isEmpty() && std::isfinite(line->binned.lo) && std::isfinite(line->binned.hi)
				&& line->binned.lo <= line->binned.hi) {
			lo = std::min(lo, line->binned.lo);
			hi = std::max(hi, line->binned.hi);
		}
	if (yAuto_ && lo <= hi) {
		double margin = (hi - lo) * 0.05;
		if (margin <= 0) margin = std::max(std::fabs(hi) * 0.1, 0.5); /* a flat line: in the middle */
		const double wantLo = lo - margin, wantHi = hi + margin;
		if (!std::isfinite(wantLo) || !std::isfinite(wantHi)) {
			/* a span past the doubles' range: kept as it was */
		} else if (!yHeld_) {
			yLo_ = wantLo;
			yHi_ = wantHi;
			yHeld_ = true;
		} else {
			yLo_ = wantLo < yLo_ ? wantLo : yLo_ + (wantLo - yLo_) * 0.05;
			yHi_ = wantHi > yHi_ ? wantHi : yHi_ + (wantHi - yHi_) * 0.05;
		}
	}
	if (!std::isfinite(yLo_) || !std::isfinite(yHi_)) { /* never kept: every later frame would be NaN */
		yLo_ = 0;
		yHi_ = 1;
		yHeld_ = false;
	}
	if (!(yHi_ > yLo_)) yHi_ = yLo_ + 1;
	const bool moved = axes_.lo != yLo_ || axes_.hi != yHi_;
	axes_.setRange(yLo_, yHi_, false);
	if (moved) emit yRangeChanged();
}

/* the grid at 1-2-5 steps in time and value, one device pixel wide on whole device pixels, and its labels */
void ChartItem::makeTicks(QVector<float> &grid) {
	const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
	const double hair = 1 / dpr;
	/* whole device pixels of the window, not of the item: it may sit at a fraction of one */
	const QPointF at = mapToScene(QPointF(0, 0));
	const auto snapX = [dpr, at](double x) { return std::floor((x + at.x()) * dpr + 1e-6) / dpr - at.x(); };
	const auto snapY = [dpr, at](double y) { return std::floor((y + at.y()) * dpr + 1e-6) / dpr - at.y(); };
	grid.clear();
	QVector<ChartTicks::Tick> times, values;
	/* at most MAX_LOOP steps each way: a step under a value's precision (v + step == v) cannot spin either */
	constexpr int MAX_LOOP = 64;
	const int xCount = std::max(2, int(plot_.width() / 90));
	const double tStep = niceStep(axes_.span / xCount);
	const bool timesOk = std::isfinite(axes_.t0) && std::isfinite(axes_.t1) && std::isfinite(tStep) && tStep > 0;
	int steps = 0;
	for (double t = std::ceil(axes_.t0 / tStep) * tStep; timesOk && t <= axes_.t1 + tStep * 1e-9 && steps < MAX_LOOP;
			t += tStep, ++steps) {
		const double x = snapX(axes_.x(t));
		if (x < plot_.left() || x >= plot_.right()) continue;
		rect(grid, x, plot_.top(), x + hair, plot_.bottom());
		times.push_back({ x, timeLabel(t, tStep) });
	}
	const int yCount = std::clamp(int(plot_.height() / 40), 2, MAX_TICKS_Y);
	const double vStep = niceStep((axes_.hi - axes_.lo) / yCount);
	const bool valuesOk = std::isfinite(axes_.lo) && std::isfinite(axes_.hi) && std::isfinite(vStep) && vStep > 0;
	steps = 0;
	for (double v = std::ceil(axes_.lo / vStep) * vStep; valuesOk && v <= axes_.hi + vStep * 1e-9 && steps < MAX_LOOP;
			v += vStep, ++steps) {
		const double y = snapY(axes_.y(v));
		if (y < plot_.top() || y >= plot_.bottom()) continue;
		rect(grid, plot_.left(), y, plot_.right(), y + hair);
		values.push_back({ y, valueLabel(v, vStep) });
	}
	timeTicks_->set(times);
	valueTicks_->set(values);
}

void ChartItem::updatePolish() {
	++polishes_;
	QElapsedTimer took;
	took.start();
	const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
	const double t1 = live_ ? now() : viewEnd_;
	axes_.rect = plot_;
	axes_.t0 = t1 - window_;
	axes_.t1 = t1;
	axes_.span = window_;
	axes_.columns = std::max(1.0, std::round(plot_.width()));
	/* each line binned as ChartView::binView bins the view's (the bins kept from the frame before) */
	for (auto &line : lines_) {
		if (!line->shown) continue; /* hidden: nothing binned, nothing drawn */
		ChartData::BinnedLine fresh;
		ChartData::binViewSeries(line->series, axes_.t0, axes_.t1, axes_.columns, fresh, &line->binned, generation_,
				polledBinned_, fastBinned_);
		line->binned = std::move(fresh);
	}
	updateYRange();
	makeTicks(grid_);
	/* the plot's edge, a device pixel inside it */
	const double hair = 1 / dpr;
	frame_.clear();
	if (!plot_.isEmpty()) {
		rect(frame_, plot_.left(), plot_.top(), plot_.right(), plot_.top() + hair);
		rect(frame_, plot_.left(), plot_.bottom() - hair, plot_.right(), plot_.bottom());
		rect(frame_, plot_.left(), plot_.top(), plot_.left() + hair, plot_.bottom());
		rect(frame_, plot_.right() - hair, plot_.top(), plot_.right(), plot_.bottom());
	}
	/* each line's triangles: a full column's min..max a band, the line through its first and last value */
	const auto x = [this](double t) { return axes_.x(t); };
	const auto y = [this](double v) { return axes_.y(v); };
	for (auto &line : lines_) {
		QVector<float> &out = line->vertices;
		out.clear();
		if (line->binned.bins.isEmpty() || plot_.isEmpty()) continue;
		QVector<QRectF> bands;
		QVector<qsizetype> breaks;
		const QPolygonF poly = ChartData::toPolyline(line->binned.bins, axes_.columnSeconds(), x, y, &bands, LINE_WIDTH,
				hair, &breaks);
		out.reserve((bands.size() + poly.size()) * 12);
		for (const QRectF &band : std::as_const(bands)) rect(out, band.left(), band.top(), band.right(), band.bottom());
		/* piece by piece between the breaks (a fast line's gaps): nothing across one; a piece of one point a dot */
		qsizetype start = 0;
		for (qsizetype k = 0; k <= breaks.size(); ++k) {
			const qsizetype end = k < breaks.size() ? breaks[k] : poly.size();
			if (end - start == 1) segment(out, poly[start], poly[start], LINE_WIDTH);
			for (qsizetype i = start + 1; i < end; ++i) segment(out, poly[i - 1], poly[i], LINE_WIDTH);
			start = end;
		}
	}
	/* the legend's values: at their pace while live, at once for a view that moved by hand */
	if (!live_ || !legendClock_.isValid() || legendClock_.elapsed() >= LEGEND_MS) {
		legendClock_.start();
		updateLegend(true);
		updateReadouts();
	}
	const double ax = cursorAX(), bx = cursorBX();
	const auto moved = [](double was, double now) { return std::isnan(was) != std::isnan(now) || (!std::isnan(now) && was != now); };
	if (moved(cursorAtX_, ax) || moved(cursorBtX_, bx)) {
		cursorAtX_ = ax;
		cursorBtX_ = bx;
		emit cursorsMoved();
	}
	polishSeconds_ += took.nsecsElapsed() / 1e9;
	emit framed();
	update();
}

/* on the render thread, the window's thread waiting: the triangles made by updatePolish into the nodes. The tree: the
 * grid, the plot's edge, then a clip of the plot holding a node per line */
QSGNode *ChartItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
	QSGNode *root = old;
	if (!root) {
		root = new QSGNode;
		root->appendChildNode(flatNode(gridColor_));
		root->appendChildNode(flatNode(frameColor_));
		auto *clip = new QSGClipNode;
		clip->setIsRectangular(true);
		auto *clipGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
		clip->setGeometry(clipGeometry);
		clip->setFlag(QSGNode::OwnsGeometry);
		root->appendChildNode(clip);
		colorsDirty_ = true;
	}
	auto *gridNode = static_cast<QSGGeometryNode *>(root->childAtIndex(0));
	auto *frameNode = static_cast<QSGGeometryNode *>(root->childAtIndex(1));
	auto *clip = static_cast<QSGClipNode *>(root->childAtIndex(2));
	fill(gridNode, grid_);
	fill(frameNode, frame_);
	if (clip->clipRect() != plot_) {
		clip->setClipRect(plot_);
		QSGGeometry::updateRectGeometry(clip->geometry(), plot_);
		clip->markDirty(QSGNode::DirtyGeometry);
	}
	/* a node per line, in the lines' order */
	while (clip->childCount() > int(lines_.size())) {
		QSGNode *last = clip->lastChild();
		clip->removeChildNode(last);
		delete last;
	}
	while (clip->childCount() < int(lines_.size())) clip->appendChildNode(flatNode(Qt::black));
	if (colorsDirty_ || linesDirty_) {
		recolor(gridNode, gridColor_);
		recolor(frameNode, frameColor_);
		for (int i = 0; i < int(lines_.size()); ++i)
			recolor(static_cast<QSGGeometryNode *>(clip->childAtIndex(i)), colourOf(*lines_[size_t(i)]));
		colorsDirty_ = linesDirty_ = false;
	}
	for (int i = 0; i < int(lines_.size()); ++i)
		fill(static_cast<QSGGeometryNode *>(clip->childAtIndex(i)), lines_[size_t(i)]->vertices);
	return root;
}
