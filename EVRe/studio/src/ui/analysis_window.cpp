/* SPDX-License-Identifier: Apache-2.0 */
/* A line's histogram or spectrum: see analysis_window.h. */
#include "ui/analysis_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "ui/chart_widget.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

/* the plot inside its widget: the value labels left, the readout over it, the axis labels under it */
constexpr double LEFT = 64, TOP = 30, RIGHT = 18, BOTTOM = 30;
constexpr double MARGIN = 0.08;     /* over the tallest bar or peak */
constexpr double LOG_DECADES = 6;   /* the spectrum's log scale: this far under its peak */

QString number(double v) { return QString::number(v, 'g', 4); }

/* 1, 2, 5 x 10^n steps, about `target` of them over span */
double step(double span, int target) {
	if (!(span > 0)) return 1;
	const double raw = span / target, magnitude = std::pow(10.0, std::floor(std::log10(raw))), n = raw / magnitude;
	return (n < 1.5 ? 1 : n < 3.5 ? 2 : n < 7.5 ? 5 : 10) * magnitude;
}

} // namespace

/* the plot: bars (histogram) or a line (spectrum), its grid and labels, a dashed line and the readout at the mouse */
class AnalysisWindow::Plot : public QWidget {
public:
	explicit Plot(AnalysisWindow *owner) : QWidget(owner), w_(owner) {
		setMouseTracking(true);
		setMinimumSize(420, 260);
		setAttribute(Qt::WA_OpaquePaintEvent);
	}
	bool log = false;
	double mouseX = -1;

	QRectF area() const { return QRectF(rect()).adjusted(LEFT, TOP, -RIGHT, -BOTTOM); }
	void xRange(double &lo, double &hi) const {
		if (w_->kind_ == Kind::Histogram) {
			lo = w_->histogram_.from;
			hi = w_->histogram_.binTo(int(std::max<qsizetype>(1, w_->histogram_.counts.size())) - 1);
		} else {
			lo = 0;
			hi = std::max(w_->spectrum_.rate / 2, 1e-9);
		}
	}
	/* the values' range; log: decades (the spectrum) */
	void yRange(double &lo, double &hi, bool &logScale) const {
		double top = 0;
		if (w_->kind_ == Kind::Histogram)
			for (qint64 c : w_->histogram_.counts) top = std::max(top, double(c));
		else
			for (double a : w_->spectrum_.amplitude) top = std::max(top, a);
		if (!(top > 0)) top = 1;
		logScale = log && w_->kind_ == Kind::Spectrum;
		if (logScale) {
			hi = std::log10(top) + LOG_DECADES * MARGIN;
			lo = std::log10(top) - LOG_DECADES;
		} else {
			lo = 0;
			hi = top * (1 + MARGIN);
		}
	}
	double toX(double v) const {
		double lo, hi;
		xRange(lo, hi);
		return area().left() + (v - lo) / (hi - lo) * area().width();
	}
	double fromX(double x) const {
		double lo, hi;
		xRange(lo, hi);
		return lo + (x - area().left()) / area().width() * (hi - lo);
	}
	double toY(double v) const {
		double lo, hi;
		bool logScale;
		yRange(lo, hi, logScale);
		if (logScale) v = v > 0 ? std::log10(v) : lo;
		return area().bottom() - std::clamp((v - lo) / (hi - lo), 0.0, 1.0) * area().height();
	}

protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		const ThemeColors &c = Theme::colors();
		p.fillRect(rect(), c.surface);
		const QRectF a = area();
		QFont small = font();
		small.setPointSizeF(8.5);
		p.setFont(small);
		/* the grid and its labels */
		double xLo, xHi, yLo, yHi;
		bool logScale;
		xRange(xLo, xHi);
		yRange(yLo, yHi, logScale);
		const double xs = step(xHi - xLo, std::max(2, int(a.width() / 110)));
		for (double v = std::ceil(xLo / xs) * xs; v <= xHi + xs * 1e-9; v += xs) {
			const double x = toX(v);
			p.setPen(QPen(c.grid, 1));
			p.drawLine(QPointF(x, a.top()), QPointF(x, a.bottom()));
			p.setPen(c.muted);
			p.drawText(QRectF(x - 50, a.bottom() + 6, 100, 16), Qt::AlignCenter, chartAxisLabel(v, xs, false));
		}
		if (logScale) {
			for (int d = int(std::ceil(yLo)); d <= int(std::floor(yHi)); d++) {
				const double y = toY(std::pow(10.0, d));
				p.setPen(QPen(c.grid, 1));
				p.drawLine(QPointF(a.left(), y), QPointF(a.right(), y));
				p.setPen(c.muted);
				p.drawText(QRectF(2, y - 8, LEFT - 8, 16), Qt::AlignRight | Qt::AlignVCenter, chartLogLabel(std::pow(10.0, d)));
			}
		} else {
			const double ys = step(yHi - yLo, std::max(2, int(a.height() / 60)));
			for (double v = 0; v <= yHi + ys * 1e-9; v += ys) {
				const double y = toY(v);
				p.setPen(QPen(c.grid, 1));
				p.drawLine(QPointF(a.left(), y), QPointF(a.right(), y));
				p.setPen(c.muted);
				p.drawText(QRectF(2, y - 8, LEFT - 8, 16), Qt::AlignRight | Qt::AlignVCenter, chartAxisLabel(v, ys, false));
			}
		}
		/* what was computed */
		p.setRenderHint(QPainter::Antialiasing);
		p.save();
		p.setClipRect(a);
		if (w_->kind_ == Kind::Histogram) {
			QColor fill = w_->color_;
			fill.setAlpha(170);
			p.setPen(QPen(w_->color_, 1));
			p.setBrush(fill);
			const analysis::Histogram &h = w_->histogram_;
			for (int k = 0; k < h.counts.size(); k++) {
				if (h.counts[k] == 0) continue;
				p.drawRect(QRectF(QPointF(toX(h.binFrom(k)), toY(double(h.counts[k]))), QPointF(toX(h.binTo(k)), a.bottom())));
			}
		} else {
			const analysis::Spectrum &s = w_->spectrum_;
			QPolygonF line;
			for (qsizetype j = 0; j < s.frequency.size(); j++) line << QPointF(toX(s.frequency[j]), toY(s.amplitude[j]));
			p.setPen(QPen(w_->color_, 1.5));
			p.drawPolyline(line);
		}
		p.restore();
		/* the readout at the mouse */
		const QString text = w_->readout();
		if (!text.isEmpty()) {
			p.setPen(QPen(c.muted, 1, Qt::DashLine));
			p.drawLine(QPointF(mouseX, a.top()), QPointF(mouseX, a.bottom()));
			p.setPen(c.text);
			p.drawText(QRectF(a.left(), 4, a.width(), TOP - 8), Qt::AlignLeft | Qt::AlignVCenter, text);
		} else if (w_->kind_ == Kind::Spectrum ? w_->spectrum_.frequency.isEmpty() : w_->histogram_.counts.isEmpty()) {
			p.setPen(c.muted);
			p.drawText(a, Qt::AlignCenter, QCoreApplication::translate("AnalysisWindow", "Too few samples (16 at least for a spectrum)"));
		}
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		mouseX = e->position().x();
		update();
	}
	bool event(QEvent *e) override {
		if (e->type() == QEvent::Leave) {
			mouseX = -1;
			update();
		}
		return QWidget::event(e);
	}

