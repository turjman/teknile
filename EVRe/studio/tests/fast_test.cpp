/* SPDX-License-Identifier: Apache-2.0 */
/* evre_fast_test: Fast EVRe without a window or a device (FAST_PLAN.md section 10): the rules of the block, a fuzz
 * of random bytes, the clock's fit against a device that runs fast or slow, the source the fake devices send from,
 * the frames lib/fast builds (tests/fast_lib_test.py hands them over in EVRE_FAST_FRAMES), and the store the chart
 * keeps a stream's records in (model/fast_store.h). */
#include <QFile>
#include <QtTest>
#include <cmath>
#include <random>

#include "evre/frame.h"
#include "io/fast_stream.h"
#include "model/device_map.h"
#include "model/fast_recording.h"
#include "model/fast_store.h"

using namespace fast;

namespace {

/* the example map's stream: two i16 channels, a record of 4 bytes, 254 records a block */
StreamDef exampleStream(double rate = 10000) {
	StreamDef def;
	def.name = QStringLiteral("ADC");
	def.addr = 0xDC00;
	def.size = 1024;
	def.rate = rate;
	StreamChannel current;
	current.name = QStringLiteral("I_LOAD");
	current.unit = QStringLiteral("A");
	current.scale = 0.0005;
	StreamChannel voltage;
	voltage.name = QStringLiteral("V_BUS");
	voltage.unit = QStringLiteral("V");
	voltage.scale = 0.001;
	def.channels = { current, voltage };
	return def;
}

/* a block's frame data: the header and `count` records of 4 bytes */
QByteArray blockData(uint32_t first, uint16_t count, uint8_t flags = 0, uint8_t spare = 0) {
	QByteArray data(HEADER + count * 4, '\0');
	for (int i = 0; i < 4; i++) data[i] = char(first >> (8 * i));
	data[4] = char(count);
	data[5] = char(count >> 8);
	data[6] = char(flags);
	data[7] = char(spare);
	for (int i = 0; i < count * 4; i++) data[HEADER + i] = char(i);
	return data;
}

} // namespace

class FastTest : public QObject {
	Q_OBJECT

private slots:
	void header() {
		BlockHeader h;
		QCOMPARE(fastBlock(blockData(0x12345678, 3, FLAG_START), 4, h), BlockCheck::Ok);
		QCOMPARE(h.first, 0x12345678U);
		QCOMPARE(h.count, uint16_t(3));
		QCOMPARE(h.flags, FLAG_START);
		QCOMPARE(fastBlock(blockData(0, 0), 4, h), BlockCheck::Ok); /* a block of no records: its header alone */
	}

	/* the frame's count is exactly 8 + count x record size, else a bad block, counted, nothing used */
	void countMustFit() {
		BlockHeader h;
		QByteArray data = blockData(0, 3);
		QCOMPARE(fastBlock(data + "x", 4, h), BlockCheck::BadCount);
		QCOMPARE(fastBlock(data.left(data.size() - 1), 4, h), BlockCheck::BadCount);
		QCOMPARE(fastBlock(data.left(7), 4, h), BlockCheck::BadCount);
		QCOMPARE(fastBlock(data, 6, h), BlockCheck::BadCount); /* another record size */
		FastStream s;
		s.reset(exampleStream());
		BlockTaken t;
		QCOMPARE(s.take(data + "x", 1.0, t), BlockCheck::BadCount);
		QCOMPARE(s.badBlocks, quint64(1));
		QCOMPARE(s.records, quint64(0));
		QCOMPARE(t.count, 0);
	}

	/* a flag bit nobody knows, or a spare that is not 0: a newer kind, none of its records used */
	void newerKind() {
		FastStream s;
		s.reset(exampleStream());
		BlockTaken t;
		QCOMPARE(s.take(blockData(0, 2, FLAG_START | 0x04), 1.0, t), BlockCheck::NewerKind);
		QCOMPARE(s.take(blockData(0, 2, FLAG_START, 1), 1.0, t), BlockCheck::NewerKind);
		QCOMPARE(s.newerBlocks, quint64(2));
		QCOMPARE(s.records, quint64(0));
		/* both bits known are taken */
		QCOMPARE(s.take(blockData(0, 2, FLAG_START | FLAG_LOST), 1.0, t), BlockCheck::Ok);
	}

