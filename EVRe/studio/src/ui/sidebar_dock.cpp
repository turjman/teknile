/* SPDX-License-Identifier: Apache-2.0 */
/* The sidebar's fold and its thin bar: see sidebar_dock.h. */
#include "ui/sidebar_dock.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSettings>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

#include "ui/fold_button.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

const QString FOLDED_KEY = QStringLiteral("ui/sidebarFolded");
constexpr int ICON_BOX = 32; /* the bar's buttons, as the fold button */

enum class Icon { Connection, Devices, Map, Fast, Polling, Api, Language, Help };

/* a colour a share of the way toward another: the lit fill, a step from the bar's */
QColor toward(const QColor &from, const QColor &to, qreal share) {
	return QColor::fromRgbF(float(from.redF() + (to.redF() - from.redF()) * share),
			float(from.greenF() + (to.greenF() - from.greenF()) * share),
			float(from.blueF() + (to.blueF() - from.blueF()) * share));
}

/* An icon in a 24-unit square drawn with 2-unit lines, as the owner's preview has them: a plug, two devices, a map,
 * a bolt, a record mark, code brackets, a globe, a question mark */
QPainterPath iconPath(Icon icon) {
	QPainterPath path;
	switch (icon) {
	case Icon::Connection: /* a plug: two pins, the body, the cable */
		path.moveTo(9, 7);
		path.lineTo(9, 3);
		path.moveTo(15, 7);
		path.lineTo(15, 3);
		path.moveTo(7, 7);
		path.lineTo(17, 7);
		path.lineTo(17, 11);
		path.arcTo(QRectF(7, 6, 10, 10), 0, -180);
		path.closeSubpath();
		path.moveTo(12, 16);
		path.lineTo(12, 21);
		break;
	case Icon::Devices:
		path.addRoundedRect(QRectF(4, 4, 16, 6), 1.5, 1.5);
		path.addRoundedRect(QRectF(4, 14, 16, 6), 1.5, 1.5);
		break;
	case Icon::Map: /* a folded map */
		path.addPolygon(QPolygonF({ QPointF(4, 6), QPointF(9, 4), QPointF(15, 6), QPointF(20, 4), QPointF(20, 18),
				QPointF(15, 20), QPointF(9, 18), QPointF(4, 20) }));
		path.closeSubpath();
		path.moveTo(9, 4);
		path.lineTo(9, 18);
		path.moveTo(15, 6);
		path.lineTo(15, 20);
		break;
	case Icon::Fast: /* a bolt */
		path.addPolygon(QPolygonF({ QPointF(13, 3), QPointF(5, 14), QPointF(11, 14), QPointF(10, 21), QPointF(18, 10),
				QPointF(12, 10) }));
		path.closeSubpath();
		break;
	case Icon::Polling: /* a ring around a dot (the dot filled by the caller) */
		path.addEllipse(QPointF(12, 12), 8, 8);
		break;
	case Icon::Api: /* < > */
		path.moveTo(8, 7);
		path.lineTo(3, 12);
		path.lineTo(8, 17);
		path.moveTo(16, 7);
		path.lineTo(21, 12);
		path.lineTo(16, 17);
		break;
	case Icon::Language: /* a globe */
		path.addEllipse(QPointF(12, 12), 9, 9);
		path.moveTo(3, 12);
		path.lineTo(21, 12);
		path.moveTo(12, 3);
		path.cubicTo(15, 6.5, 15, 17.5, 12, 21);
		path.moveTo(12, 3);
		path.cubicTo(9, 6.5, 9, 17.5, 12, 21);
		break;
	case Icon::Help: /* a question mark in a ring (its dot filled by the caller) */
		path.addEllipse(QPointF(12, 12), 9, 9);
		path.moveTo(9.5, 9.5);
		path.arcTo(QRectF(9.5, 7, 5, 5), 180, -248);
		path.cubicTo(12.3, 12.1, 12, 12.7, 12, 13.5);
		break;
	}
	return path;
}

/* the bar's button for a card, Help or Language: the icon alone, lit under the mouse (a fill and the text's colour),
 * the accent's fill while pressed, a ring with the keyboard's focus */
