/* SPDX-License-Identifier: Apache-2.0 */
/* The analysis of one line's samples: see analysis.h. */
#include "model/analysis.h"

#include <algorithm>
#include <cmath>

namespace analysis {

namespace {

/* the value at fraction p (0..1) of the sorted values, straight between the two around it */
double quantile(const QVector<double> &sorted, double p) {
	const double at = p * double(sorted.size() - 1);
	const qsizetype k = qsizetype(std::floor(at));
	if (k + 1 >= sorted.size()) return sorted.last();
	return sorted[k] + (sorted[k + 1] - sorted[k]) * (at - double(k));
}

qsizetype powerOfTwoBelow(double n) {
	qsizetype p = 1;
	while (double(p * 2) <= n) p *= 2;
	return p;
}

} // namespace

bool isPowerOfTwo(qsizetype n) { return n > 0 && (n & (n - 1)) == 0; }

/* iterative Cooley-Tukey: the samples in bit-reversed order, then log2(n) passes of butterflies */
void fft(QVector<std::complex<double>> &data, bool inverse) {
	const qsizetype n = data.size();
	if (!isPowerOfTwo(n) || n < 2) return;
	for (qsizetype i = 1, j = 0; i < n; i++) {
		qsizetype bit = n >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) std::swap(data[i], data[j]);
	}
	for (qsizetype length = 2; length <= n; length <<= 1) {
		const double angle = 2 * M_PI / double(length) * (inverse ? 1 : -1);
		const std::complex<double> step(std::cos(angle), std::sin(angle));
		for (qsizetype start = 0; start < n; start += length) {
			std::complex<double> w(1, 0);
			for (qsizetype k = 0; k < length / 2; k++) {
				const std::complex<double> a = data[start + k], b = data[start + k + length / 2] * w;
				data[start + k] = a + b;
				data[start + k + length / 2] = a - b;
				w *= step;
			}
		}
	}
}

Histogram histogram(const QVector<double> &values) {
	Histogram out;
	if (values.isEmpty()) return out;
	QVector<double> sorted = values;
	std::sort(sorted.begin(), sorted.end());
	const double lo = sorted.first(), hi = sorted.last();
	const qsizetype n = sorted.size();
	out.total = n;
	if (!(hi > lo)) { /* one value: one bin around it */
		out.width = std::max(std::fabs(lo) * 0.01, 1e-9);
		out.from = lo - out.width / 2;
		out.counts = { qint64(n) };
		return out;
	}
	/* Freedman-Diaconis; values that do not spread between the quartiles (a few levels): the square root of n bins */
	const double iqr = quantile(sorted, 0.75) - quantile(sorted, 0.25);
	double width = 2 * iqr / std::cbrt(double(n));
	if (!(width > 0)) width = (hi - lo) / std::ceil(std::sqrt(double(n)));
	int bins = int(std::ceil((hi - lo) / width));
	if (bins > MAX_BINS) {
		bins = MAX_BINS;
		width = (hi - lo) / MAX_BINS;
	}
	bins = std::max(bins, 1);
	out.from = lo;
	out.width = width;
	out.counts.fill(0, bins);
	for (double v : sorted) out.counts[std::clamp(int((v - lo) / width), 0, bins - 1)]++; /* the top value: the last bin */
	return out;
}

Spectrum spectrum(const QVector<double> &times, const QVector<double> &values, bool even) {
	Spectrum out;
	const qsizetype n = std::min(times.size(), values.size());
	if (n < MIN_SAMPLES || !(times[n - 1] > times[0])) return out;
	/* even steps at the mean rate, straight between the samples around each */
	const double t0 = times[0], span = times[n - 1] - t0;
	out.rate = double(n - 1) / span;
	out.resampled = !even;
	const qsizetype m = even ? n : qsizetype(std::floor(span * out.rate)) + 1;
	QVector<double> steps = even ? values.mid(0, n) : QVector<double>(m);
	for (qsizetype i = 0, k = 0; !even && i < m; i++) {
		const double t = t0 + double(i) / out.rate;
		while (k + 1 < n - 1 && times[k + 1] < t) k++;
		const double ta = times[k], tb = times[k + 1];
		steps[i] = tb > ta ? values[k] + (values[k + 1] - values[k]) * std::clamp((t - ta) / (tb - ta), 0.0, 1.0) : values[k];
	}
	/* segments of a power of two, about 8 of them overlapping by half (a shorter one: finer, but noisier) */
	const qsizetype length = std::clamp<qsizetype>(powerOfTwoBelow(double(m) / 4.5), 8, MAX_SEGMENT);
	const qsizetype segment = std::min(length, powerOfTwoBelow(double(m)));
	const qsizetype hop = segment / 2;
	QVector<double> window(segment);
	double windowSum = 0;
	for (qsizetype k = 0; k < segment; k++) {
		window[k] = 0.5 * (1 - std::cos(2 * M_PI * double(k) / double(segment))); /* periodic Hann */
		windowSum += window[k];
	}
	QVector<double> power(segment / 2 + 1, 0);
	QVector<std::complex<double>> buffer(segment);
	int count = 0;
	for (qsizetype start = 0; start + segment <= m; start += hop) {
		for (qsizetype k = 0; k < segment; k++) buffer[k] = steps[start + k] * window[k];
		fft(buffer);
		for (qsizetype j = 0; j <= segment / 2; j++) power[j] += std::norm(buffer[j]);
		count++;
	}
	out.segment = int(segment);
	out.segments = count;
	out.frequency.resize(power.size());
	out.amplitude.resize(power.size());
	for (qsizetype j = 0; j < power.size(); j++) {
		/* a sine's peak: twice the windowed bin over the window's sum; 0 Hz and half the rate once */
		const double magnitude = std::sqrt(power[j] / count) / windowSum;
		out.frequency[j] = double(j) * out.rate / double(segment);
		out.amplitude[j] = (j == 0 || j == segment / 2) ? magnitude : 2 * magnitude;
	}
	return out;
}

} // namespace analysis