	/* START, then numbers that follow; LOST and a gap counted from `first`; a restart without START */
	void numbers() {
		FastStream s;
		s.reset(exampleStream());
		BlockTaken t;
		QCOMPARE(s.take(blockData(0, 10, FLAG_START), 1.00, t), BlockCheck::Ok);
		QVERIFY(t.newStart);
		QCOMPARE(t.first, quint64(0));
		QCOMPARE(s.take(blockData(10, 10), 1.01, t), BlockCheck::Ok);
		QVERIFY(!t.newStart);
		QCOMPARE(t.lost, quint64(0));
		QCOMPARE(t.first, quint64(10));
		/* 30 records dropped: the block says LOST, but `first` is the truth */
		QCOMPARE(s.take(blockData(50, 10, FLAG_LOST), 1.02, t), BlockCheck::Ok);
		QCOMPARE(t.lost, quint64(30));
		QCOMPARE(t.first, quint64(50));
		/* a gap without LOST counts the same */
		QCOMPARE(s.take(blockData(65, 10), 1.03, t), BlockCheck::Ok);
		QCOMPARE(t.lost, quint64(5));
		QCOMPARE(s.lost, quint64(35));
		/* the number went back without START: the device restarted */
		QCOMPARE(s.take(blockData(3, 10), 1.04, t), BlockCheck::Ok);
		QVERIFY(t.newStart);
		QCOMPARE(t.lost, quint64(0));
		QCOMPARE(t.first, quint64(3));
		/* START again: a new start, whatever the number */
		QCOMPARE(s.take(blockData(13, 10, FLAG_START), 1.05, t), BlockCheck::Ok);
		QVERIFY(t.newStart);
		QCOMPARE(s.starts, quint64(3));
		QCOMPARE(s.records, quint64(60));
		QCOMPARE(s.blocks, quint64(6));
	}

	/* the device's u32 wraps; the host's 64-bit number goes on */
	void wrap() {
		FastStream s;
		s.reset(exampleStream());
		BlockTaken t;
		QCOMPARE(s.take(blockData(0xFFFFFFF0U, 16, FLAG_START), 1.0, t), BlockCheck::Ok);
		QCOMPARE(s.take(blockData(0, 16), 1.01, t), BlockCheck::Ok);
		QVERIFY(!t.newStart);
		QCOMPARE(t.lost, quint64(0));
		QCOMPARE(t.first, quint64(0x100000000ULL));
		QCOMPARE(s.take(blockData(26, 16), 1.02, t), BlockCheck::Ok); /* 10 lost across nothing special */
		QCOMPARE(t.lost, quint64(10));
		QCOMPARE(t.first, quint64(0x100000000ULL + 26));
	}

	/* a host that joins a running stream begins at the first block it sees */
	void joinLate() {
		FastStream s;
		s.reset(exampleStream());
		BlockTaken t;
		QCOMPARE(s.take(blockData(123456, 10), 1.0, t), BlockCheck::Ok);
		QVERIFY(t.newStart);
		QCOMPARE(t.first, quint64(123456));
		QCOMPARE(t.lost, quint64(0));
		QCOMPARE(s.take(blockData(123466, 10), 1.001, t), BlockCheck::Ok);
		QCOMPARE(t.lost, quint64(0));
	}

	/* a bad block's records are not trusted: they show as lost at the next good one */
	void badBlockThenGood() {
		FastStream s;
		s.reset(exampleStream());
		BlockTaken t;
		s.take(blockData(0, 10, FLAG_START), 1.0, t);
		QCOMPARE(s.take(blockData(10, 10) + "zz", 1.01, t), BlockCheck::BadCount);
		QCOMPARE(s.take(blockData(20, 10), 1.02, t), BlockCheck::Ok);
		QCOMPARE(t.lost, quint64(10));
	}

