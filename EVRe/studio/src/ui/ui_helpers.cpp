/* SPDX-License-Identifier: Apache-2.0 */
/* The shared helpers of the window: see ui_helpers.h. */
#include "ui/ui_helpers.h"

#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {

QString secondsPlain(double seconds) {
	const auto number = [](double v) { return QString::number(v, 'g', 4); };
	if (seconds < 1e-3) return QStringLiteral("%1 µs").arg(number(seconds * 1e6));
	if (seconds < 1) return QStringLiteral("%1 ms").arg(number(seconds * 1000));
	if (seconds < 60 || std::fmod(seconds, 60) != 0) return QStringLiteral("%1 s").arg(number(seconds));
	if (seconds < 3600 || std::fmod(seconds, 3600) != 0) return QStringLiteral("%1 min").arg(number(seconds / 60));
	return QStringLiteral("%1 h").arg(number(seconds / 3600));
}

QString durationPlain(double seconds) {
	const double s = std::fabs(seconds);
	/* 4 significant digits, in the first unit where they stay below 1000 (999.96 ms is "1 s", not "1000 ms") */
	const auto below = [](double v, double limit) { return QString::number(v, 'g', 4).toDouble() < limit; };
	if (below(s * 1e6, 1000)) return QStringLiteral("%1 µs").arg(QString::number(s * 1e6, 'g', 4));
	if (below(s * 1e3, 1000)) return QStringLiteral("%1 ms").arg(QString::number(s * 1e3, 'g', 4));
	if (below(s, 60)) return QStringLiteral("%1 s").arg(QString::number(s, 'g', 4));
	const qint64 tenths = std::llround(s * 10);
	if (tenths < 36000)
		return QStringLiteral("%1 min %2 s").arg(tenths / 600).arg((tenths % 600) / 10.0, 0, 'f', 1);
	const qint64 minutes = std::llround(s / 60);
	return QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

QString ltrPiece(const QString &text) {
	if (text.isEmpty() || !QGuiApplication::isRightToLeft()) return text;
	return QChar(0x2066) + text + QChar(0x2069);
}

QString secondsText(double seconds) { return ltrPiece(secondsPlain(seconds)); }

QString durationText(double seconds) { return ltrPiece(durationPlain(seconds)); }

double parseSeconds(const QString &text) {
	static const QRegularExpression length(QStringLiteral("^\\s*([0-9]*[.,]?[0-9]+)\\s*(us|µs|ms|s|sec|m|min|h)?\\s*$"),
			QRegularExpression::CaseInsensitiveOption);
	QString plain = text;
	plain.remove(QChar(0x2066)).remove(QChar(0x2069)); /* the isolates of a right-to-left window's own texts */
	const QRegularExpressionMatch match = length.match(plain);
	if (!match.hasMatch()) return -1;
	const double value = match.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
	const QString unit = match.captured(2).toLower();
	if (unit == QLatin1String("us") || unit == QStringLiteral("µs")) return value / 1e6;
	if (unit == QLatin1String("ms")) return value / 1000;
	if (unit == QLatin1String("m") || unit == QLatin1String("min")) return value * 60;
	if (unit == QLatin1String("h")) return value * 3600;
	return value;
}

QString noMnemonic(QString text) { return text.replace(QLatin1Char('&'), QStringLiteral("&&")); }

QString coloredSpan(const QString &html, const QColor &color) {
	return QStringLiteral("<span style='color:%1'>%2</span>").arg(color.name(), html);
}

QString mapsFolder() { return QCoreApplication::applicationDirPath() + QStringLiteral("/maps"); }

QWidget *card(const QString &title, QLayout *content) {
	auto *frame = new QFrame;
	frame->setObjectName(QStringLiteral("card"));
	auto *layout = new QVBoxLayout(frame);
	layout->setContentsMargins(14, 12, 14, 14);
	layout->setSpacing(8);
	auto *heading = new QLabel(title.toUpper());
	heading->setObjectName(QStringLiteral("cardTitle"));
	layout->addWidget(heading);
	layout->addLayout(content);
	return frame;
}

QLabel *mutedLabel(const QString &text) {
	auto *label = new QLabel(text);
	label->setObjectName(QStringLiteral("muted"));
	return label;
}

QPushButton *segmentButton(const QString &text, const char *position) {
	auto *button = new QPushButton(text);
	button->setCheckable(true);
	button->setProperty("segment", true);
	button->setProperty("segmentPos", position);
	button->setCursor(Qt::PointingHandCursor);
	return button;
}

void setButtonMenu(QPushButton *button, QMenu *menu) {
	button->setMenu(menu);
	button->setProperty("menuButton", true);
}

bool confirmed(QWidget *parent, const QString &title, const QString &html, const QString &goAhead) {
	QMessageBox box(QMessageBox::Warning, title, html, QMessageBox::Cancel, parent);
	box.setTextFormat(Qt::RichText);
	const QPushButton *go = box.addButton(goAhead, QMessageBox::AcceptRole);
	box.setDefaultButton(QMessageBox::Cancel);
	box.exec();
	return box.clickedButton() == go;
}

void noWindowAnimation(QWidget *dialog) {
	dialog->setProperty("noAnimation", true);
#ifdef Q_OS_WIN
	const BOOL disabled = TRUE; /* winId: the dialog's window made now, so the attribute is there before it shows */
	DwmSetWindowAttribute(HWND(dialog->winId()), DWMWA_TRANSITIONS_FORCEDISABLED, &disabled, sizeof disabled);
#endif
}

void repolish(QWidget *widget) {
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}

void setHighlighted(QWidget *widget, bool on, const QColor &color) {
	const QString bold = QStringLiteral("color:%1; font-weight:600").arg(color.name());
	/* as wide as in bold, measured in bold once (the font's 600 may be drawn wider than a metric says): the
	 * bold text would widen it and push the row beside it */
	if (!widget->property("highlightWidth").isValid()) {
		widget->ensurePolished(); /* the application's font and style sheet, as it will be shown */
		widget->setStyleSheet(bold);
		const int width = widget->sizeHint().width();
		widget->setStyleSheet(QString());
		widget->setMinimumWidth(std::max(width, widget->sizeHint().width()));
		widget->setProperty("highlightWidth", width);
	}
	widget->setStyleSheet(on ? bold : QString());
}

QFont monospaceFont() {
	QFont mono(QStringLiteral("Cascadia Mono"));
	mono.setStyleHint(QFont::Monospace);
	mono.setPointSize(9);
	return mono;
}

QIcon mediaIcon(MediaIcon kind, const QColor &color, int size) {
	/* drawn at the screen's pixel ratio: sharp on a 4K display too */
	const qreal ratio = qApp ? qApp->devicePixelRatio() : 1.0;
	QPixmap pixmap(int(size * ratio), int(size * ratio));
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);
	QPainter p(&pixmap);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const qreal s = size, margin = s * 0.12;
	if (kind == MediaIcon::Pause) {
		const qreal bar = s * 0.26, gap = s * 0.2;
		const qreal left = (s - 2 * bar - gap) / 2;
		p.drawRoundedRect(QRectF(left, margin, bar, s - 2 * margin), 1.2, 1.2);
		p.drawRoundedRect(QRectF(left + bar + gap, margin, bar, s - 2 * margin), 1.2, 1.2);
	} else {
		QPainterPath triangle;
		triangle.moveTo(s * 0.2, margin);
		triangle.lineTo(s * 0.88, s / 2);
		triangle.lineTo(s * 0.2, s - margin);
		triangle.closeSubpath();
		p.drawPath(triangle);
	}
	return QIcon(pixmap);
}

