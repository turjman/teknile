/* SPDX-License-Identifier: Apache-2.0 */
/* A line's samples analysed (model/analysis.h) in a small window of its own:
 * its histogram or its spectrum over A -> B (or the view), drawn on a plot of
 * its own, a readout under the mouse (the bin, or the frequency, and its
 * value), the picture copied or saved, the numbers exported as CSV. Opened
 * from a right-click on the line's chip in the chart's legend; the chart goes
 * on, the window keeps what it was given. */
#pragma once

#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>

#include "model/analysis.h"

class QCheckBox;
class QLabel;

class AnalysisWindow : public QWidget {
	Q_OBJECT
public:
	enum class Kind { Histogram, Spectrum };
	/* name, unit, colour: the line's; span: what was measured, in words ("A → B, 2.500 s"); even: the samples are
	 * evenly spaced (a fast line's), the spectrum takes them as they are */
	AnalysisWindow(Kind kind, const QString &name, const QString &unit, const QColor &color, const QString &span,
			const QVector<double> &times, const QVector<double> &values, QWidget *parent = nullptr, bool even = false);

	Kind kind() const { return kind_; }
	const analysis::Histogram &histogram() const { return histogram_; }
	const analysis::Spectrum &spectrum() const { return spectrum_; }
	QString summary() const;          /* the line above the plot: what was computed, and how */
	/* the readout under the mouse at x (the plot's coordinates; tests), and as shown now (empty: the mouse is away) */
	QString readoutAt(double x) const;
	QString readout() const;
	void setLogScale(bool on);        /* the spectrum's amplitude on a log scale */
	QWidget *plot() const;

	QImage picture() const;           /* the plot as shown */
	void copyPicture() const;
	bool savePicture(const QString &file) const;
	/* histogram: from, to, count, share; spectrum: frequency, amplitude */
	bool exportCsv(const QString &file, QString &error) const;

private:
	class Plot;
	Kind kind_;
	QString name_, unit_;
	QColor color_;
	analysis::Histogram histogram_;
	analysis::Spectrum spectrum_;
	Plot *plot_ = nullptr;
	QLabel *summary_ = nullptr;
	QCheckBox *log_ = nullptr;
	qint64 samples_ = 0;
};