	/* random bytes: no crash, and never a record out of a block that is not exactly right */
	void fuzz() {
		std::mt19937 random(20261005);
		FastStream s;
		s.reset(exampleStream());
		quint64 used = 0;
		for (int i = 0; i < 200000; i++) {
			QByteArray data(int(random() % 40), '\0');
			for (char &c : data) c = char(random());
			if (i % 3 == 0 && data.size() >= HEADER) { /* often a header that looks right */
				data[6] = char(random() % 4);
				data[7] = 0;
			}
			BlockTaken t;
			const BlockCheck check = s.take(data, 1.0 + i * 1e-4, t);
			if (check != BlockCheck::Ok) {
				QCOMPARE(t.count, 0);
				continue;
			}
			const int count = int(evre::littleEndian16(data, 4));
			QCOMPARE(data.size(), HEADER + count * 4);
			QCOMPARE(t.count, count);
			used += quint64(count);
		}
		QCOMPARE(s.records, used);
		QCOMPARE(s.blocks + s.badBlocks + s.newerBlocks, quint64(200000));
	}

	/* The clock's fit (FAST_PLAN.md section 16): a device 200 ppm fast or slow, its blocks arriving late by a
	 * random delay (now and then by 20 ms). After one minute and after ten its records lie within 2 ms of the host's
	 * clock, the correction shown is within 20 ppm of the truth, the rate changes by at most a few ppm a second, and
	 * the time never steps. */
	void clockFit_data() {
		QTest::addColumn<double>("ppm");
		QTest::newRow("200 ppm fast") << 200.0;
		QTest::newRow("200 ppm slow") << -200.0;
		QTest::newRow("on time") << 0.0;
	}
	void clockFit() {
		QFETCH(double, ppm);
		const double nominal = 10000, rate = nominal * (1 + ppm * 1e-6), start = 5.0;
		std::mt19937 random(7);
		std::exponential_distribution<double> late(1 / 0.0005);
		FastStream s;
		s.reset(exampleStream(nominal));
		quint64 next = 0;
		FastClock::Mark mark;
		double markPpm = 0, markAt = 0;
		double worstSlew = 0, worstStep = 0;
		for (int block = 0; block < 60000; block++) { /* 100 records every 10 ms: ten minutes */
			const uint16_t count = 100;
			const double taken = start + double(next + count) / rate; /* the block's last record */
			double arrival = taken + 0.0002 + late(random);
			if (block % 500 == 250) arrival += 0.020;
			BlockTaken t;
			QCOMPARE(s.take(blockData(uint32_t(next), count, block == 0 ? FLAG_START : 0), arrival, t), BlockCheck::Ok);
			next += count;
			if (t.newMark && block > 0) {
				const FastClock::Mark now = s.clock().mark();
				/* the new mark lies on the line before it: no step */
				const double onOldLine = mark.time + double(now.record - mark.record) / (markPpm * 1e-6 + 1) / nominal;
				worstStep = std::max(worstStep, std::fabs(onOldLine - now.time));
				worstSlew = std::max(worstSlew, std::fabs(s.clock().ppm() - markPpm) / std::max(1e-3, arrival - markAt));
			}
			if (t.newMark) {
				mark = s.clock().mark();
				markPpm = s.clock().ppm();
				markAt = arrival;
			}
			if (block == 5999 || block == 59999) {
				std::printf("%s after %d s: %.3f ms, %.1f ppm, slew %.2f\n", QTest::currentDataTag(), (block + 1) / 100, (s.clock().timeOf(next) - (start + double(next) / rate)) * 1e3, s.clock().ppm(), worstSlew);
				const double error = s.clock().timeOf(next) - (start + double(next) / rate);
				QVERIFY2(std::fabs(error) < 0.002, qPrintable(QStringLiteral("off by %1 ms after %2 s")
						.arg(error * 1e3).arg((block + 1) / 100)));
				QVERIFY2(std::fabs(s.clock().ppm() - ppm) < 20, qPrintable(QStringLiteral("shows %1 ppm, truth %2")
						.arg(s.clock().ppm()).arg(ppm)));
			}
		}
		QVERIFY2(worstStep < 1e-6, qPrintable(QStringLiteral("a step of %1 us").arg(worstStep * 1e6)));
		QVERIFY2(worstSlew <= FastClock::MAX_SLEW_PPM * 1.05, qPrintable(QStringLiteral("%1 ppm a second").arg(worstSlew)));
	}

