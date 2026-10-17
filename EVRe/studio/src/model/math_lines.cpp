/* SPDX-License-Identifier: Apache-2.0 */
/* The chart's math lines: see math_lines.h. */
#include "model/math_lines.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStringList>
#include <QVarLengthArray>
#include <algorithm>
#include <cmath>
#include <limits>

#include "io/fast_stream.h"

namespace {


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

bool MathLine::compile(const QVector<RegDef> &registers, const QVector<StreamDef> &streams) {
	inputs.clear();
	channels.clear();
	stream = -1;
	error.clear();
	QString firstChannel, otherStream; /* a channel of the stream taken, and one of another stream named after it */
	/* each register or fast channel named becomes an input, numbered in the order it is first named; a register's name
	 * wins over a channel's (STREAM.CHANNEL), as before there were fast lines */
	const auto input = [&](RegKey key, int channel) {
		for (int i = 0; i < inputs.size(); i++)
			if (channels[i] == channel && (channel >= 0 || inputs[i] == key)) return i;
		inputs << key;
		channels << channel;
		return int(inputs.size()) - 1;
	};
	const auto resolve = [&](const QString &name) {
		if (const RegDef *def = numericRegister(registers, name)) return input(regKey(*def), -1);
		for (int s = 0; s < streams.size(); s++) {
			for (int c = 0; c < streams[s].channels.size(); c++) {
				const StreamChannel &channel = streams[s].channels[c];
				if (channel.type == RegType::Bytes
						|| (streams[s].name + QLatin1Char('.') + channel.name).compare(name, Qt::CaseInsensitive) != 0)
					continue;
				/* one stream's records hold all its channels at one instant; another stream's are of another clock */
				if (stream >= 0 && stream != s) {
					otherStream = name;
					return -1;
				}
				if (stream < 0) firstChannel = name;
				stream = s;
				return input(0, c);
			}
		}
		return -1;
	};
	if (expr.parse(formula, resolve, error)) return true;
	if (!otherStream.isEmpty())
		error = QCoreApplication::translate("MathLine", "%1 and %2: two streams, two clocks: not in this version")
						.arg(firstChannel, otherStream);
	inputs.clear();
	channels.clear();
	stream = -1;
	return false;
}

void MathLine::evaluateRecords(const StreamDef &def, const char *records, qsizetype count, const double *held,
		float *out) const {
	const int inputCount = int(inputs.size());
	const int size = def.recordSize();
	/* each channel input's place in a record, found once a block */
	QVarLengthArray<int, 16> offsets(inputCount);
	for (int i = 0; i < inputCount; i++) {
		offsets[i] = 0;
		for (int c = 0; c < channels[i]; c++) offsets[i] += typeSize(def.channels[c].type);
	}
	QVarLengthArray<double, 16> values(inputCount);
	for (int i = 0; i < inputCount; i++) values[i] = channels[i] < 0 ? held[i] : 0.0;
	for (qsizetype k = 0; k < count; k++) {
		const char *record = records + k * size;
		for (int i = 0; i < inputCount; i++)
			if (channels[i] >= 0) values[i] = fast::channelValue(def.channels[channels[i]], record + offsets[i]);
		const float value = float(expr.eval(values.data()));
		out[k] = std::isfinite(value) ? value : std::numeric_limits<float>::quiet_NaN();
	}
}

/* ------------------------------------------------------------- the settings */

void MathLines::load(const QString &key) {
	key_ = key;
	lines_.clear();
	for (const QString &text : QSettings().value(key_).toStringList()) {
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
	QSettings().setValue(key_, texts);
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

void MathLines::compile(const QVector<RegDef> &registers, const QVector<StreamDef> &streams) {
	for (MathLine &line : lines_) line.compile(registers, streams);
}

QVector<RegKey> MathLines::registersRead() const {
	QVector<RegKey> registers;
	for (const MathLine &line : lines_) {
		if (!line.active()) continue;
		for (int i = 0; i < line.inputs.size(); i++)
			if (line.channels[i] < 0 && !registers.contains(line.inputs[i])) registers << line.inputs[i];
	}
	return registers;
}

void MathLines::evaluate(const Samples &samples, const PointSink &out) const {
	for (int i = 0; i < lines_.size() && i < MAX_DRAWN; i++) {
		const MathLine &line = lines_[i];
		if (line.active() && !line.fast() && !line.inputs.isEmpty()) evaluateLine(line, chartKey(i), samples, out);
	}
}