private:
	AnalysisWindow *w_;
};

AnalysisWindow::AnalysisWindow(Kind kind, const QString &name, const QString &unit, const QColor &color,
		const QString &span, const QVector<double> &times, const QVector<double> &values, QWidget *parent)
	: QWidget(parent, Qt::Window), kind_(kind), name_(name), unit_(unit), color_(color), samples_(values.size()) {
	setAttribute(Qt::WA_DeleteOnClose);
	setObjectName(kind == Kind::Histogram ? QStringLiteral("histogramWindow") : QStringLiteral("spectrumWindow"));
	if (kind == Kind::Histogram) histogram_ = analysis::histogram(values);
	else spectrum_ = analysis::spectrum(times, values);
	setWindowTitle((kind == Kind::Histogram ? tr("Histogram of %1 — %2") : tr("Spectrum of %1 — %2")).arg(name, span));

	summary_ = mutedLabel(summary());
	summary_->setObjectName(QStringLiteral("analysisSummary"));
	summary_->setWordWrap(true);
	plot_ = new Plot(this);
	auto *copy = new QPushButton(tr("Copy picture"));
	connect(copy, &QPushButton::clicked, this, &AnalysisWindow::copyPicture);
	auto *save = new QPushButton(tr("Save picture…"));
	connect(save, &QPushButton::clicked, this, [this] {
		const QString file = QFileDialog::getSaveFileName(this, tr("Save picture"), QDir::homePath() + QStringLiteral("/%1.png")
				.arg(objectName()), tr("PNG (*.png)"));
		if (!file.isEmpty()) savePicture(file);
	});
	auto *csv = new QPushButton(tr("Export CSV…"));
	connect(csv, &QPushButton::clicked, this, [this] {
		const QString file = QFileDialog::getSaveFileName(this, tr("Export CSV"), QDir::homePath() + QStringLiteral("/%1.csv")
				.arg(objectName()), tr("CSV (*.csv)"));
		QString error;
		if (!file.isEmpty() && !exportCsv(file, error)) summary_->setText(tr("Not exported: %1").arg(error));
	});
	auto *row = new QHBoxLayout;
	row->addWidget(summary_, 1);
	if (kind == Kind::Spectrum) {
		log_ = new QCheckBox(tr("Log"));
		log_->setObjectName(QStringLiteral("spectrumLog"));
		log_->setToolTip(tr("The amplitude on a logarithmic scale: 6 decades under the peak"));
		connect(log_, &QCheckBox::toggled, this, &AnalysisWindow::setLogScale);
		row->addWidget(log_);
	}
	row->addWidget(copy);
	row->addWidget(save);
	row->addWidget(csv);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(14, 12, 14, 12);
	layout->addLayout(row);
	layout->addWidget(plot_, 1);
	resize(760, 460);
}