	/* a new start gets a new pair of numbers: the clock starts again from the block's arrival */
	void clockRestart() {
		FastStream s;
		s.reset(exampleStream());
		BlockTaken t;
		s.take(blockData(0, 100, FLAG_START), 1.0, t);
		s.take(blockData(100, 100), 1.01, t);
		QCOMPARE(s.clock().timeOf(200), 1.01);
		s.take(blockData(0, 100, FLAG_START), 50.0, t);
		QVERIFY(t.newStart && t.newMark);
		QCOMPARE(s.clock().timeOf(100), 50.0);
		QCOMPARE(s.clock().timeOf(0), 50.0 - 0.01); /* a block's length before its arrival */
	}

	/* the source the fake devices send from: blocks when full or 10 ms old, the waves, the test aids */
	void source() {
		const StreamDef def = exampleStream();
		FastSource source(def, 1);
		source.start({}, 0);
		/* 25 ms at 10 000 a second: 250 records, no block full yet (254), the first record 25 ms old */
		QByteArray out = source.due(25000000);
		evre::Parser parser;
		parser.feed(out);
		evre::Frame frame;
		QVERIFY(parser.next(frame));
		QCOMPARE(frame.fn, uint8_t(evre::READ_RESP));
		QCOMPARE(frame.addr, uint16_t(0xDC00));
		BlockHeader h;
		QCOMPARE(fastBlock(frame.data, 4, h), BlockCheck::Ok);
		QCOMPARE(h.first, 0U);
		QCOMPARE(h.count, uint16_t(250));
		QCOMPARE(h.flags, FLAG_START);
		/* each record's values follow from its number */
		for (int k : { 0, 17, 249 }) {
			const char *record = frame.data.constData() + HEADER + 4 * k;
			QCOMPARE(channelValue(def.channels[0], record), FastSource::wave(def, 0, quint64(k), 10000) * 0.0005);
			QCOMPARE(channelValue(def.channels[1], record + 2), FastSource::wave(def, 1, quint64(k), 10000) * 0.001);
		}
		QVERIFY(!parser.next(frame));
		/* a second later: 39 full blocks of 254, then the rest of 144, its oldest record 14 ms old; numbers in order */
		parser.feed(source.due(1030000000));
		FastStream s;
		s.reset(def);
		BlockTaken t;
		quint64 records = 0;
		bool started = false;
		while (parser.next(frame)) {
			QCOMPARE(s.take(frame.data, 1.0, t), BlockCheck::Ok);
			if (!started) QCOMPARE(t.first, quint64(250));
			started = true;
			records += quint64(t.count);
		}
		QCOMPARE(s.lost, quint64(0));
		QCOMPARE(records, quint64(10050));
	}

	void sourceAids() {
		const StreamDef def = exampleStream();
		/* --fast-lose 3: every third block not sent; the next one says LOST, the numbers count the gap */
		FastSource source(def, 1);
		FastSource::Options options;
		options.loseEvery = 3;
		options.first = 0xFFFFFF00U; /* --fast-first: just below the wrap */
		source.start(options, 0);
		evre::Parser parser;
		parser.feed(source.due(1000000000)); /* 10 000 records */
		FastStream s;
		s.reset(def);
		evre::Frame frame;
		BlockTaken t;
		int blocks = 0, lostFlags = 0;
		while (parser.next(frame)) {
			BlockHeader h;
			fastBlock(frame.data, 4, h);
			if (h.flags & FLAG_LOST) lostFlags++;
			QCOMPARE(s.take(frame.data, 1.0, t), BlockCheck::Ok);
			blocks++;
		}
		/* 39 full blocks made (the 94 records after them are not 10 ms old yet): blocks 3, 6, ... 39 not sent; the
		 * last of them has no block after it to tell. Across the wrap, no new start. */
		QCOMPARE(blocks, 26);
		QCOMPARE(s.starts, quint64(1));
		QCOMPARE(s.records, quint64(26 * 254));
		QCOMPARE(s.lost, quint64(12 * 254));
		QCOMPARE(lostFlags, 12);
		QVERIFY(t.first > 0xFFFFFFFFULL);
		/* --fast-rate and --fast-ppm: the records a second as built */
		FastSource fastOne(def, 1);
		FastSource::Options faster;
		faster.rate = 1000000;
		faster.ppm = 1000;
		fastOne.start(faster, 0);
		parser.clear();
		parser.feed(fastOne.due(100000000)); /* 100 ms */
		quint64 records = 0;
		while (parser.next(frame)) {
			BlockHeader h;
			fastBlock(frame.data, 4, h);
			records += h.count;
		}
		QVERIFY2(records >= 100000 && records <= 100100, qPrintable(QString::number(records)));
	}