class RailButton : public QAbstractButton {
public:
	RailButton(Icon icon, QWidget *parent) : QAbstractButton(parent), icon_(icon) {
		setCursor(Qt::PointingHandCursor);
		setFocusPolicy(Qt::StrongFocus);
		setAttribute(Qt::WA_Hover);
		setFixedSize(ICON_BOX, ICON_BOX);
	}

protected:
	void enterEvent(QEnterEvent *event) override {
		QAbstractButton::enterEvent(event);
		update();
	}
	void leaveEvent(QEvent *event) override {
		QAbstractButton::leaveEvent(event);
		update();
	}
	void paintEvent(QPaintEvent *) override {
		const ThemeColors &c = Theme::colors();
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const bool pressed = isDown(), lit = underMouse() && !pressed;
		const bool ring = property("keyFocus").toBool() && hasFocus();
		const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		if (pressed || lit || ring) {
			p.setPen(ring ? QPen(c.text, 1.5) : Qt::NoPen);
			p.setBrush(pressed ? c.accentFill : lit ? toward(c.surface, c.text, 0.1) : Qt::transparent);
			p.drawRoundedRect(box, 7, 7);
		}
		const QColor ink = pressed ? QColor(Qt::white) : lit ? c.text : c.muted;
		const qreal icon = ICON_BOX * 0.625, scale = icon / 24.0;
		p.translate((width() - icon) / 2.0, (height() - icon) / 2.0);
		p.scale(scale, scale);
		p.setPen(QPen(ink, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPath(iconPath(icon_));
		/* the filled parts: the record mark's dot, the question mark's dot */
		p.setPen(Qt::NoPen);
		p.setBrush(ink);
		if (icon_ == Icon::Polling) p.drawEllipse(QPointF(12, 12), 3, 3);
		if (icon_ == Icon::Help) p.drawEllipse(QPointF(12, 17), 1.25, 1.25);
	}

private:
	Icon icon_;
};

/* the link's dot: the pill's colour, a faint ring of it around */
class RailDot : public QWidget {
public:
	explicit RailDot(QWidget *parent) : QWidget(parent) { setFixedSize(ICON_BOX, 24); }
	void setState(const QString &state) {
		if (state == state_) return;
		state_ = state;
		setProperty("state", state);
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override {
		const ThemeColors &c = Theme::colors();
		const QColor colour = state_ == QLatin1String("ok") ? c.good : state_ == QLatin1String("busy") ? c.warn
				: state_ == QLatin1String("error") ? c.bad : state_ == QLatin1String("listen") ? c.listen : c.muted;
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QPointF centre(width() / 2.0, height() / 2.0);
		QColor halo = colour;
		halo.setAlphaF(0.22f);
		p.setPen(Qt::NoPen);
		p.setBrush(halo);
		p.drawEllipse(centre, 9, 9);
		p.setBrush(colour);
		p.drawEllipse(centre, 6, 6);
	}

private:
	QString state_;
};

} // namespace

/* ------------------------------------------------------------- the thin bar */

SidebarRail::SidebarRail(QWidget *parent) : QWidget(parent) {
	setObjectName(QStringLiteral("sidebarRail"));
	open_ = new FoldButton(ICON_BOX, this);
	open_->setObjectName(QStringLiteral("railOpen"));
	open_->setPoints(FoldButton::Points::End); /* away from the window's edge: where the sidebar comes from */
	open_->setToolTip(tr("Open the sidebar (Ctrl+B)"));
	open_->setAccessibleName(open_->toolTip());
	QSizePolicy kept = open_->sizePolicy();
	kept.setRetainSizeWhenHidden(true); /* hidden while it slides: the icons under it stay */
	open_->setSizePolicy(kept);
	connect(open_, &FoldButton::clicked, this, &SidebarRail::openClicked);
	dot_ = new RailDot(this);
	dot_->setObjectName(QStringLiteral("railDot"));
	const struct {
		Sidebar::Card card;
		Icon icon;
		const char *name;
		QString tip;
	} cards[] = {
		{ Sidebar::Card::Connection, Icon::Connection, "railConnection", tr("Connection") },
		{ Sidebar::Card::Devices, Icon::Devices, "railDevices", tr("Devices on the link") },
		{ Sidebar::Card::Map, Icon::Map, "railMap", tr("Device map") },
		{ Sidebar::Card::Fast, Icon::Fast, "railFast", tr("Fast streams") },
		{ Sidebar::Card::Polling, Icon::Polling, "railPolling", tr("Polling & recording") },
		{ Sidebar::Card::Api, Icon::Api, "railApi", tr("API server") },
		{ Sidebar::Card::Language, Icon::Language, "railLanguage", tr("Language") },
	};
	for (const auto &item : cards) {
		auto *button = new RailButton(item.icon, this);
		button->setObjectName(QLatin1String(item.name));
		button->setToolTip(item.tip);
		button->setAccessibleName(item.tip);
		const Sidebar::Card card = item.card;
		connect(button, &QAbstractButton::clicked, this, [this, card] { emit cardClicked(card); });
		cards_[int(card)] = button;
	}
	cards_[int(Sidebar::Card::Fast)]->hide(); /* until a map with streams */
	help_ = new RailButton(Icon::Help, this);
	help_->setObjectName(QStringLiteral("railHelp"));
	help_->setToolTip(tr("Help (F1)"));
	help_->setAccessibleName(help_->toolTip());
	connect(help_, &QAbstractButton::clicked, this, &SidebarRail::helpClicked);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 10, 0, 10);
	layout->setSpacing(4);
	layout->addWidget(open_, 0, Qt::AlignHCenter);
	layout->addWidget(dot_, 0, Qt::AlignHCenter);
	for (const Sidebar::Card card : { Sidebar::Card::Connection, Sidebar::Card::Devices, Sidebar::Card::Map,
				 Sidebar::Card::Fast, Sidebar::Card::Polling, Sidebar::Card::Api })
		layout->addWidget(cards_[int(card)], 0, Qt::AlignHCenter);
	layout->addStretch(1);
	layout->addWidget(help_, 0, Qt::AlignHCenter);
	layout->addWidget(cards_[int(Sidebar::Card::Language)], 0, Qt::AlignHCenter);
	showLink(QStringLiteral("idle"), QString(), QString());
}

void SidebarRail::showLink(const QString &state, const QString &text, const QString &rate) {
	static_cast<RailDot *>(dot_)->setState(state);
	QStringList lines;
	if (!text.isEmpty()) lines << text;
	if (!rate.isEmpty() && rate != text) lines << rate;
	const QString tip = lines.join(QLatin1Char('\n'));
	if (dot_->toolTip() != tip) dot_->setToolTip(tip);
}

void SidebarRail::setCardShown(Sidebar::Card card, bool shown) {
	if (cards_[int(card)]) cards_[int(card)]->setVisible(shown);
}

/* the sidebar's own panel colour, and its edge on the side of the tabs */
void SidebarRail::paintEvent(QPaintEvent *) {
	const ThemeColors &c = Theme::colors();
	QPainter p(this);
	p.fillRect(rect(), c.surface);
	const int x = isRightToLeft() ? 0 : width() - 1;
	p.fillRect(QRect(x, 0, 1, height()), c.border);
	/* a line over Help and Language: the foot apart from the cards, as in the sidebar */
	const int above = help_->geometry().top() - 6;
	p.fillRect(QRect(width() / 2 - 12, above, 24, 1), c.border);
}

/* ----------------------------------------------------------------- the dock */

SidebarDock::SidebarDock(Sidebar *sidebar, QWidget *parent) : QWidget(parent), sidebar_(sidebar) {
	setObjectName(QStringLiteral("sidebarDock"));
	sidebar_->setParent(this);
	rail_ = new SidebarRail(this);
	sidebar_->raise(); /* over the bar while it slides: the bar comes out from under its edge */
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

	slide_ = new QVariantAnimation(this);
	slide_->setEasingCurve(QEasingCurve::InOutCubic);
	connect(slide_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { setProgress(value.toDouble()); });
	connect(slide_, &QVariantAnimation::finished, this, [this] { arrived(true); });
	fade_ = new QVariantAnimation(this);
	fade_->setDuration(FADE_MS);
	fade_->setStartValue(0.0);
	fade_->setEndValue(1.0);
	connect(fade_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
		if (fading_) fading_->setOpacity(value.toDouble());
	});