QIcon refreshIcon(const QColor &color, int size) {
	const qreal ratio = qApp ? qApp->devicePixelRatio() : 1.0;
	QPixmap pixmap(int(size * ratio), int(size * ratio));
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);
	QPainter p(&pixmap);
	p.setRenderHint(QPainter::Antialiasing);
	const qreal s = size, pen = s * 0.11;
	const QRectF ring(s * 0.2, s * 0.2, s * 0.6, s * 0.6);
	const qreal r = ring.width() / 2, cx = ring.center().x(), cy = ring.center().y();
	/* the arc: clockwise from 30 degrees (the right, a little up) round to the top, 300 degrees; the gap at the top
	 * right is where the arrow points */
	p.setPen(QPen(color, pen, Qt::SolidLine, Qt::FlatCap));
	p.setBrush(Qt::NoBrush);
	p.drawArc(ring, 30 * 16, -300 * 16);
	/* the arrowhead at the arc's end (the top), along the turn: clockwise on screen is (sin a, cos a) */
	const qreal a = M_PI / 2, head = s * 0.34;
	const QPointF end(cx + r * std::cos(a), cy - r * std::sin(a));
	const QPointF along(std::sin(a), std::cos(a)), out(std::cos(a), -std::sin(a));
	QPainterPath arrow;
	arrow.moveTo(end + along * head * 0.55);
	arrow.lineTo(end - along * head * 0.15 + out * head * 0.5);
	arrow.lineTo(end - along * head * 0.15 - out * head * 0.5);
	arrow.closeSubpath();
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawPath(arrow);
	return QIcon(pixmap);
}