	/* ---- the store */

	/* 300 000 records of two i16 channels in blocks of random sizes, now and then some lost, marks at the clock's
	 * pace: every record's time and value, and the summaries over random spans equal a plain loop */
	void storeSummaries() {
		const StreamDef def = exampleStream(100000);
		fast::Store store(def);
		std::mt19937 random(11);
		QVector<qint16> current, voltage;
		QVector<quint64> numbers;
		quint64 next = 0;
		bool first = true;
		while (numbers.size() < 300000) {
			const int count = int(random() % 300) + 1;
			const quint64 lost = random() % 40 == 0 ? random() % 500 + 1 : 0;
			next += lost;
			QByteArray records(count * 4, '\0');
			for (int k = 0; k < count; k++) {
				const qint16 a = qint16(random()), b = qint16(random() % 2000 - 1000);
				records[4 * k] = char(a);
				records[4 * k + 1] = char(a >> 8);
				records[4 * k + 2] = char(b);
				records[4 * k + 3] = char(b >> 8);
				current << a;
				voltage << b;
				numbers << next + quint64(k);
			}
			store.append(next, count, records.constData(), first, first ? 0 : lost);
			if (first || numbers.size() % 7 == 0) store.mark(next + quint64(count), 1.0 + double(next + quint64(count)) * 1e-5, 1e-5);
			first = false;
			next += quint64(count);
		}
		QCOMPARE(store.size(), qsizetype(numbers.size()));
		bool values = true, times = true;
		for (qsizetype i = 0; i < store.size(); i += 97) {
			values = values && store.value(0, i) == current[i] * 0.0005 && store.value(1, i) == voltage[i] * 0.001;
			times = times && std::fabs(store.timeAt(i) - (1.0 + double(numbers[i]) * 1e-5)) < 1e-9;
		}
		QVERIFY(values);
		QVERIFY(times);
		int wrong = 0;
		for (int round = 0; round < 3000; round++) {
			qsizetype i0 = qsizetype(random() % quint32(store.size())), i1 = qsizetype(random() % quint32(store.size()));
			if (round % 3 == 0) i1 = std::min<qsizetype>(store.size(), i0 + qsizetype(random() % 600)); /* short ones */
			if (i0 > i1) std::swap(i0, i1);
			if (i0 == i1) continue;
			for (int c = 0; c < 2; c++) {
				double lo = 1e300, hi = -1e300, sum = 0, squares = 0;
				for (qsizetype i = i0; i < i1; i++) {
					const double v = c == 0 ? current[i] * 0.0005 : voltage[i] * 0.001;
					lo = std::min(lo, v);
					hi = std::max(hi, v);
					sum += v;
					squares += v * v;
				}
				double slo, shi, ssum, ssquares;
				store.minMax(c, i0, i1, slo, shi);
				store.sums(c, i0, i1, ssum, ssquares);
				if (slo != lo || shi != hi || std::fabs(ssum - sum) > 1e-9 * (1 + std::fabs(sum))
						|| std::fabs(ssquares - squares) > 1e-9 * (1 + squares))
					wrong++;
			}
		}
		QCOMPARE(wrong, 0);
		/* the gaps: where the numbers jump, and only there */
		int gapsSeen = 0, gapsWrong = 0;
		for (qsizetype i = 1; i < store.size(); i++) {
			const bool gap = numbers[i] != numbers[i - 1] + 1;
			gapsSeen += gap;
			if (store.startsAfterGap(i) != gap) gapsWrong++;
		}
		QCOMPARE(gapsWrong, 0);
		QCOMPARE(qsizetype(gapsSeen), store.gaps());
		QVERIFY(gapsSeen > 10);
		QCOMPARE(store.segmentEnd(0) > 0, true);
		QVERIFY(!store.startsAfterGap(0));
	}