QWidget *AnalysisWindow::plot() const { return plot_; }

QString AnalysisWindow::summary() const {
	const QString u = unit_.isEmpty() ? QString() : QLatin1Char(' ') + unit_;
	if (kind_ == Kind::Histogram) {
		if (histogram_.counts.isEmpty()) return tr("No samples");
		return tr("%1 samples · %2 bins of %3%4 (Freedman–Diaconis)").arg(histogram_.total).arg(histogram_.counts.size())
				.arg(number(histogram_.width), u);
	}
	if (spectrum_.frequency.isEmpty()) return tr("%1 samples: too few for a spectrum").arg(samples_);
	qsizetype peak = 1;
	for (qsizetype j = 1; j < spectrum_.amplitude.size(); j++)
		if (spectrum_.amplitude[j] > spectrum_.amplitude[peak]) peak = j;
	return tr("%1 samples, resampled to %2 Hz · %3 segments of %4, Hann, 50 % overlap · %5 Hz apart · peak %6 Hz: %7%8")
			.arg(samples_).arg(number(spectrum_.rate)).arg(spectrum_.segments).arg(spectrum_.segment)
			.arg(number(spectrum_.resolution())).arg(number(spectrum_.frequency.value(peak)))
			.arg(number(spectrum_.amplitude.value(peak)), u);
}

QString AnalysisWindow::readoutAt(double x) const {
	if (x < plot_->area().left() || x > plot_->area().right()) return {};
	const double at = plot_->fromX(x);
	const QString u = unit_.isEmpty() ? QString() : QLatin1Char(' ') + unit_;
	if (kind_ == Kind::Histogram) {
		if (histogram_.counts.isEmpty()) return {};
		const int k = std::clamp(int(std::floor((at - histogram_.from) / histogram_.width)), 0, int(histogram_.counts.size()) - 1);
		return tr("%1 … %2%3: %4 samples (%5 %)").arg(number(histogram_.binFrom(k)), number(histogram_.binTo(k)), u)
				.arg(histogram_.counts[k]).arg(100.0 * double(histogram_.counts[k]) / double(histogram_.total), 0, 'f', 1);
	}
	if (spectrum_.frequency.isEmpty()) return {};
	const qsizetype j = std::clamp<qsizetype>(qsizetype(std::llround(at / std::max(spectrum_.resolution(), 1e-12))), 0,
			spectrum_.frequency.size() - 1);
	return tr("%1 Hz: %2%3").arg(number(spectrum_.frequency[j]), number(spectrum_.amplitude[j]), u);
}

QString AnalysisWindow::readout() const { return plot_->mouseX < 0 ? QString() : readoutAt(plot_->mouseX); }

void AnalysisWindow::setLogScale(bool on) {
	plot_->log = on;
	if (log_ && log_->isChecked() != on) log_->setChecked(on);
	plot_->update();
}

QImage AnalysisWindow::picture() const { return plot_->grab().toImage(); }

void AnalysisWindow::copyPicture() const { QApplication::clipboard()->setImage(picture()); }

bool AnalysisWindow::savePicture(const QString &file) const { return picture().save(file, "PNG"); }

bool AnalysisWindow::exportCsv(const QString &file, QString &error) const {
	QFile out(file);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
		error = out.errorString();
		return false;
	}
	QTextStream text(&out);
	const QString unit = unit_.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(unit_);
	if (kind_ == Kind::Histogram) {
		text << "from" << unit << ",to" << unit << ",count,share_percent\n";
		for (int k = 0; k < histogram_.counts.size(); k++)
			text << QString::number(histogram_.binFrom(k), 'g', 9) << ',' << QString::number(histogram_.binTo(k), 'g', 9)
				 << ',' << histogram_.counts[k] << ','
				 << QString::number(100.0 * double(histogram_.counts[k]) / double(histogram_.total), 'f', 3) << '\n';
	} else {
		text << "frequency [Hz],amplitude" << unit << '\n';
		for (qsizetype j = 0; j < spectrum_.frequency.size(); j++)
			text << QString::number(spectrum_.frequency[j], 'g', 9) << ',' << QString::number(spectrum_.amplitude[j], 'g', 9)
				 << '\n';
	}
	text.flush();
	if (text.status() != QTextStream::Ok) {
		error = out.errorString();
		return false;
	}
	return true;
}
