/* SPDX-License-Identifier: Apache-2.0 */
/* The analysis of one line's samples (a span of the chart): how its values are
 * spread (a histogram) and which frequencies it holds (a spectrum). Nothing of
 * the chart or a window; no library: a radix-2 FFT of our own.
 *
 *  - Histogram: bins of equal width by the Freedman-Diaconis rule, 2 IQR /
 *    n^(1/3) (robust against a few outliers); values that do not spread (an
 *    IQR of 0) get the square-root rule; one value alone, one bin.
 *  - Spectrum: the polls are uneven, so the samples are first resampled to even
 *    steps at their mean rate (straight between neighbours); then Welch's
 *    method: segments of a power of two, 50 % overlap, each with a Hann window,
 *    their power averaged. The amplitude is a sine's peak in the line's unit
 *    (a sine of 2 V reads 2 V at its frequency), from 0 up to half the rate. */
#pragma once

#include <QVector>
#include <complex>

namespace analysis {

/* in place, the length a power of two (else nothing is done); inverse: the
 * other sign, not scaled */
void fft(QVector<std::complex<double>> &data, bool inverse = false);
bool isPowerOfTwo(qsizetype n);

struct Histogram {
	double from = 0, width = 1; /* bin k covers from + k width .. from + (k + 1) width */
	QVector<qint64> counts;
	qint64 total = 0;
	double binFrom(int k) const { return from + k * width; }
	double binTo(int k) const { return from + (k + 1) * width; }
};
/* MAX_BINS at most (wider bins then) */
constexpr int MAX_BINS = 2000;
Histogram histogram(const QVector<double> &values);

struct Spectrum {
	QVector<double> frequency, amplitude; /* Hz; the line's unit */
	double rate = 0;     /* the even rate resampled to, Hz */
	int segment = 0;     /* samples per segment (a power of two) */
	int segments = 0;    /* averaged */
	double resolution() const { return segment > 0 ? rate / segment : 0; } /* Hz between two frequencies */
};
/* MIN_SAMPLES at least, times rising; the segment a power of two up to MAX_SEGMENT, with at least about 8
 * segments when there are samples enough */
constexpr int MIN_SAMPLES = 16;
constexpr int MAX_SEGMENT = 1 << 16;
Spectrum spectrum(const QVector<double> &times, const QVector<double> &values);

} // namespace analysis