	/* lowerBound and upperBound: the first record at or after a time, and after it, across gaps */
	void storeBounds() {
		fast::Store store(exampleStream(1000));
		QByteArray records(4 * 100, '\0');
		store.append(0, 100, records.constData(), true, 0);
		store.mark(100, 10.0, 0.001);         /* record 100 at 10 s: record 0 at 9.9 s */
		store.append(150, 100, records.constData(), false, 50);
		std::mt19937 random(3);
		bool right = true;
		for (int k = 0; k < 2000; k++) {
			const double t = 9.85 + (random() % 400000) * 1e-6;
			const qsizetype lo = store.lowerBound(t), up = store.upperBound(t);
			if (lo < store.size() && store.timeAt(lo) < t) right = false;
			if (lo > 0 && store.timeAt(lo - 1) >= t) right = false;
			if (up < store.size() && store.timeAt(up) <= t) right = false;
			if (up > 0 && store.timeAt(up - 1) > t) right = false;
		}
		QVERIFY(right);
		QCOMPARE(store.lowerBound(9.9), qsizetype(0));
		QCOMPARE(store.lowerBound(10.0), qsizetype(100)); /* in the gap: the first record after it, number 150 */
		QCOMPARE(store.timeAt(100), 10.05);
		QCOMPARE(store.upperBound(100.0), store.size());
		QCOMPARE(store.segmentEnd(10), qsizetype(100));
		QVERIFY(store.startsAfterGap(100));
		QCOMPARE(store.lostBefore(100), qint64(50)); /* the gap's count, for its tooltip */
		QCOMPARE(store.lostBefore(99), qint64(0));
	}

	/* a new start whose clock begins before the last one ended: shifted after it, the times never go back */
	void storeNewStart() {
		fast::Store store(exampleStream(1000));
		QByteArray records(4 * 100, '\0');
		store.append(0, 100, records.constData(), true, 0);
		store.mark(100, 10.0, 0.001);
		store.append(0, 100, records.constData(), true, 0);
		store.mark(100, 9.95, 0.001); /* its first record at 9.85 s: before 9.999 */
		bool rising = true;
		for (qsizetype i = 1; i < store.size(); i++) rising = rising && store.timeAt(i) > store.timeAt(i - 1);
		QVERIFY(rising);
		QVERIFY(store.startsAfterGap(100));
		QCOMPARE(store.lostBefore(100), qint64(-1)); /* a new start: not counted */
		QVERIFY(std::fabs(store.timeAt(100) - (store.timeAt(99) + 0.001)) < 1e-9);
	}