	connect(sidebar_, &Sidebar::foldClicked, this, [this] { setFolded(true); });
	connect(rail_, &SidebarRail::openClicked, this, [this] { setFolded(false); });
	connect(rail_, &SidebarRail::cardClicked, this, &SidebarDock::openAt);
	connect(rail_, &SidebarRail::helpClicked, this, &SidebarDock::helpClicked);
	connect(sidebar_, &Sidebar::linkShown, this, [this] {
		rail_->showLink(sidebar_->linkStateName(), sidebar_->linkStateText(), sidebar_->pollRateText());
	});
	connect(sidebar_, &Sidebar::cardsChanged, this, [this] {
		QWidget *fast = sidebar_->cardWidget(Sidebar::Card::Fast);
		rail_->setCardShown(Sidebar::Card::Fast, fast && !fast->isHidden());
	});

	folded_ = QSettings().value(FOLDED_KEY, false).toBool();
	setProgress(folded_ ? 1 : 0);
	arrived(false);
}

bool SidebarDock::sliding() const { return slide_->state() == QAbstractAnimation::Running; }

int SidebarDock::widthAt(double progress) const {
	const int open = sidebar_->width();
	return int(std::lround(open - (open - RAIL_W) * progress));
}

void SidebarDock::setProgress(double progress) {
	progress_ = std::clamp(progress, 0.0, 1.0);
	setFixedWidth(widthAt(progress_));
	place();
}