QIcon warningIcon(const QColor &color, int size) {
	const qreal ratio = qApp ? qApp->devicePixelRatio() : 1.0;
	QPixmap pixmap(int(size * ratio), int(size * ratio));
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);
	QPainter p(&pixmap);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(color, size * 0.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(color);
	const qreal s = size;
	QPainterPath triangle;
	triangle.moveTo(s / 2, s * 0.08);
	triangle.lineTo(s * 0.95, s * 0.9);
	triangle.lineTo(s * 0.05, s * 0.9);
	triangle.closeSubpath();
	p.drawPath(triangle);
	/* the mark cut out: the tab's background shows through it */
	p.setCompositionMode(QPainter::CompositionMode_Clear);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::black);
	const qreal bar = s * 0.13;
	p.drawRoundedRect(QRectF(s / 2 - bar / 2, s * 0.34, bar, s * 0.3), bar / 2, bar / 2);
	p.drawEllipse(QRectF(s / 2 - bar / 2, s * 0.7, bar, bar));
	return QIcon(pixmap);
}

QIcon studioIcon() {
	QIcon icon;
	for (const int size : { 16, 24, 32, 48, 64, 128, 256 })
		icon.addFile(QStringLiteral(":/icons/evre-studio-%1.png").arg(size), QSize(size, size));
	return icon;
}

QIcon stateDot(const QColor &color, qreal ratio) {
	QPixmap pixmap(QSize(10, 10) * ratio);
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(color);
	painter.drawEllipse(QRectF(1, 1, 8, 8));
	return QIcon(pixmap);
}

void fillDevicePicker(QComboBox *box, const QVector<PickerDevice> &devices, int current) {
	const QSignalBlocker blocker(box);
	box->setIconSize(QSize(10, 10));
	/* room for "D12 · slave 255" and "Broadcast · slave 0"; the dot, the box's padding and its arrow (theme.cpp)
	 * beside it */
	const QFontMetrics metrics(box->font());
	const int textRoom = std::max(metrics.horizontalAdvance(QObject::tr("Broadcast · slave 0")),
			metrics.horizontalAdvance(QObject::tr("%1 · slave %2").arg(QStringLiteral("D12345")).arg(255)));
	box->setFixedWidth(textRoom + 64);
	const bool inPlace = box->count() == devices.size();
	if (!inPlace) box->clear();
	const qreal ratio = box->devicePixelRatioF();
	for (int i = 0; i < devices.size(); i++) {
		const PickerDevice &device = devices[i];
		const bool isDevice = device.dot.isValid();
		const QString slave = QObject::tr(" · slave %1").arg(device.slave);
		const QString full = isDevice ? device.name + slave : device.name;
		/* a long name cut, never its slave address */
		const QString text = metrics.horizontalAdvance(full) <= textRoom ? full : isDevice
				? metrics.elidedText(device.name, Qt::ElideRight, textRoom - metrics.horizontalAdvance(slave)) + slave
				: metrics.elidedText(device.name, Qt::ElideRight, textRoom);
		/* a choice that is no device gets an empty dot: its text starts where the devices' names do */
		const QIcon icon = stateDot(isDevice ? device.dot : QColor(Qt::transparent), ratio);
		if (inPlace) {
			box->setItemText(i, text);
			box->setItemIcon(i, icon);
			box->setItemData(i, device.slave);
		} else {
			box->addItem(icon, text, device.slave);
		}
		box->setItemData(i, text == full ? device.state : full + QLatin1Char('\n') + device.state, Qt::ToolTipRole);
	}
	box->setCurrentIndex(std::max(0, box->findData(current)));
}