	/* A recording written as the Studio writes it (a block lost now and then, a restart) and read back: the records,
	 * their values and times as a store fed live holds them, the gaps counted; a file cut off reads up to its last
	 * whole piece; a file of another kind is refused */
	void recordingReadBack() {
		const StreamDef def = exampleStream();
		QTemporaryDir folder;
		const QString path = folder.filePath(QStringLiteral("run.ADC.evrs"));
		RecordingWriter writer;
		QString err;
		QVERIFY(writer.open(path, QStringLiteral("example"), def, QDateTime::currentDateTime(), err, 12.5));
		FastSource source(def, 1);
		FastSource::Options lose;
		lose.loseEvery = 7;
		source.start(lose, 0);
		FastStream state;
		state.reset(def);
		Store live(def);
		evre::Parser parser;
		quint64 lost = 0;
		const auto run = [&](qint64 fromNs, qint64 toNs) {
			for (qint64 ns = fromNs; ns <= toNs; ns += 10000000) {
				parser.feed(source.due(ns));
				evre::Frame frame;
				while (parser.next(frame)) {
					BlockTaken taken;
					if (state.take(frame.data, 12.5 + ns / 1e9, taken) != BlockCheck::Ok) continue;
					if (taken.newMark) QVERIFY(writer.mark(state.clock().mark().record, state.clock().mark().time));
					QVERIFY(writer.block(frame.data));
					live.append(taken.first, taken.count, frame.data.constData() + HEADER, taken.newStart, taken.lost);
					if (taken.newMark) live.mark(state.clock().mark().record, state.clock().mark().time, state.clock().period());
					lost += taken.lost;
				}
			}
		};
		run(0, 3000000000LL);
		source.start({}, 3010000000LL); /* the device started again: START, numbers from 0 */
		run(3010000000LL, 5000000000LL);
		writer.close();

		Recording read;
		QString error;
		const std::atomic<bool> cancel{ false };
		QVERIFY2(readRecording(path, cancel, {}, read, error), qPrintable(error));
		QVERIFY(read.store && !read.cut && read.store->mapped());
		QCOMPARE(read.store->size(), live.size());
		QCOMPARE(read.lost, lost);
		QVERIFY(lost > 0);
		QCOMPARE(read.store->gaps(), live.gaps());
		QCOMPARE(read.startClock, 12.5);
		QCOMPARE(read.stream.name, def.name);
		double worst = 0;
		int differ = 0;
		for (qsizetype i = 0; i < live.size(); i++) {
			worst = std::max(worst, std::fabs(read.store->timeAt(i) - live.timeAt(i)));
			if (read.store->value(0, i) != live.value(0, i) || read.store->value(1, i) != live.value(1, i)) differ++;
		}
		qInfo("recording read back: %lld records, %lld lost, times within %.3g s of the live store's", (long long) live.size(),
				(long long) lost, worst);
		QCOMPARE(differ, 0);
		QVERIFY(worst < 1e-4);
		double lo, hi, liveLo, liveHi;
		read.store->minMax(0, 0, read.store->size(), lo, hi);
		live.minMax(0, 0, live.size(), liveLo, liveHi);
		QCOMPARE(lo, liveLo);
		QCOMPARE(hi, liveHi);

		/* cut off inside its last piece */
		const QString cutPath = folder.filePath(QStringLiteral("cut.ADC.evrs"));
		QVERIFY(QFile::copy(path, cutPath));
		{
			QFile cut(cutPath);
			QVERIFY(cut.open(QIODevice::ReadWrite));
			QVERIFY(cut.resize(cut.size() - 5));
		}
		Recording cutRead;
		QVERIFY(readRecording(cutPath, cancel, {}, cutRead, error));
		QVERIFY(cutRead.cut && cutRead.bytesRead < cutRead.bytes);
		QCOMPARE(cutRead.blocks, read.blocks - 1);
		/* not a recording */
		const QString other = folder.filePath(QStringLiteral("run.csv"));
		{
			QFile csv(other);
			QVERIFY(csv.open(QIODevice::WriteOnly));
			csv.write("time_s,datetime\n");
		}
		Recording none;
		QVERIFY(!readRecording(other, cancel, {}, none, error) && !error.isEmpty());
		/* beside a CSV */
		QFile(folder.filePath(QStringLiteral("run.csv"))).open(QIODevice::WriteOnly);
		QCOMPARE(recordingsBeside(folder.filePath(QStringLiteral("run.csv"))), QStringList{ path });
	}

