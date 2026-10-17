/* SPDX-License-Identifier: Apache-2.0 */
/* The chart's math lines: a formula over registers, drawn and measured like
 * a register's line, for example
 *
 *   P [W] = SUPPLY_V * SUPPLY_I
 *
 * A line is its name, unit, formula and whether it is shown. The formula is
 * compiled against the map's registers (compile(), again whenever the map
 * changes); a formula naming a register the map does not have keeps the
 * reason in `error` and is not drawn.
 *
 * The lines are kept in the settings under "chart/math", one text per line:
 * "name \t unit \t formula \t 1|0" (the last field: shown; left out, as in
 * an older entry: shown). Every change is saved at once.
 *
 * On the chart, line i has the key FIRST_CHART_KEY + i, clear of every
 * register's key (a register's line is keyed by regKey: its device and
 * address, below 1 << 24).
 *
 * A fast math line (Fast EVRe): a formula whose fast channels (ADC.I_LOAD)
 * all belong to one stream is computed for every record of that stream, at
 * the record's own time, from the channels of that record; a register in it
 * is held at its last polled value. The chart keeps its records as a fast
 * line's, in a store of their own (the chart's stream fastStream(i)), so it
 * is drawn, measured, triggered and exported as one. Channels of two streams
 * are refused: two streams, two clocks. */
#pragma once

#include <QHash>
#include <QPointF>
#include <QString>
#include <QVector>
#include <cstdint>
#include <functional>

#include "model/device_map.h"
#include "model/expr.h"

/* One math line: what the user typed, and the formula compiled from it. */
struct MathLine {
	QString name, unit, formula;
	bool on = true;            /* shown, when its formula compiles */

	/* the formula compiled against the map, by compile() */
	Expr expr;
	QVector<RegKey> inputs;    /* the registers it reads: input i of expr is register inputs[i] (where channels[i] < 0) */
	QVector<int> channels;     /* input i is channel channels[i] of `stream`; -1: a register */
	int stream = -1;           /* a fast math line: the map's stream all its channels are of; -1: it reads registers only */
	QString error;             /* why the formula does not compile, in words for the user; empty: it does */

	/* false (error set) when the formula is not valid or names no numeric register or fast channel of these (or
	 * channels of two streams) */
	bool compile(const QVector<RegDef> &registers, const QVector<StreamDef> &streams = {});
	/* on and compiled: drawn, and its registers sampled */
	bool active() const { return on && error.isEmpty(); }
	bool fast() const { return stream >= 0; }
	/* a fast line's values for `count` records of its stream as they came (`def`'s layout), one float each into out;
	 * held[i]: the value of input i where it is a register (its last polled one). A result that is not a finite number
	 * is NaN there (the caller leaves that record out) */
	void evaluateRecords(const StreamDef &def, const char *records, qsizetype count, const double *held, float *out) const;
};

/* Every math line, as kept in the settings, and their points on the chart. */
class MathLines {
public:
	static constexpr int FIRST_CHART_KEY = 1 << 24;
	static constexpr int MAX_DRAWN = 64;  /* lines past this many are kept but not drawn */
	static int chartKey(int line) { return FIRST_CHART_KEY + line; }
	/* a fast math line's records: the chart's store of this stream, clear of the map's streams (ChartView::fastKey) */
	static constexpr int FAST_STREAM = 1024;
	static int fastStream(int line) { return FAST_STREAM + line; }

	/* the samples of one frame, per register key (regKey): (poll time, value) */
	using Samples = QHash<RegKey, QVector<QPointF>>;
	/* where evaluate() puts each point of a math line */
	using PointSink = std::function<void(int chartKey, double time, double value)>;

	/* the lines kept in the settings, under `key` (a recording's chart keeps its own) */
	void load(const QString &key = QStringLiteral("chart/math"));

	const QVector<MathLine> &lines() const { return lines_; }
	bool isEmpty() const { return lines_.isEmpty(); }
	int activeCount() const;

	/* changes, each saved at once; compile() again after them */
	void add(const MathLine &line);
	void replace(int index, const MathLine &line);
	void remove(int index);
	void setOn(int index, bool on);

	/* every formula against the map's registers and fast streams */
	void compile(const QVector<RegDef> &registers, const QVector<StreamDef> &streams = {});
	/* the registers the active lines read, each once (a fast line's too: held for its records): they must be sampled
	 * for the chart */
	QVector<RegKey> registersRead() const;

	/* the active lines' points for one frame's samples: a point for every poll
	 * that gave all the registers a line reads. A poll gives each of its
	 * registers the same time, so the inputs are matched by it. A fast line's
	 * records come with its stream's blocks instead (evaluateRecords). */
	void evaluate(const Samples &samples, const PointSink &out) const;

private:
	void save() const;

	QVector<MathLine> lines_;
	QString key_ = QStringLiteral("chart/math");
};
