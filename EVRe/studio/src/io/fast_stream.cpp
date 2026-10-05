/* SPDX-License-Identifier: Apache-2.0 */
/* Fast EVRe, the host's side (see fast_stream.h): the block's rules, the clock's fit, a stream's running state,
 * and a stream as a device sends it. */
#include "io/fast_stream.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "evre/frame.h"

namespace fast {

/* ---------------------------------------------------------------- the block */

BlockCheck fastBlock(const QByteArray &data, int recordSize, BlockHeader &header) {
	header = BlockHeader();
	if (data.size() < HEADER || recordSize <= 0) return BlockCheck::BadCount;
	const auto *bytes = reinterpret_cast<const uint8_t *>(data.constData());
	header.first = evre::littleEndian32(bytes);
	header.count = evre::littleEndian16(bytes + 4);
	header.flags = bytes[6];
	header.spare = bytes[7];
	/* a newer kind may lay its records out otherwise: it is told apart before its size is judged */
	if ((header.flags & ~KNOWN_FLAGS) != 0 || header.spare != 0) return BlockCheck::NewerKind;
	if (qint64(data.size()) != HEADER + qint64(header.count) * recordSize) return BlockCheck::BadCount;
	return BlockCheck::Ok;
}

/* ---------------------------------------------------------------- the clock */

void FastClock::reset(double nominalRate) {
	nominal_ = nominalRate > 0 ? nominalRate : 1.0;
	started_ = false;
	correction_ = 0;
	period_ = 1.0 / nominal_;
	mark_ = origin_ = Mark();
	windowBlocks_ = 0;
	trendAt_.clear();
	trendResidual_.clear();
}

/* The first block sets the line: its last record at its arrival, so record `first` lies a block's length before
 * it. Later blocks are measured against the line; the earliest of each WINDOW_S corrects it. */
bool FastClock::arrived(quint64 end, double arrival) {
	if (!started_) {
		started_ = true;
		mark_ = origin_ = { end, arrival };
		windowStart_ = windowMinAt_ = arrival;
		windowMin_ = windowMinOrigin_ = 0;
		windowBlocks_ = 1;
		return true;
	}
	const double residual = arrival - timeOf(end);
	if (windowBlocks_ == 0 || residual < windowMin_) {
		windowMin_ = residual;
		windowMinOrigin_ = arrival - (origin_.time + double(qint64(end - origin_.record)) / nominal_);
		windowMinAt_ = arrival;
	}
	windowBlocks_++;
	if (arrival - windowStart_ < WINDOW_S) return false;
	closeWindow(end, arrival - windowStart_);
	windowStart_ = arrival;
	windowBlocks_ = 0;
	return true;
}

/* One span's earliest arrival: the trend of all of them gives the clock's rate against the map's (a device slower
 * than its map makes the arrivals drift later), its distance from the line the phase. The new mark lies on the old
 * line; only the period after it changes. */
void FastClock::closeWindow(quint64 end, double span) {
	trendAt_.push_back(windowMinAt_ - origin_.time);
	trendResidual_.push_back(windowMinOrigin_);
	if (trendAt_.size() > HISTORY) {
		trendAt_.remove(0);
		trendResidual_.remove(0);
	}
	double slope = correction_;
	const int n = int(trendAt_.size());
	if (n >= 3 && trendAt_.last() - trendAt_.first() >= 2 * WINDOW_S) {
		double meanX = 0, meanY = 0;
		for (int i = 0; i < n; i++) {
			meanX += trendAt_[i];
			meanY += trendResidual_[i];
		}
		meanX /= n;
		meanY /= n;
		double sxy = 0, sxx = 0;
		for (int i = 0; i < n; i++) {
			sxy += (trendAt_[i] - meanX) * (trendResidual_[i] - meanY);
			sxx += (trendAt_[i] - meanX) * (trendAt_[i] - meanX);
		}
		if (sxx > 0) slope = sxy / sxx;
	}
	const double phase = windowMin_; /* > 0: the arrivals come later than the line says, its records too early */
	const double target = slope + phase / PULL_S;
	double step = MAX_SLEW_PPM * 1e-6 * span;
	if (std::fabs(phase) > FAR_S) step *= std::fabs(phase) / FAR_S * 10.0;
	const Mark next{ end, timeOf(end) };
	correction_ += std::clamp(target - correction_, -step, step);
	correction_ = std::clamp(correction_, -0.5, 0.5);
	mark_ = next;
	period_ = (1.0 + correction_) / nominal_;
}

/* --------------------------------------------------------------- the stream */

void FastStream::reset(const StreamDef &def, double rate) {
	def_ = def;
	nominal_ = rate > 0 ? rate : def.rate;
	recordSize_ = def.recordSize();
	seen_ = false;
	next_ = 0;
	blocks = records = lost = badBlocks = newerBlocks = starts = 0;
	clock_.reset(nominal_);
}

/* The rules of the block: `first` is the truth about losses (LOST only tells a person who reads a dump). With
 * d = (first - expected) mod 2^32, d below 2^31 is d records lost; anything else, START, or the first block this
 * host sees, is a new start, with a new pair of clock numbers. A bad block's numbers are not trusted: the records
 * it held show as lost at the next good one. */
BlockCheck FastStream::take(const QByteArray &data, double arrival, BlockTaken &taken) {
	taken = BlockTaken();
	BlockHeader header;
	const BlockCheck check = fastBlock(data, recordSize_, header);
	if (check == BlockCheck::BadCount) badBlocks++;
	if (check == BlockCheck::NewerKind) newerBlocks++;
	if (check != BlockCheck::Ok) return check;
	const uint32_t d = header.first - uint32_t(next_);
	if (!seen_ || (header.flags & FLAG_START) || d >= 0x80000000U) {
		taken.newStart = true;
		taken.first = header.first;
		starts++;
		clock_.reset(nominal_);
	} else {
		taken.lost = d;
		taken.first = next_ + d;
	}
	seen_ = true;
	taken.count = header.count;
	next_ = taken.first + header.count;
	blocks++;
	records += header.count;
	lost += taken.lost;
	taken.newMark = clock_.arrived(next_, arrival);
	return check;
}

/* --------------------------------------------------------------- the source */

double FastSource::rate() const { return options_.rate > 0 ? options_.rate : def_.rate; }

void FastSource::start(const Options &options, qint64 nowNs) {
	options_ = options;
	startNs_ = nowNs;
	made_ = 0;
	next_ = options.first;
	blockNo_ = 0;
	pending_ = FLAG_START;
	running_ = rate() > 0 && def_.recordsPerBlock() > 0;
}

/* A sine of 50 Hz for the first channel, 100 Hz for the second, ..., at 40 % of the type's range, around its middle
 * for an unsigned type; an f32 channel swings 1 either side of 0. */
double FastSource::wave(const StreamDef &def, int channel, quint64 record, double rate) {
	const double t = double(record) / (rate > 0 ? rate : 1.0);
	const double s = std::sin(2 * M_PI * 50.0 * (channel + 1) * t + channel);
	double middle = 0, amplitude = 1;
	switch (def.channels[channel].type) {
	case RegType::I8: amplitude = 50; break;
	case RegType::U8: middle = 128; amplitude = 50; break;
	case RegType::I16: amplitude = 13000; break;
	case RegType::U16: middle = 32768; amplitude = 13000; break;
	case RegType::I32: amplitude = 1e6; break;
	case RegType::U32: middle = 2e9; amplitude = 1e6; break;
	case RegType::F32: return s;
	case RegType::Bytes: return 0;
	}
	return middle + std::round(amplitude * s);
}

QByteArray FastSource::due(qint64 nowNs) {
	if (!running_) return {};
	const double rate = this->rate() * (1.0 + options_.ppm * 1e-6);
	const quint64 dueRecords = quint64(double(nowNs - startNs_) / 1e9 * rate);
	const quint64 room = quint64(def_.recordsPerBlock());
	if (dueRecords <= made_) return {};
	/* over a second behind: what could not be sent is dropped, as a device whose transmitter is busy drops it */
	if (double(dueRecords - made_) > rate + double(room)) {
		const quint64 dropped = dueRecords - made_ - room;
		made_ += dropped;
		next_ += uint32_t(dropped);
		pending_ |= FLAG_LOST;
	}
	QByteArray out;
	while (dueRecords - made_ >= room) out += block(int(room));
	if (dueRecords > made_) {
		const double oldest = double(made_) / rate; /* seconds after the start */
		if (double(nowNs - startNs_) / 1e9 - oldest >= BLOCK_AGE_MS / 1000.0) out += block(int(dueRecords - made_));
	}
	return out;
}

QByteArray FastSource::block(int n) {
	const int record = def_.recordSize();
	QByteArray data(HEADER + n * record, '\0');
	auto *bytes = reinterpret_cast<uint8_t *>(data.data());
	for (int i = 0; i < 4; i++) bytes[i] = uint8_t(next_ >> (8 * i));
	bytes[4] = uint8_t(n);
	bytes[5] = uint8_t(n >> 8);
	bytes[6] = pending_;
	uint8_t *at = bytes + HEADER;
	for (int i = 0; i < n; i++) {
		const quint32 number = next_ + uint32_t(i);
		for (int c = 0; c < def_.channels.size(); c++) {
			const double raw = wave(def_, c, number, rate());
			const RegType type = def_.channels[c].type;
			if (type == RegType::F32) {
				const float f = float(raw);
				std::memcpy(at, &f, 4);
			} else {
				const qint64 v = qint64(raw);
				for (int b = 0; b < typeSize(type); b++) at[b] = uint8_t(quint64(v) >> (8 * b));
			}
			at += typeSize(type);
		}
	}
	next_ += uint32_t(n);
	made_ += quint64(n);
	blockNo_++;
	if (options_.loseEvery > 0 && blockNo_ % quint64(options_.loseEvery) == 0) {
		pending_ |= FLAG_LOST; /* not sent: its records keep their numbers, the next block says so */
		return {};
	}
	pending_ = 0;
	return evre::build(slave_, evre::READ_RESP, def_.addr, uint16_t(data.size()), data);
}

/* ---------------------------------------------------------------- channels */

double channelValue(const StreamChannel &channel, const char *bytes) {
	const auto *b = reinterpret_cast<const uint8_t *>(bytes);
	double raw = 0;
	switch (channel.type) {
	case RegType::U8: raw = b[0]; break;
	case RegType::I8: raw = int8_t(b[0]); break;
	case RegType::U16: raw = evre::littleEndian16(b); break;
	case RegType::I16: raw = int16_t(evre::littleEndian16(b)); break;
	case RegType::U32: raw = evre::littleEndian32(b); break;
	case RegType::I32: raw = int32_t(evre::littleEndian32(b)); break;
	case RegType::F32: {
		const uint32_t bits = evre::littleEndian32(b);
		float f;
		std::memcpy(&f, &bits, 4);
		raw = f;
		break;
	}
	case RegType::Bytes: return std::numeric_limits<double>::quiet_NaN();
	}
	return raw * channel.scale + channel.offset;
}

} // namespace fast