/* the sidebar moved out over the window's edge by the share folded, the bar at the dock's inner end: at the start
 * under the sidebar, at the end the whole dock */
void SidebarDock::place() {
	const int open = sidebar_->width();
	const QRect side(-int(std::lround(open * progress_)), 0, open, height());
	const QRect bar(width() - RAIL_W, 0, RAIL_W, height());
	sidebar_->setGeometry(QStyle::visualRect(layoutDirection(), rect(), side));
	rail_->setGeometry(QStyle::visualRect(layoutDirection(), rect(), bar));
}

void SidebarDock::resizeEvent(QResizeEvent *event) {
	QWidget::resizeEvent(event);
	place();
}

void SidebarDock::changeEvent(QEvent *event) {
	QWidget::changeEvent(event);
	if (event->type() == QEvent::LayoutDirectionChange) place();
}

QSize SidebarDock::sizeHint() const { return QSize(widthAt(progress_), sidebar_->sizeHint().height()); }

/* as tall as the sidebar asks (it scrolls); the bar is cut on a window shorter than its icons */
QSize SidebarDock::minimumSizeHint() const {
	return QSize(widthAt(progress_), sidebar_->minimumSizeHint().height());
}

void SidebarDock::setFolded(bool folded) {
	if (folded == folded_) return;
	folded_ = folded;
	QSettings().setValue(FOLDED_KEY, folded);
	/* the focus on the side leaving goes to the arriving side's button once it is there (with its ring, when it came
	 * from the keyboard) */
	QWidget *leaving = folded ? static_cast<QWidget *>(sidebar_) : rail_;
	QWidget *focus = QApplication::focusWidget();
	refocus_ = focus && leaving->isAncestorOf(focus) ? (focus->property("keyFocus").toBool() ? 2 : 1) : 0;
	fade_->stop();
	fading_ = nullptr;
	sidebar_->foldButton()->hide();
	rail_->openButton()->hide();
	sidebar_->show();
	rail_->show();
	const double target = folded ? 1.0 : 0.0;
	slide_->stop();
	if (reducedMotion() || !isVisible()) {
		setProgress(target);
		arrived(false);
	} else {
		slide_->setDuration(std::max(1, int(std::lround(SLIDE_MS * std::abs(target - progress_)))));
		slide_->setStartValue(progress_);
		slide_->setEndValue(target);
		slide_->start();
	}
	emit foldedChanged(folded);
}

void SidebarDock::arrived(bool fade) {
	sidebar_->setVisible(!folded_);
	rail_->setVisible(folded_);
	FoldButton *button = folded_ ? rail_->openButton() : sidebar_->foldButton();
	fade = fade && !reducedMotion();
	button->setOpacity(fade ? 0.0 : 1.0);
	button->show();
	if (refocus_) button->setFocus(refocus_ == 2 ? Qt::TabFocusReason : Qt::OtherFocusReason);
	refocus_ = 0;
	if (fade) {
		fading_ = button;
		fade_->start();
	}
}

void SidebarDock::openAt(Sidebar::Card card) {
	setFolded(false);
	/* once the sidebar is shown and laid out: it slides in with the card already in view */
	QTimer::singleShot(0, sidebar_, [sidebar = sidebar_, card] { sidebar->showCard(card); });
}
