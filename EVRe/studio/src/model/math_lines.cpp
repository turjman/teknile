/* SPDX-License-Identifier: Apache-2.0 */
/* The chart's math lines: see math_lines.h. */
#include "model/math_lines.h"

#include <QSettings>
#include <QStringList>
#include <QVarLengthArray>
#include <algorithm>

namespace {

const QString SETTINGS_KEY = QStringLiteral("chart/math");

/* the first numeric register of this name (in any case), nullptr if none */
const RegDef *numericRegister(const QVector<RegDef> &registers, const QString &name) {
	for (const RegDef &def : registers)
		if (def.name.compare(name, Qt::CaseInsensitive) == 0 && def.isNumeric()) return &def;
	return nullptr;
}

/* one line's points: a point for each poll of its first register, with the
 * other registers' values from the same poll (none of theirs: no point) */
void evaluateLine(const MathLine &line, int key, const MathLines::Samples &samples, const MathLines::PointSink &out) {
	const auto first = samples.constFind(line.inputs[0]);
	if (first == samples.constEnd()) return;
	const int inputCount = int(line.inputs.size());
	QVector<QHash<double, double>> valueAtTime(inputCount); /* of the other registers */
	for (int input = 1; input < inputCount; input++)
		for (const QPointF &p : samples.value(line.inputs[input])) valueAtTime[input].insert(p.x(), p.y());
	QVarLengthArray<double, 16> values(inputCount);
	for (const QPointF &p : *first) {
		values[0] = p.y();
		bool complete = true;
		for (int input = 1; input < inputCount && complete; input++) {
			const auto found = valueAtTime[input].constFind(p.x());
			if (found == valueAtTime[input].constEnd()) complete = false;
			else values[input] = found.value();
		}
		if (complete) out(key, p.x(), line.expr.eval(values.data()));
	}
}

} // namespace

/* ----------------------------------------------------------------- one line */

bool MathLine::compile(const QVector<RegDef> &registers) {
	inputs.clear();
	error.clear();
	/* each register named becomes an input, numbered in the order it is first named */
	const auto resolve = [&](const QString &name) {
		const RegDef *def = numericRegister(registers, name);
		if (!def) return -1;
		int input = int(inputs.indexOf(regKey(*def)));
		if (input < 0) {
			input = int(inputs.size());
			inputs << regKey(*def);
		}
		return input;
	};
	if (expr.parse(formula, resolve, error)) return true;
	inputs.clear();
	return false;
}

/* ------------------------------------------------------------- the settings */

void MathLines::load() {
	lines_.clear();
	for (const QString &text : QSettings().value(SETTINGS_KEY).toStringList()) {
		const QStringList fields = text.split(QLatin1Char('\t'));
		if (fields.size() < 3) continue;
		MathLine line;
		line.name = fields[0];
		line.unit = fields[1];
		line.formula = fields[2];
		line.on = fields.value(3) != QLatin1String("0"); /* no fourth field: shown */
		lines_ << line;
	}
}

void MathLines::save() const {
	QStringList texts;
	for (const MathLine &line : lines_) {
		texts << QStringList{ line.name, line.unit, line.formula, line.on ? QStringLiteral("1") : QStringLiteral("0") }
						.join(QLatin1Char('\t'));
	}
	QSettings().setValue(SETTINGS_KEY, texts);
}

/* ------------------------------------------------------------------ changes */

void MathLines::add(const MathLine &line) {
	lines_ << line;
	save();
}

void MathLines::replace(int index, const MathLine &line) {
	lines_[index] = line;
	save();
}

void MathLines::remove(int index) {
	lines_.removeAt(index);
	save();
}

void MathLines::setOn(int index, bool on) {
	lines_[index].on = on;
	save();
}

/* ---------------------------------------- against the map, and on the chart */

int MathLines::activeCount() const {
	return int(std::count_if(lines_.begin(), lines_.end(), [](const MathLine &line) { return line.active(); }));
}

void MathLines::compile(const QVector<RegDef> &registers) {
	for (MathLine &line : lines_) line.compile(registers);
}

QVector<RegKey> MathLines::registersRead() const {
	QVector<RegKey> registers;
	for (const MathLine &line : lines_) {
		if (!line.active()) continue;
		for (RegKey key : line.inputs)
			if (!registers.contains(key)) registers << key;
	}
	return registers;
}

void MathLines::evaluate(const Samples &samples, const PointSink &out) const {
	for (int i = 0; i < lines_.size() && i < MAX_DRAWN; i++) {
		const MathLine &line = lines_[i];
		if (line.active() && !line.inputs.isEmpty()) evaluateLine(line, chartKey(i), samples, out);
	}
}