	/* trims drop whole pieces; what stays reads the same; the bytes it says it holds are what its arrays hold */
	void storeTrim() {
		fast::Store store(exampleStream(1000000));
		const qsizetype n = 5 * fast::Store::PIECE + 1234;
		QByteArray records(int(n * 4), '\0');
		for (qsizetype k = 0; k < n; k++) {
			records[int(4 * k)] = char(k);
			records[int(4 * k + 1)] = char(k >> 8);
		}
		store.append(0, n, records.constData(), true, 0);
		store.mark(quint64(n), 100.0, 1e-6);
		const double t = store.timeAt(2 * fast::Store::PIECE + 10);
		const double v = store.value(0, 2 * fast::Store::PIECE + 10);
		store.dropFront(2 * fast::Store::PIECE + 5);
		QCOMPARE(store.dropped(), qint64(2 * fast::Store::PIECE));
		QCOMPARE(store.size(), n - 2 * fast::Store::PIECE);
		QCOMPARE(store.timeAt(10), t);
		QCOMPARE(store.value(0, 10), v);
		double lo, hi;
		store.minMax(0, 0, store.size(), lo, hi);
		double plo = 1e300, phi = -1e300;
		for (qsizetype i = 0; i < store.size(); i++) {
			plo = std::min(plo, store.value(0, i));
			phi = std::max(phi, store.value(0, i));
		}
		QCOMPARE(lo, plo);
		QCOMPARE(hi, phi);
		store.dropFront(100 * fast::Store::PIECE); /* never the records after the last whole piece */
		QCOMPARE(store.size(), qsizetype(1234));
		/* what it says it holds: its pieces (each a piece's room) and its summaries, near a record's share each */
		const qint64 held = store.bytes();
		QVERIFY(held >= qint64(store.size()) * 4);
		QVERIFY2(held <= qint64(fast::Store::PIECE) * 4 + 16384, qPrintable(QString::number(held)));
		fast::Store full(exampleStream(1000000));
		full.append(0, n, records.constData(), true, 0);
		full.mark(quint64(n), 100.0, 1e-6);
		const double perRecord = double(full.bytes()) / double(full.size());
		QVERIFY2(perRecord >= full.bytesPerRecord() * 0.95 && perRecord <= full.bytesPerRecord() * 1.25,
				qPrintable(QStringLiteral("%1 bytes a record held, %2 said").arg(perRecord).arg(full.bytesPerRecord())));
		/* Clear keeps the newest mark: the stream goes on with times */
		full.clear();
		full.append(quint64(n), 10, records.constData(), false, 0);
		QVERIFY(full.hasTime());
		QCOMPARE(full.timeAt(0), 100.0); /* record n, at its mark */
	}

	/* the frames lib/fast built (tests/fast_lib_test.py): through the Studio's parser and the block's rules. The file
	 * holds the frames, then a line per frame: "first count flags" as the helper was asked to build them */
	void helperFrames() {
		const QString file = qEnvironmentVariable("EVRE_FAST_FRAMES");
		if (file.isEmpty()) QSKIP("EVRE_FAST_FRAMES not set (tests/fast_lib_test.py sets it)");
		QFile in(file);
		QVERIFY(in.open(QIODevice::ReadOnly));
		const QList<QByteArray> parts = in.readAll().split('\n');
		QVERIFY(parts.size() >= 2);
		evre::Parser parser;
		parser.feed(QByteArray::fromHex(parts[0]));
		FastStream s;
		StreamDef def = exampleStream();
		s.reset(def);
		evre::Frame frame;
		int i = 1;
		while (parser.next(frame)) {
			const QList<QByteArray> want = parts.value(i++).split(' ');
			QCOMPARE(want.size(), 3);
			QCOMPARE(frame.slave, uint8_t(1));
			QCOMPARE(frame.fn, uint8_t(evre::READ_RESP));
			QCOMPARE(frame.addr, uint16_t(0xDC00));
			BlockHeader h;
			QCOMPARE(fastBlock(frame.data, 4, h), BlockCheck::Ok);
			QCOMPARE(h.first, want[0].toUInt());
			QCOMPARE(int(h.count), want[1].toInt());
			QCOMPARE(int(h.flags), want[2].toInt());
			BlockTaken t;
			QCOMPARE(s.take(frame.data, 1.0, t), BlockCheck::Ok);
			for (int k = 0; k < h.count; k++) /* the helper's test fills record k of a block with k, k + 1 */
				QCOMPARE(int(int16_t(evre::littleEndian16(frame.data, HEADER + 4 * k))), int(h.first + uint32_t(k)) & 0x7FFF);
		}
		QCOMPARE(parser.badFrames(), 0);
		QCOMPARE(i - 1, int(parts.size()) - 2);
		std::printf("helper frames: %d, records %llu, lost %llu\n", i - 1, s.records, s.lost);
	}
};

QTEST_GUILESS_MAIN(FastTest)
#include "fast_test.moc"
