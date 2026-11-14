/* SPDX-License-Identifier: Apache-2.0 */
/* evre_phone_test: the phone's window (src/phone, Qt Quick) driven by QtTest, as the phone program loads it
 * (phone_app.cpp): Main.qml at a phone's portrait (412 x 915) and landscape (915 x 412), touched by QTest::touchEvent,
 * its pixels read from QQuickWindow::grabWindow() (an area scaled by the picture's devicePixelRatio: the owner's
 * Windows runs at 225 %). STUDIO.md 26.11.
 *
 *   python3 tests/fake_device.py --port 1210 --map maps/example_device.json &
 *   evre_phone_test
 *
 * The link's checks run over TCP against the fake device on 1210 (its DEVICE_ID 0x1001), against stand-ins of its own
 * (a proxy that drops the link, a device that refuses every request) and, for USB, a fake UsbBridge that speaks for
 * Android and the board. The maps beside it (the build copies maps/) give the session and P-9 their rows. Settings: its
 * own application (EVReStudioPhoneTest), cleared at the start. On Linux under xvfb it draws with Mesa through OpenGL
 * (QSG_RHI_BACKEND=opengl); the software adaptation would not draw the chart's nodes to come.
 *
 * The Registers screen's checks (B.2) run against the fake device too: its rows' values, a write read back, the limits
 * asked first, a refused write (a map with a register the device does not have), the glow in the picture; a stand-in that
 * leaves the first DEVICE_ID request unanswered checks that the ID is read again by itself. The chart's checks (B.3a)
 * feed its lines from the test (a sine at 1 kHz), compare its bins with the desktop chart's binning (chart_data.h),
 * find the line's colour in the picture, and pinch, drag and double tap it. The chart wired (B.3b): registers plotted
 * through the session and polled from the fake device, the legend's chips (the latest value, hide, Take off the
 * chart), Clear, Y auto and the manual sheet, the empty state; a fast stream's channel against evre_fake_fast with
 * maps/example_fast.json, started by the check on EVRE_FAKE_FAST_PORT (default 1213: 1221 to 1320 are often taken on
 * Windows).
 *
 * EVRE_PHONE_SHOT=<prefix> also saves the window's pictures for the eye: <prefix>_<en|ar>_<dark|light>_<name>.png,
 * the names portrait, drawer, help, helppage (the phone's own topic; helppage2 a screen down, helppage_end its end),
 * connected, drawerkb, log, registers, search, sheet, question, keyboard, enum and, at 915 x 412, land_registers,
 * land_sheet, land_drawer, land_log, landscape, then chartdemo and chartdemo_portrait (not checks; all but the Help's
 * and the two before them, and the chart's with the fake device, chartdemo with the
 * chart's test feed; drawerkb and keyboard with a keyboard stood for, drawn grey). EVRE_PHONE_ONLY=shots saves them
 * without running the checks.
 * EVRE_FAKE_PORT: the fake device on another port than 1210 (a second run on the same PC).
 * The design pass (part A): the Registers screen's rows counted in view against the cards before, a register's sheet
 * (its trend, Plot, Copy, the quick write by Enter, the number pad), the trend's bounded ring, the pill's poll rate and
 * chevron, the slimmer header, the empty Log and the empty search with their one button, no group header over a row
 * while the list scrolls.
 * The cursors and the Monitor (B.4): placed by the Cursors button, A dragged by its handle, the readouts against
 * statsOf; the fake device's frames, Pause, Clear, the empty state; texts in both languages and themes. Their pictures:
 * <prefix>_<en|ar>_<dark|light>_<cursors|cursors6|cursors6end|monitor|paused>_<portrait|landscape>.png.
 * The Chart screen's pictures too: <prefix>_<en|ar>_<dark|light>_chart_<empty|two|hidden|manual|ysheet|chipmenu|six|
 * sixend|regsheet>_<portrait|landscape>.png (six: six lines, the legend scrolling; sixend: scrolled to its end;
 * regsheet: a plotted register's sheet on the Registers screen). EVRE_PHONE_ONLY=<text> (other than shots): only the checks
 * whose function's name holds it, and with a prefix only the chart's pictures.
 *
 * Exit code: 0 all passed, 1 a check failed. */
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMutex>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <thread>
#include <tuple>

#include "evre/frame.h"
#include "evre/usb_bridge.h"
#include "model/device_map.h"
#include "model/register_model.h"
#include "phone/chart_demo.h"
#include "phone/chart_item.h"
#include "phone/p9_proof.h"
#include "phone/phone_app.h"
#include "phone/phone_session.h"
#include "phone/phone_system.h"
#include "phone/phone_theme.h"
#include "phone/register_list.h"
#include "ui/chart_data.h"
#include "ui/language.h"
#include "ui/help_topics.h"
#include "ui/theme.h"

Q_IMPORT_QML_PLUGIN(EVRePhonePlugin)

namespace {

/* ---- results */

int passed = 0, failed = 0;

void check(bool ok, const char *what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	std::fflush(stdout);
	(ok ? passed : failed)++;
}

void check(bool ok, const QString &what) { check(ok, what.toUtf8().constData()); }

/* ---- Qt's warnings about objects used across threads (the render thread's), counted, then passed on */

std::atomic<int> threadWarnings{ 0 };
QtMessageHandler previousHandler = nullptr;

void countThreadWarnings(QtMsgType type, const QMessageLogContext &context, const QString &message) {
	if (message.contains(QLatin1String("QObject::")) || message.contains(QLatin1String("QBasicTimer"))
			|| message.contains(QLatin1String("Timers cannot")))
		++threadWarnings;
	if (previousHandler) previousHandler(type, context, message);
}

/* ---- the window */

/* the window's top item: the content and the overlay (where a popup's items are) under it */
QQuickItem *topItem(QQuickWindow *window) {
	QQuickItem *item = window->contentItem();
	while (item && item->parentItem()) item = item->parentItem();
	return item;
}

/* every item under `root`, depth first */
void forEachItem(QQuickItem *root, const std::function<void(QQuickItem *)> &visit) {
	if (!root) return;
	visit(root);
	for (QQuickItem *child : root->childItems()) forEachItem(child, visit);
}

const QSize PORTRAIT(412, 915), LANDSCAPE(915, 412);

QString sizeText(QSize size) { return QStringLiteral("%1 x %2").arg(size.width()).arg(size.height()); }

/* Main.qml in an engine of its own, as main_phone.cpp loads it, shown and drawn once; its QML warnings kept */
struct Phone {
	std::unique_ptr<QQmlApplicationEngine> engine;
	QPointer<QQuickWindow> window;
	QStringList warnings;

	explicit Phone(QSize size, bool p9 = false, bool perf = false, bool chartDemo = false) : engine(std::make_unique<QQmlApplicationEngine>()) {
		QObject::connect(engine.get(), &QQmlEngine::warnings, [this](const QList<QQmlError> &list) {
			for (const QQmlError &error : list) warnings << error.toString();
		});
		window = phone::loadMain(*engine, p9, size, perf, chartDemo);
		if (!window) return;
		QSignalSpy frames(window.data(), &QQuickWindow::frameSwapped);
		window->show();
		/* both may time out on a slow machine: the checks then see what is there */
		(void) QTest::qWaitForWindowExposed(window);
		(void) QTest::qWaitFor([&] { return frames.count() > 0; }, 5000);
		settle();
	}
	~Phone() {
		/* closed, its render loop let go of it, then deleted with its engine */
		if (window) window->close();
		QTest::qWait(50);
		engine.reset();
		QCoreApplication::processEvents();
	}
	explicit operator bool() const { return window; }

	/* by objectName: among the window's QObject children, else in its item tree (a Repeater's items are only there) */
	QObject *object(const char *name) const;
	QQuickItem *item(const char *name) const { return qobject_cast<QQuickItem *>(object(name)); }

	/* a few frames, so animations started by a tap (the drawer's slide, a tab's ink) end */
	void settle(int ms = 450) const { QTest::qWait(ms); }

	/* a finger's tap at a point of the window, in its logical pixels */
	void tap(QPointF at) const {
		static QPointingDevice *finger = QTest::createTouchDevice();
		QTest::touchEvent(window, finger).press(0, at.toPoint(), window.data());
		QTest::qWait(30);
		QTest::touchEvent(window, finger).release(0, at.toPoint(), window.data());
		settle();
	}
	void tap(const QQuickItem *on) const {
		reveal(on);
		if (on) tap(on->mapToScene(QPointF(on->width() / 2, on->height() / 2)));
	}
	/* an item of the drawer scrolled into its view first: the drawer is taller than the screen */
	void reveal(const QQuickItem *on) const {
		QQuickItem *flick = item("drawerFlick");
		if (!flick || !on || !flick->isAncestorOf(on)) return;
		const double top = on->mapToItem(flick, QPointF(0, 0)).y();
		if (top >= 0 && top + on->height() <= flick->height()) return;
		const double most = std::max(0.0, flick->property("contentHeight").toDouble() - flick->height());
		const double y = flick->property("contentY").toDouble() + top - (flick->height() - on->height()) / 2;
		flick->setProperty("contentY", std::clamp(y, 0.0, most));
		QTest::qWait(50);
	}
	/* the window's session (the QML singleton), made when Main.qml loaded */
	PhoneSession *session() const {
		return engine ? engine->singletonInstance<PhoneSession *>("EVRe.Phone", "PhoneSession") : nullptr;
	}
	QString text(const char *name) const {
		const QObject *found = object(name);
		return found ? found->property("text").toString() : QString();
	}
	/* a text field given its text, as typing would */
	void type(const char *name, const QString &text) const {
		if (QObject *field = object(name)) field->setProperty("text", text);
	}

	/* the window as drawn, and a colour in it at a point of logical pixels (scaled by the picture's dpr) */
	QImage grab() const { return window ? window->grabWindow() : QImage(); }
	static QColor pixel(const QImage &image, QPointF at) {
		const qreal dpr = image.devicePixelRatio();
		const QPoint p(int(at.x() * dpr), int(at.y() * dpr));
		return image.rect().contains(p) ? image.pixelColor(p) : QColor();
	}

	/* open and its slide ended (`opened`): a position over 0.99 alone came about a fifth of the slide early, and on a
	 * busy PC what the step did next (a back, a tap) met a drawer still coming in */
	bool drawerOpen() const {
		const QObject *drawer = object("connectDrawer");
		return drawer && drawer->property("visible").toBool() && drawer->property("position").toReal() > 0.99
				&& drawer->property("opened").toBool();
	}
	bool drawerClosed() const {
		const QObject *drawer = object("connectDrawer");
		return drawer && !drawer->property("visible").toBool();
	}
	void openDrawer() const {
		tap(item("menuButton"));
		(void) QTest::qWaitFor([this] { return drawerOpen(); }, 3000);
	}
	/* a tap on the dimmed screen beside the drawer closes it */
	void closeDrawer() const {
		const bool rtl = QGuiApplication::layoutDirection() == Qt::RightToLeft;
		tap(QPointF(rtl ? 16 : window->width() - 16, window->height() / 2.0));
		(void) QTest::qWaitFor([this] { return drawerClosed(); }, 3000);
		settle();
	}
};

QObject *Phone::object(const char *name) const {
	if (!window) return nullptr;
	if (QObject *found = window->findChild<QObject *>(QLatin1String(name))) return found;
	QQuickItem *found = nullptr;
	forEachItem(topItem(window), [&](QQuickItem *item) {
		if (!found && item->objectName() == QLatin1String(name)) found = item;
	});
	/* a tab while the Chart has the whole window (no rail, no tab bar): its bars brought back first, as a finger would
	 * with the bars button */
	if (!found && QByteArray(name).startsWith("tab") && window->property("chartFull").toBool()) {
		window->setProperty("chartBars", true);
		settle(300);
		return object(name);
	}
	return found;
}

QRectF sceneRect(const QQuickItem *item);
QQuickItem *within(QQuickItem *root, const QString &name);

bool near(const QColor &a, const QColor &b, int tolerance = 6) {
	return a.isValid() && std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance
			&& std::abs(a.blue() - b.blue()) <= tolerance;
}

/* the visible Texts that do not lie inside their parent, or whose text is wider or taller than the Text itself (cut),
 * as "text (where)" */
QStringList textsOutside(QQuickWindow *window, int &seen) {
	QStringList bad;
	forEachItem(topItem(window), [&](QQuickItem *item) {
		if (!item->inherits("QQuickText") || !item->isVisible() || item->opacity() <= 0) return;
		const QString text = item->property("text").toString();
		if (text.isEmpty()) return;
		QQuickItem *parent = item->parentItem();
		if (!parent || parent->width() <= 0 || parent->height() <= 0) return;
		++seen;
		const QRectF inParent(item->position(), QSizeF(item->width(), item->height()));
		const QRectF room(0, 0, parent->width(), parent->height());
		const double contentWidth = item->property("contentWidth").toDouble();
		const double contentHeight = item->property("contentHeight").toDouble();
		const bool elided = item->property("truncated").toBool() && item->property("elide").toInt() != 0;
		const bool inside = room.adjusted(-0.5, -0.5, 0.5, 0.5).contains(inParent);
		/* inside its paddings: a text in a box keeps off the box's edge */
		const double roomWidth = item->width() - item->property("leftPadding").toDouble()
				- item->property("rightPadding").toDouble();
		const double roomHeight = item->height() - item->property("topPadding").toDouble()
				- item->property("bottomPadding").toDouble();
		const bool fits = elided || (contentWidth <= roomWidth + 1 && contentHeight <= roomHeight + 1);
		if (!inside || !fits)
			bad << QStringLiteral("\"%1\" (%2,%3 %4x%5 in %6x%7, text %8x%9)").arg(text.left(30)).arg(inParent.x())
					.arg(inParent.y()).arg(inParent.width()).arg(inParent.height()).arg(room.width()).arg(room.height())
					.arg(contentWidth).arg(contentHeight);
	});
	return bad;
}

/* the visible Texts in Latin letters alone: in Arabic none but names that stay as they are (a text with Arabic in it
 * is translated; USB and TCP stay in it as they are) */
QStringList latinTexts(QQuickWindow *window) {
	static const QRegularExpression latin(QStringLiteral("[A-Za-z]"));
	static const QRegularExpression arabic(QStringLiteral("[\\x{0600}-\\x{06FF}]"));
	/* the language list (each language in its own words), the links' names, and the chart's windows (a unit stays as it
	 * is) */
	static const QStringList kept{ QStringLiteral("English"), QStringLiteral("USB"), QStringLiteral("TCP"),
		QStringLiteral("10 s"), QStringLiteral("1 min"), QStringLiteral("5 min"), QStringLiteral("TX"), QStringLiteral("RX") };
	/* the data shown as it is: the map's device name, a USB device's name and IDs; a register's name, group, value,
	 * unit, decoded text, address and the device's error */
	static const QStringList data{ QStringLiteral("mapText"), QStringLiteral("portText"), QStringLiteral("regName"),
		QStringLiteral("regGroup"), QStringLiteral("regValue"), QStringLiteral("regUnit"), QStringLiteral("regDecoded"),
		QStringLiteral("regAddr"), QStringLiteral("regError"), QStringLiteral("sheetName"), QStringLiteral("sheetAddr"),
		QStringLiteral("sheetValue"), QStringLiteral("sheetValueUnit"), QStringLiteral("sheetUnit"), QStringLiteral("sheetDecoded"),
		QStringLiteral("sheetError"), QStringLiteral("sheetTrendMin"), QStringLiteral("sheetTrendMax"), QStringLiteral("linkPillText"), QStringLiteral("chipName"), QStringLiteral("chipValue"),
		QStringLiteral("fastChannel"), QStringLiteral("frameTime"), QStringLiteral("frameDirection"),
		QStringLiteral("frameBytes"), QStringLiteral("frameNote"), QStringLiteral("cursorName"), QStringLiteral("readoutName"),
		QStringLiteral("readoutA"), QStringLiteral("readoutB"), QStringLiteral("readoutDelta"), QStringLiteral("readoutMin"),
		QStringLiteral("readoutMax"), QStringLiteral("readoutMean"), QStringLiteral("readoutTimeA"),
		QStringLiteral("readoutTimeB"), QStringLiteral("readoutSpan"), QStringLiteral("frameType"),
		QStringLiteral("frameSheetType"), QStringLiteral("frameSheetWhen"), QStringLiteral("frameFieldName"),
		QStringLiteral("frameFieldBytes"), QStringLiteral("frameFieldMeaning"), QStringLiteral("menuButtonLabel") };
	QStringList found;
	forEachItem(topItem(window), [&](QQuickItem *item) {
		if (!item->inherits("QQuickText") || !item->isVisible() || data.contains(item->objectName())) return;
		const QString text = item->property("text").toString();
		if (text.contains(latin) && !text.contains(arabic) && !kept.contains(text)) found << text;
	});
	return found;
}

/* ---- the checks */

void checkLoads() {
	bool ok = true;
	QStringList problems;
	for (QSize size : { PORTRAIT, LANDSCAPE }) {
		Phone phone(size);
		/* the size asked for; a screen smaller than it (a CI runner's 1024 x 768) keeps the window inside itself */
		const QSize room = phone && phone.window->screen() ? phone.window->screen()->availableGeometry().size() : size;
		const QSize got = phone ? phone.window->size() : QSize();
		const bool tooBig = size.width() > room.width() || size.height() > room.height();
		const bool sized = got == size
				|| (tooBig && !got.isEmpty() && got.width() <= size.width() && got.height() <= size.height());
		const bool whole = phone && sized && phone.item("header") && phone.item("tabBar")
				&& phone.item("screens") && phone.item("screens")->childItems().size() == 4
				&& phone.object("connectDrawer") && phone.warnings.isEmpty();
		if (!whole)
			problems << QStringLiteral("%1: got %2 on a screen of %3 %4").arg(sizeText(size), sizeText(got), sizeText(room),
					phone.warnings.join(QStringLiteral("; ")));
		ok = ok && whole;
	}
	check(ok, QStringLiteral("phone, Main.qml: loads at 412 x 915 and at 915 x 412, without a QML warning: the header, "
			"the tab bar, four screens, the drawer %1").arg(problems.join(QStringLiteral(" | "))));
}

void checkTabs() {
	Phone phone(PORTRAIT);
	const char *tabs[] = { "tabRegisters", "tabChart", "tabMonitor", "tabLog" };
	const char *screens[] = { "screenRegisters", "screenChart", "screenMonitor", "screenLog" };
	bool ok = bool(phone);
	QString seen;
	/* the last first: a tap that did nothing would leave the first shown */
	for (int i : { 3, 1, 2, 0 }) {
		QQuickItem *tab = phone.item(tabs[i]);
		phone.tap(tab);
		const int current = phone.item("screens") ? phone.item("screens")->property("currentIndex").toInt() : -1;
		bool onlyThis = true;
		for (int j = 0; j < 4; ++j)
			onlyThis = onlyThis && phone.item(screens[j]) && phone.item(screens[j])->isVisible() == (j == i);
		const QString title = phone.item("screenTitle") ? phone.item("screenTitle")->property("text").toString() : QString();
		const bool named = tab && title == tab->property("text").toString();
		seen += QStringLiteral(" %1:%2%3%4").arg(i).arg(current).arg(onlyThis ? "" : " others shown")
				.arg(named ? "" : " title " + title);
		ok = ok && current == i && onlyThis && named;
	}
	check(ok, QStringLiteral("phone, tabs: a tap on each of the four tabs shows its screen alone and names it in the "
			"header (%1)").arg(seen.trimmed()));
}

void checkDrawer() {
	Phone phone(PORTRAIT);
	const bool closedAtStart = phone && phone.drawerClosed();
	phone.openDrawer();
	const bool opened = phone.drawerOpen();
	QQuickItem *title = phone.item("drawerTitle");
	const bool titleShown = title && title->isVisible();
	const bool fromLeft = phone.object("connectDrawer") && phone.object("connectDrawer")->property("x").toReal() < 1;
	phone.closeDrawer();
	const bool closed = phone.drawerClosed();
	check(closedAtStart && opened && titleShown && fromLeft && closed,
			QStringLiteral("phone, drawer: closed at the start, the menu button opens the Connect drawer from the left "
					"(its title shown), a tap beside it closes it (opened %1, title %2, left %3, closed %4)")
					.arg(opened).arg(titleShown).arg(fromLeft).arg(closed));
}

/* behind the open drawer the screen darkens in both themes: Material's grey wash lightened the dark one */
void checkScrim() {
	QStringList seen;
	bool ok = true;
	for (bool dark : { true, false }) {
		QSettings().setValue(QStringLiteral("ui/dark"), dark);
		Phone phone(PORTRAIT);
		phone.tap(phone.item("tabMonitor")); /* a plain screen: its background at the spot */
		const QPointF spot(phone.window ? phone.window->width() - 12 : 400, phone.item("header") ? phone.item("header")->height() + 30 : 90);
		const QColor before = Phone::pixel(phone.grab(), spot);
		phone.openDrawer();
		const QColor behind = Phone::pixel(phone.grab(), spot);
		phone.closeDrawer();
		const QColor bg = Theme::colorsFor(dark).bg;
		const bool darker = near(before, bg, 3) && behind.red() <= before.red() && behind.green() <= before.green()
				&& behind.blue() <= before.blue() && behind.lightness() < before.lightness() - 3;
		seen << QStringLiteral("%1: %2 -> %3").arg(dark ? "dark" : "light", before.name(), behind.name());
		ok = ok && darker;
	}
	QSettings().remove(QStringLiteral("ui/dark"));
	check(ok, QStringLiteral("phone, drawer: the screen behind it darker than the background, in the dark theme too (%1)")
			.arg(seen.join(QStringLiteral("; "))));
}

void checkTheme() {
	Phone phone(PORTRAIT);
	const ThemeColors dark = Theme::colorsFor(true), light = Theme::colorsFor(false);
	/* a point of the screen between the rows' panels, behind a section's header: the window's background; one in the
	 * header: its panel */
	const QQuickItem *group = within(phone.item("registerList"), QStringLiteral("regGroup"));
	const QPointF inScreen = group ? group->parentItem()->mapToScene(QPointF(4, 4)) : QPointF(4, 600);
	const QPointF inHeader(phone.window ? phone.window->width() / 2.0 : 200, 6);
	const QImage before = phone.grab();
	const QColor bgBefore = Phone::pixel(before, inScreen), headerBefore = Phone::pixel(before, inHeader);

	phone.openDrawer();
	QQuickItem *themeSwitch = phone.item("themeSwitch");
	phone.tap(themeSwitch);
	const bool switchedOff = themeSwitch && !themeSwitch->property("checked").toBool();
	const bool saved = !QSettings().value(QStringLiteral("ui/dark"), true).toBool();
	phone.closeDrawer();
	const QImage after = phone.grab();
	const QColor bgAfter = Phone::pixel(after, inScreen), headerAfter = Phone::pixel(after, inHeader);

	phone.openDrawer();
	phone.tap(themeSwitch);
	const bool back = QSettings().value(QStringLiteral("ui/dark"), false).toBool();
	phone.closeDrawer();
	const QColor bgBack = Phone::pixel(phone.grab(), inScreen);

	check(near(bgBefore, dark.bg) && near(headerBefore, dark.surface) && switchedOff && saved
			&& near(bgAfter, light.bg) && near(headerAfter, light.surface) && back && near(bgBack, dark.bg),
			QStringLiteral("phone, theme: the drawer's switch turns dark to light and back; the window's background and "
					"the header follow in the picture (dpr %1: %2 -> %3 -> %4, header %5 -> %6), saved in ui/dark")
					.arg(before.devicePixelRatio()).arg(bgBefore.name(), bgAfter.name(), bgBack.name(),
							headerBefore.name(), headerAfter.name()));
}

void checkLanguageChoice() {
	Phone phone(PORTRAIT);
	phone.openDrawer();
	QObject *box = phone.object("languageBox");
	QQuickItem *note = phone.item("languageNote");
	const bool noNoteAtStart = note && !note->isVisible();
	/* the list's choice as a tap on its row gives it */
	if (box) {
		box->setProperty("currentIndex", 2);
		QMetaObject::invokeMethod(box, "activated", Q_ARG(int, 2));
	}
	QTest::qWait(100);
	const bool saved = QSettings().value(QStringLiteral("ui/language")).toString() == QLatin1String("ar");
	const bool noteShown = note && note->isVisible();
	const bool notYet = QGuiApplication::layoutDirection() == Qt::LeftToRight && language::current() == QLatin1String("en");
	if (box) {
		box->setProperty("currentIndex", 1);
		QMetaObject::invokeMethod(box, "activated", Q_ARG(int, 1));
	}
	QTest::qWait(100);
	const bool english = QSettings().value(QStringLiteral("ui/language")).toString() == QLatin1String("en") && note
			&& !note->isVisible();
	QSettings().remove(QStringLiteral("ui/language"));
	check(noNoteAtStart && saved && noteShown && notYet && english,
			"phone, language: the drawer's list saves ui/language (Arabic), says the language changes when the app is "
			"opened again, and changes nothing before; English chosen back, the note goes");
}

void checkArabic() {
	language::apply(*qApp, QStringLiteral("ar"));
	{
		Phone phone(PORTRAIT);
		const double width = phone.window ? phone.window->width() : 0;
		auto centreX = [](QQuickItem *item) {
			return item ? item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).x() : -1.0;
		};
		const double menu = centreX(phone.item("menuButton")), pill = centreX(phone.item("linkPill"));
		const double registers = centreX(phone.item("tabRegisters")), log = centreX(phone.item("tabLog"));
		const QString title = phone.item("screenTitle") ? phone.item("screenTitle")->property("text").toString() : QString();
		phone.openDrawer();
		const QObject *drawer = phone.object("connectDrawer");
		const bool drawerRight = phone.drawerOpen() && drawer
				&& drawer->property("x").toReal() + drawer->property("width").toReal() > width - 1
				&& drawer->property("x").toReal() > 1;
		phone.closeDrawer();
		check(phone && menu > width / 2 && pill < width / 2 && registers > log && title == QStringLiteral("المسجّلات")
				&& drawerRight,
				QStringLiteral("phone, Arabic: the header mirrored (the menu button at the right, the pill at the left), "
						"the tab bar mirrored (Registers the right-most tab), the texts in Arabic, the drawer from the "
						"right (menu %1, pill %2, Registers %3, Log %4 of %5)")
						.arg(menu).arg(pill).arg(registers).arg(log).arg(width));
	}
	language::apply(*qApp, QStringLiteral("en"));
}

void checkTexts() {
	QStringList bad, latin;
	int seen = 0;
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		for (QSize size : { PORTRAIT, LANDSCAPE }) {
			Phone phone(size);
			if (!phone) {
				bad << code + QStringLiteral(" ") + sizeText(size) + QStringLiteral(": not loaded");
				continue;
			}
			for (const char *tab : { "tabRegisters", "tabChart", "tabMonitor", "tabLog" }) {
				phone.tap(phone.item(tab));
				for (const QString &text : textsOutside(phone.window, seen))
					bad << QStringLiteral("%1 %2 %3: %4").arg(code, sizeText(size), QLatin1String(tab), text);
				if (code == QLatin1String("ar")) latin << latinTexts(phone.window);
			}
			phone.openDrawer();
			for (const QString &text : textsOutside(phone.window, seen))
				bad << QStringLiteral("%1 %2 drawer: %3").arg(code, sizeText(size), text);
			if (code == QLatin1String("ar")) latin << latinTexts(phone.window);
			phone.closeDrawer();
		}
	}
	language::apply(*qApp, QStringLiteral("en"));
	latin.removeDuplicates();
	check(bad.isEmpty() && seen > 50, QStringLiteral("phone, texts: every Text inside its parent and not cut, at 412 x 915 "
			"and 915 x 412, in English and Arabic, on each screen and in the drawer (%1 seen) %2")
			.arg(seen).arg(bad.join(QStringLiteral(" | "))));
	check(latin.isEmpty(), QStringLiteral("phone, Arabic: every text shown is translated (none left in English) %1")
			.arg(latin.join(QStringLiteral(" | "))));
}

void checkP9() {
	DeviceMap map;
	QString error;
	map.load(phone::mapsFolder() + QStringLiteral("/example_device.json"), error);
	Phone phone(PORTRAIT, true);
	QObject *list = phone.object("p9List");
	auto *model = phone.window ? phone.window->findChild<P9Registers *>() : nullptr;
	auto *meter = phone.window ? phone.window->findChild<P9Meter *>() : nullptr;
	const bool tabsHidden = phone.item("tabBar") && !phone.item("tabBar")->isVisible();
	const int rows = model ? model->rowCount() : -1;
	const QString first = model ? model->data(model->index(0), P9Registers::NameRole).toString() : QString();
	/* a numeric register's value, a second apart: 20 ticks, a changed text */
	int numeric = 0;
	while (numeric < map.regs.size() && !map.regs[numeric].isNumeric()) ++numeric;
	const QString valueBefore = model ? model->data(model->index(numeric), P9Registers::ValueRole).toString() : QString();
	const int ticksBefore = model ? model->ticks() : 0;
	QSignalSpy lines(meter, &P9Meter::logged);
	QElapsedTimer waited;
	waited.start();
	(void) QTest::qWaitFor([&] { return waited.elapsed() >= 1000 && !lines.isEmpty(); }, 4000);
	const double perSecond = model ? (model->ticks() - ticksBefore) * 1000.0 / std::max<qint64>(waited.elapsed(), 1) : 0;
	const QString valueAfter = model ? model->data(model->index(numeric), P9Registers::ValueRole).toString() : QString();
	const QString line = lines.isEmpty() ? QString() : lines.constLast().constFirst().toString();
	static const QRegularExpression shape(QStringLiteral("^fps [0-9.]+, longest [0-9.]+ ms, over 20 ms: [0-9]+$"));
	/* 20 a second by the timer (TICK_MS); a busy machine's window thread, waiting on its frames, keeps fewer: the
	 * phone's own run is P-9's measure, this checks that the values move */
	check(list && tabsHidden && rows == map.regs.size() && first == map.regs.value(0).name
			&& P9Registers::TICK_MS == 50 && perSecond >= 2
			&& valueAfter != valueBefore && shape.match(line).hasMatch(),
			QStringLiteral("phone, P-9: the proof's list alone, its rows the example map's registers (%1 of %2), the values "
					"changing 20 times a second (%3 a second, %4 -> %5), a line a second for logcat: \"%6\"")
					.arg(rows).arg(map.regs.size()).arg(perSecond, 0, 'f', 1).arg(valueBefore, valueAfter, line));
}

/* ---- the chart (B.3a): its own nodes on the GPU, the desktop's binning */

/* the Chart screen's chart, the screen shown */
ChartItem *showChart(const Phone &phone) {
	phone.tap(phone.item("tabChart"));
	return phone.window ? phone.window->findChild<ChartItem *>(QStringLiteral("chartItem")) : nullptr;
}

/* the same for lines the test feeds itself, at times from 0 s: the chart's clock is the newest sample's, not the
 * session's (the engine's, set by ChartScreen.qml) */
ChartItem *showFedChart(const Phone &phone) {
	ChartItem *chart = showChart(phone);
	if (chart) chart->setClock({});
	return chart;
}

/* the Chart screen's area under the legend (the plot and its labels), in the picture's pixels */
QRectF areaInPicture(const Phone &phone, const QImage &image) {
	const QQuickItem *area = phone.item("chartArea");
	if (!area) return QRectF();
	const qreal dpr = image.devicePixelRatio();
	const QPointF at = area->mapToScene(QPointF(0, 0));
	return QRectF(at.x() * dpr, at.y() * dpr, area->width() * dpr, area->height() * dpr);
}

/* waits for the chart's next frame made (updatePolish) and drawn */
void chartFrame(const Phone &phone, ChartItem *chart, int frames = 2) {
	if (!chart || !phone.window) return;
	QSignalSpy made(chart, &ChartItem::framed);
	QSignalSpy swapped(phone.window.data(), &QQuickWindow::frameSwapped);
	chart->polish();
	(void) QTest::qWaitFor([&] { return made.count() >= frames && swapped.count() >= frames; }, 3000);
}

/* a sine of `hz` sampled at 1 kHz from 0 to `seconds`, as a polled line would come */
void sine(double hz, double seconds, QVector<double> &times, QVector<double> &values) {
	times.clear();
	values.clear();
	for (int i = 0; i <= int(seconds * 1000); ++i) {
		times << i / 1000.0;
		values << 2.0 + 1.5 * std::sin(2 * M_PI * hz * i / 1000.0);
	}
}

/* a line's samples taken as the desktop's chart takes them (ChartView::appendNow), binned for a view as its view is
 * (ChartView::binView: binViewSeries, the lines' generation, nothing kept yet) */
QVector<ChartData::Bin> desktopBins(const QVector<double> &times, const QVector<double> &values, double t0, double t1,
		double columns) {
	ChartData::Series s;
	const qsizetype most = 1 << 24;
	for (qsizetype i = 0; i < times.size(); ++i) {
		ChartData::roomForOne(s.times, most, s.spread);
		ChartData::roomForOne(s.values, most, s.spread);
		s.times.push_back(times[i]);
		s.values.push_back(values[i]);
		ChartData::addChunks(s, most);
	}
	ChartData::BinnedLine out;
	std::atomic<qint64> polled{ 0 }, fast{ 0 };
	ChartData::binViewSeries(s, t0, t1, columns, out, nullptr, 1, polled, fast);
	return out.bins;
}

bool sameBins(const QVector<ChartData::Bin> &a, const QVector<ChartData::Bin> &b) {
	if (a.size() != b.size()) return false;
	for (qsizetype i = 0; i < a.size(); ++i) {
		const ChartData::Bin &x = a[i], &y = b[i];
		if (x.column != y.column || x.count != y.count || x.firstSample != y.firstSample || x.t0 != y.t0 || x.t1 != y.t1
				|| x.first != y.first || x.last != y.last || x.min != y.min || x.max != y.max || x.gap != y.gap)
			return false;
	}
	return true;
}

const QColor SINE_COLOUR(255, 0, 255); /* a colour of no theme: what has it in the picture is the line */

void checkChartBins() {
	Phone phone(LANDSCAPE);
	ChartItem *chart = showFedChart(phone);
	QVector<double> times, values;
	sine(5, 20, times, values);
	if (chart) {
		chart->setWindowSeconds(10);
		chart->addLine(1, SINE_COLOUR);
		chart->appendSamples(1, times, values);
	}
	chartFrame(phone, chart, 3);
	const QVector<ChartData::Bin> phoneBins = chart ? chart->bins(1) : QVector<ChartData::Bin>();
	const double t0 = chart ? chart->viewStart() : 0, t1 = chart ? chart->viewEnd() : 0;
	const double columns = chart ? chart->columns() : 0;
	const QVector<ChartData::Bin> desktop = desktopBins(times, values, t0, t1, columns);
	const double plotWidth = chart ? chart->plotRect().width() : 0;
	check(chart && !phoneBins.isEmpty() && sameBins(phoneBins, desktop) && t1 == 20.0 && t0 == 10.0
			&& columns == std::round(plotWidth),
			QStringLiteral("phone, chart: a fed line's bins equal the desktop chart's binning of the same samples, view "
					"and width (%1 bins against %2; view %3 .. %4 s, %5 columns for a plot %6 wide)")
					.arg(phoneBins.size()).arg(desktop.size()).arg(t0).arg(t1).arg(columns).arg(plotWidth));
}

void checkChartDrawn() {
	Phone phone(LANDSCAPE);
	ChartItem *chart = showFedChart(phone);
	QVector<double> times, values;
	sine(5, 20, times, values);
	if (chart) {
		chart->setWindowSeconds(10);
		chart->addLine(1, SINE_COLOUR);
		chart->appendSamples(1, times, values);
	}
	chartFrame(phone, chart, 3);
	const QImage image = phone.grab();
	const qreal dpr = image.devicePixelRatio();
	/* the plot in the picture's pixels; outside it by more than a pixel nothing of the line */
	const QRectF plot = chart ? QRectF(chart->mapToScene(chart->plotRect().topLeft()), chart->plotRect().size()) : QRectF();
	const QRectF device(plot.x() * dpr, plot.y() * dpr, plot.width() * dpr, plot.height() * dpr);
	const QRectF room = device.adjusted(-1, -1, 1, 1);
	/* the chart's area (the plot and its labels' room): the legend's chip above it shows the line's colour on purpose */
	const QRectF area = areaInPicture(phone, image);
	int inside = 0, outside = 0, columnsHit = 0;
	QVector<bool> column(image.width(), false);
	for (int y = 0; y < image.height(); ++y)
		for (int x = 0; x < image.width(); ++x) {
			if (!near(image.pixelColor(x, y), SINE_COLOUR, 8)) continue;
			if (device.contains(QPointF(x + 0.5, y + 0.5))) {
				++inside;
				column[x] = true;
			} else if (!room.contains(QPointF(x + 0.5, y + 0.5)) && area.contains(QPointF(x + 0.5, y + 0.5))) {
				++outside;
			}
		}
	for (bool hit : std::as_const(column)) columnsHit += hit;
	/* the line runs across the plot: nearly every column of it holds some of the line */
	check(chart && inside > 0 && outside == 0 && columnsHit >= int(device.width() * 0.95),
			QStringLiteral("phone, chart: a fed sine is drawn in its colour inside the plot and nowhere else in the chart's "
					"area (dpr %1: "
					"%2 pixels inside, in %3 of %4 columns; %5 outside)")
					.arg(dpr).arg(inside).arg(columnsHit).arg(int(device.width())).arg(outside));
}

/* two fingers at the plot's middle, `from` apart, spread to `middle` apart (there `atMiddle` is called), then to `to`
 * apart, in steps, and lifted */
void pinch(const Phone &phone, QQuickItem *on, double from, double middle, double to, const std::function<void()> &atMiddle) {
	static QPointingDevice *fingers = QTest::createTouchDevice();
	const QPointF c = on->mapToScene(QPointF(on->width() / 2, on->height() / 2));
	auto at = [&c](double spread, int side) { return (c + QPointF(side * spread / 2, 0)).toPoint(); };
	QTest::touchEvent(phone.window, fingers).press(0, at(from, -1), phone.window.data()).press(1, at(from, 1), phone.window.data());
	QTest::qWait(20);
	auto spreadTo = [&](double a, double b, int steps) {
		for (int i = 1; i <= steps; ++i) {
			const double spread = a + (b - a) * i / steps;
			QTest::touchEvent(phone.window, fingers).move(0, at(spread, -1), phone.window.data()).move(1, at(spread, 1), phone.window.data());
			QTest::qWait(16);
		}
	};
	spreadTo(from, middle, 10);
	/* the last move handled before the reading (a Windows runner read the window one step early: 48.3 s for 46.5) */
	QTest::qWait(50);
	atMiddle();
	spreadTo(middle, to, 20);
	QTest::touchEvent(phone.window, fingers).release(0, at(to, -1), phone.window.data()).release(1, at(to, 1), phone.window.data());
	QTest::qWait(50);
}

/* one finger on the plot dragged by dx */
void dragBy(const Phone &phone, QQuickItem *on, double dx) {
	static QPointingDevice *finger = QTest::createTouchDevice();
	const QPointF c = on->mapToScene(QPointF(on->width() / 2, on->height() / 2));
	QTest::touchEvent(phone.window, finger).press(0, c.toPoint(), phone.window.data());
	QTest::qWait(20);
	const int steps = 15;
	for (int i = 1; i <= steps; ++i) {
		QTest::touchEvent(phone.window, finger).move(0, (c + QPointF(dx * i / steps, 0)).toPoint(), phone.window.data());
		QTest::qWait(16);
	}
	QTest::touchEvent(phone.window, finger).release(0, (c + QPointF(dx, 0)).toPoint(), phone.window.data());
	QTest::qWait(50);
}

/* two quick taps on the same spot */
void doubleTap(const Phone &phone, QQuickItem *on) {
	static QPointingDevice *finger = QTest::createTouchDevice();
	const QPoint c = on->mapToScene(QPointF(on->width() / 2, on->height() / 2)).toPoint();
	for (int i = 0; i < 2; ++i) {
		QTest::touchEvent(phone.window, finger).press(0, c, phone.window.data());
		QTest::qWait(30);
		QTest::touchEvent(phone.window, finger).release(0, c, phone.window.data());
		QTest::qWait(60);
	}
	QTest::qWait(100);
}

/* the window's button tapped, its menu's item for `seconds` (10, 60, 300) found while it shows; nullptr: none */
QQuickItem *windowItem(const Phone &phone, int seconds) {
	phone.tap(phone.item("windowButton"));
	QQuickItem *found = nullptr;
	const QString name = QStringLiteral("window%1").arg(seconds);
	forEachItem(topItem(phone.window), [&](QQuickItem *item) {
		if (!found && item->objectName() == name && item->isVisible()) found = item;
	});
	return found;
}

/* the window chosen from its menu, as a finger does */
void chooseWindow(const Phone &phone, int seconds) {
	phone.tap(windowItem(phone, seconds));
}

void checkChartPinch() {
	Phone phone(LANDSCAPE);
	ChartItem *chart = showChart(phone);
	QQuickItem *area = phone.item("chartArea");
	const double before = chart ? chart->windowSeconds() : 0;
	const QString shownBefore = phone.text("windowButton");
	/* the handler takes the fingers once they have moved past the drag threshold: from 160 px apart (taken by then)
	 * to 320, twice the spread, the window is halved */
	double middle = 0;
	if (area) pinch(phone, area, 100, 160, 320, [&] { middle = chart ? chart->windowSeconds() : 0; });
	const double after = chart ? chart->windowSeconds() : 0;
	const QString shownAfter = phone.text("windowButton");
	/* the window's menu: no length marked now */
	QQuickItem *oneMinute = windowItem(phone, 60);
	const bool chosenAfter = oneMinute && oneMinute->property("checked").toBool();
	if (QObject *menu = phone.object("windowMenu")) QMetaObject::invokeMethod(menu, "close");
	phone.settle();
	const double ratio = after > 0 ? middle / after : 0;
	const QString want = QString::number(after, 'g', 3) + QStringLiteral(" s");
	check(chart && before == 60 && middle < 60 && ratio > 1.95 && ratio < 2.05 && chart->live() && oneMinute
					&& !chosenAfter && shownBefore == QLatin1String("1 min") && shownAfter == want,
			QStringLiteral("phone, chart: a pinch to twice the fingers' spread halves the window (%1 s, %2 s at 160 px "
					"apart, %3 s at 320: %4 times shorter), the view stays live, the window's button says it (\"%5\", then "
					"\"%6\") and its menu marks none of its lengths")
					.arg(before).arg(middle, 0, 'f', 2).arg(after, 0, 'f', 2).arg(ratio, 0, 'f', 3).arg(shownBefore, shownAfter));
}

void checkChartDragAndLive() {
	Phone phone(LANDSCAPE);
	ChartItem *chart = showFedChart(phone);
	QQuickItem *area = phone.item("chartArea");
	QVector<double> times, values;
	sine(5, 30, times, values);
	if (chart) {
		chart->setWindowSeconds(10);
		chart->addLine(1, SINE_COLOUR);
		chart->appendSamples(1, times, values);
	}
	chartFrame(phone, chart);
	const bool liveBefore = chart && chart->live();
	const double endBefore = chart ? chart->viewEnd() : 0;
	const double width = chart ? chart->plotRect().width() : 1;
	if (area) dragBy(phone, area, 200);
	chartFrame(phone, chart);
	const bool held = chart && !chart->live();
	const double endHeld = chart ? chart->viewEnd() : 0;
	/* the finger's 200 px less the drag threshold, in the window's seconds a pixel: back by about 2 s */
	const double back = endBefore - endHeld, most = 200 / width * 10;
	QQuickItem *hold = phone.item("holdButton");
	const bool holdShown = hold && hold->property("checked").toBool();
	if (area) doubleTap(phone, area);
	chartFrame(phone, chart);
	const bool liveAgain = chart && chart->live() && chart->viewEnd() == 30.0;
	check(liveBefore && held && back > most * 0.6 && back <= most * 1.01 && holdShown && liveAgain,
			QStringLiteral("phone, chart: a drag to the right looks back and holds (the view's end %1 s -> %2 s, at most "
					"%3 s back; Hold marked), a double tap goes live again (end %4 s)")
					.arg(endBefore).arg(endHeld, 0, 'f', 2).arg(most, 0, 'f', 2).arg(chart ? chart->viewEnd() : 0));
}

/* the grid's colour on a value tick between two time ticks, the background between ticks; both themes */
void checkChartTheme() {
	QStringList seen;
	bool ok = true;
	for (bool dark : { true, false }) {
		QSettings().setValue(QStringLiteral("ui/dark"), dark);
		Phone phone(LANDSCAPE);
		ChartItem *chart = showChart(phone);
		chartFrame(phone, chart);
		const ThemeColors colors = Theme::colorsFor(dark);
		const QImage image = phone.grab();
		const qreal dpr = image.devicePixelRatio();
		const QVector<ChartTicks::Tick> xs = chart ? chart->timeTicks()->ticks() : QVector<ChartTicks::Tick>();
		const QVector<ChartTicks::Tick> ys = chart ? chart->valueTicks()->ticks() : QVector<ChartTicks::Tick>();
		QColor grid, background;
		if (chart && xs.size() >= 2 && ys.size() >= 2) {
			const double midX = (xs[0].at + xs[1].at) / 2, midY = (ys[0].at + ys[1].at) / 2;
			const QPointF onGrid = chart->mapToScene(QPointF(midX, ys[1].at));
			const QPointF between = chart->mapToScene(QPointF(midX, midY));
			grid = image.pixelColor(int(onGrid.x() * dpr), int(std::lround(onGrid.y() * dpr)));
			background = image.pixelColor(int(between.x() * dpr), int(between.y() * dpr));
		}
		const bool good = near(grid, colors.grid, 3) && near(background, colors.bg, 3);
		seen << QStringLiteral("%1: grid %2 (%3), background %4 (%5)").arg(dark ? "dark" : "light", grid.name(),
				colors.grid.name(), background.name(), colors.bg.name());
		ok = ok && good;
	}
	QSettings().remove(QStringLiteral("ui/dark"));
	check(ok, QStringLiteral("phone, chart: the grid and the plot's background in the theme's colours, dark and light "
			"(%1)").arg(seen.join(QStringLiteral("; "))));
}

QQuickItem *chip(const Phone &phone, const QString &name);

/* in Arabic the screen is mirrored but the plot is not: value labels left of it, time labels in rising order left to
 * right, each under its grid line */
void checkChartArabic() {
	language::apply(*qApp, QStringLiteral("ar"));
	{
		Phone phone(LANDSCAPE);
		ChartItem *chart = showFedChart(phone);
		QVector<double> times, values;
		sine(5, 20, times, values);
		if (chart) {
			chart->setWindowSeconds(10);
			chart->addLine(1, SINE_COLOUR, QStringLiteral("SINE"), QStringLiteral("V"));
			chart->addLine(2, Qt::cyan, QStringLiteral("COSINE"), QStringLiteral("V"));
			chart->appendSamples(1, times, values);
			chart->appendSamples(2, times, values);
		}
		chartFrame(phone, chart);
		const QRectF plot = chart ? QRectF(chart->mapToScene(chart->plotRect().topLeft()), chart->plotRect().size()) : QRectF();
		int valueLabels = 0, timeLabels = 0;
		bool valuesLeft = true, timesUnder = true;
		forEachItem(topItem(phone.window), [&](QQuickItem *item) {
			if (!item->isVisible()) return;
			const QRectF r(item->mapToScene(QPointF(0, 0)), QSizeF(item->width(), item->height()));
			if (item->objectName() == QLatin1String("valueLabel")) {
				++valueLabels;
				valuesLeft = valuesLeft && r.right() <= plot.left();
			} else if (item->objectName() == QLatin1String("timeLabel") && chart) {
				++timeLabels;
				/* its tick: the one of its text */
				const QString text = item->property("text").toString();
				double at = -1;
				for (const ChartTicks::Tick &tick : chart->timeTicks()->ticks())
					if (tick.text == text) at = chart->mapToScene(QPointF(tick.at, 0)).x();
				timesUnder = timesUnder && std::fabs(r.center().x() - at) <= 1 && r.top() >= plot.bottom();
			}
		});
		bool rising = chart != nullptr;
		if (chart) {
			const QVector<ChartTicks::Tick> ticks = chart->timeTicks()->ticks();
			for (qsizetype i = 1; i < ticks.size(); ++i) rising = rising && ticks[i].at > ticks[i - 1].at;
		}
		const double rightEdge = phone.window ? phone.window->width() : 0;
		/* the legend reads left to right as the plot does: the first line's chip left of the second's, at the left */
		phone.settle(300);
		const QQuickItem *firstChip = chip(phone, QStringLiteral("SINE")), *secondChip = chip(phone, QStringLiteral("COSINE"));
		const double firstX = firstChip ? firstChip->mapToScene(QPointF(0, 0)).x() : -1;
		const double secondX = secondChip ? secondChip->mapToScene(QPointF(0, 0)).x() : -1;
		const bool legendLtr = firstChip && secondChip && firstX < secondX && firstX < rightEdge / 4;
		const bool screenMirrored = phone.item("liveButton")
				&& phone.item("liveButton")->mapToScene(QPointF(0, 0)).x() > rightEdge / 2;
		check(chart && valueLabels >= 2 && timeLabels >= 2 && valuesLeft && timesUnder && rising && screenMirrored
				&& plot.left() < rightEdge / 4 && legendLtr,
				QStringLiteral("phone, Arabic: the controls mirrored (Live at the right), the plot not: its value labels "
						"left of it (%1), time rising left to right, each label under its grid line (%2), the plot from "
						"x %3, the legend's chips left to right (x %4, %5)").arg(valueLabels).arg(timeLabels).arg(plot.left())
						.arg(firstX).arg(secondX));
	}
	language::apply(*qApp, QStringLiteral("en"));
}

/* the test feed (chartdemo): 8 lines at 1 kHz live in a 1 min window, a line a second for logcat under EVRE_PERF */
void checkChartDemo() {
	Phone phone(LANDSCAPE, false, false, true);
	ChartItem *chart = phone.window ? phone.window->findChild<ChartItem *>(QStringLiteral("chartItem")) : nullptr;
	auto *meter = phone.window ? phone.window->findChild<P9Meter *>() : nullptr;
	const int tab = phone.item("screens") ? phone.item("screens")->property("currentIndex").toInt() : -1;
	QSignalSpy lines(meter, &P9Meter::logged);
	const double endBefore = chart ? chart->viewEnd() : 0;
	const qint64 polishesBefore = chart ? chart->polishes() : 0;
	QElapsedTimer waited;
	waited.start();
	(void) QTest::qWaitFor([&] { return waited.elapsed() >= 1500 && !lines.isEmpty(); }, 5000);
	chartFrame(phone, chart);
	const double endAfter = chart ? chart->viewEnd() : 0;
	int drawn = 0;
	if (chart)
		for (int k = 0; k < ChartDemo::LINES; ++k) drawn += !chart->bins(k).isEmpty();
	const QString line = lines.isEmpty() ? QString() : lines.constLast().constFirst().toString();
	const double polishMs = chart && chart->polishes() > polishesBefore
			? chart->polishSeconds() * 1000 / double(chart->polishes()) : 0;
	static const QRegularExpression shape(QStringLiteral("^fps [0-9.]+, longest [0-9.]+ ms, over 20 ms: [0-9]+$"));
	check(chart && tab == 1 && meter && meter->tag() == "EVRE_PERF" && chart->lineCount() == 8 && drawn == 8 && chart->windowSeconds() == 60 && chart->live()
			&& endAfter > endBefore + 0.5 && shape.match(line).hasMatch(),
			QStringLiteral("phone, chart demo: with chartdemo the Chart screen shows 8 lines (%1 drawn) live in a 1 min "
					"window (its end %2 s -> %3 s; a frame's lines made in %5 ms on average), and a line a second for "
					"logcat: \"%4\"").arg(drawn).arg(endBefore, 0, 'f', 2).arg(endAfter, 0, 'f', 2).arg(line)
					.arg(polishMs, 0, 'f', 2));
}

/* ---- the link (B.1): over TCP against the fake device and stand-ins, over USB through a fake bridge */

/* tests/fake_device.py --port 1210 --map maps/example_device.json (EVRE_FAKE_PORT: another port) */
const quint16 FAKE_DEVICE_PORT = quint16(qEnvironmentVariableIsSet("EVRE_FAKE_PORT") ? qEnvironmentVariableIntValue("EVRE_FAKE_PORT") : 1210);
constexpr quint16 NOBODY_PORT = 1209;          /* nothing listens there: a refused connection */
const QString USB_DEVICE = QStringLiteral("/dev/bus/usb/001/002");

QColor themeColor(QColor ThemeColors::*which) {
	return Theme::colorsFor(QSettings().value(QStringLiteral("ui/dark"), true).toBool()).*which;
}

/* waits until the session is in `state` (or the time is up) */
bool waitForState(PhoneSession *session, PhoneSession::State state, int ms = 5000) {
	return session && QTest::qWaitFor([&] { return session->state() == state; }, ms);
}

/* A stand-in between the phone and the fake device: it passes the bytes both ways, counts the links and drops them all
 * at drop(), as a device reset or a cable pulled out would. On a thread of its own. */
class DroppingProxy : public QObject {
public:
	std::atomic<int> connections{ 0 };
	std::atomic<quint16> port{ 0 };

	void start() {
		auto *server = new QTcpServer(this);
		QObject::connect(server, &QTcpServer::newConnection, this, [this, server] {
			while (QTcpSocket *client = server->nextPendingConnection()) {
				connections++;
				clients_ << client;
				auto *device = new QTcpSocket(client);
				device->connectToHost(QStringLiteral("127.0.0.1"), FAKE_DEVICE_PORT);
				QObject::connect(client, &QTcpSocket::readyRead, device, [client, device] { device->write(client->readAll()); });
				QObject::connect(device, &QTcpSocket::readyRead, client, [client, device] { client->write(device->readAll()); });
				QObject::connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
			}
		});
		if (server->listen(QHostAddress::LocalHost)) port = server->serverPort();
	}
	void drop() {
		for (const QPointer<QTcpSocket> &client : std::as_const(clients_))
			if (client) client->disconnectFromHost();
		clients_.clear();
	}

private:
	QList<QPointer<QTcpSocket>> clients_;
};

/* A device that refuses every request (ERROR_RESP permission denied) and closes the link right after, as a board whose
 * token was changed does (gui_test's RefusingDevice). On a thread of its own. */
class RefusingDevice : public QObject {
public:
	std::atomic<int> connections{ 0 };
	std::atomic<quint16> port{ 0 };

	void start() {
		auto *server = new QTcpServer(this);
		QObject::connect(server, &QTcpServer::newConnection, this, [this, server] {
			while (QTcpSocket *socket = server->nextPendingConnection()) {
				connections++;
				auto parser = std::make_shared<evre::Parser>();
				QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, parser] {
					parser->feed(socket->readAll());
					evre::Frame frame;
					bool any = false;
					while (parser->next(frame)) {
						socket->write(evre::build(frame.slave, evre::ERROR_RESP, frame.addr, frame.cnt, QByteArray(1, char(3))));
						any = true;
					}
					if (any) QTimer::singleShot(20, socket, [socket] { socket->disconnectFromHost(); });
				});
				QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
			}
		});
		if (server->listen(QHostAddress::LocalHost)) port = server->serverPort();
	}
};

/* a stand-in run on a thread of its own, started and stopped there */
template <typename Stand> struct OnThread {
	QThread thread;
	Stand *stand = new Stand;
	OnThread() {
		stand->moveToThread(&thread);
		thread.start();
		QMetaObject::invokeMethod(stand, [this] { stand->start(); }, Qt::BlockingQueuedConnection);
	}
	~OnThread() {
		QMetaObject::invokeMethod(stand, [this] { delete stand; }, Qt::BlockingQueuedConnection);
		thread.quit();
		thread.wait();
	}
	template <typename Do> void run(Do work) { QMetaObject::invokeMethod(stand, [this, work] { work(stand); }, Qt::BlockingQueuedConnection); }
};

/* A stand-in for Android's Java USB layer (usb_bridge.h), as gui_test's: it keeps what the link asks (open() comes on
 * the engine's thread), and the test speaks for Android and the board through the sink, from a thread of its own as
 * Java's reader does */
class FakeUsbBridge : public evre::UsbBridge {
public:
	QVector<Device> list; /* read on the window's thread (the port list) */
	std::atomic<int> opens{ 0 }, closes{ 0 };
	std::atomic<Sink *> sink{ nullptr };
	QVector<Device> devices() override { return list; }
	void open(const QString &name, qint32, Sink *to) override {
		{
			QMutexLocker lock(&mutex);
			opened = name;
		}
		sink = to;
		opens++;
	}
	/* the device the last open() asked for (on the engine's thread) */
	QString lastOpened() {
		QMutexLocker lock(&mutex);
		return opened;
	}
	void send(const QByteArray &) override {}
	void close() override {
		closes++;
		sink = nullptr;
	}
	QMutex mutex;
	QString opened;
	void fromJava(const std::function<void(Sink *)> &word) {
		Sink *to = sink;
		if (!to) return;
		std::thread java([&word, to] { word(to); });
		java.join();
	}
};

/* Connect over TCP from the drawer, the device's ID, polling and writes, a recording, Disconnect: one link */
void checkTcpLink() {
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	if (session) {
		session->setAutoReconnect(true);
		session->setPolling(true);
		session->setIntervalMs(100);
	}
	phone.openDrawer();
	phone.tap(phone.item("linkTcp"));
	phone.type("hostField", QStringLiteral("127.0.0.1"));
	phone.type("portField", QString::number(FAKE_DEVICE_PORT));
	phone.tap(phone.item("connectButton"));
	const bool up = waitForState(session, PhoneSession::Connected);
	const QString link = phone.text("linkText"), pill = phone.text("linkPillText");
	const QObject *dot = phone.object("linkPillDot");
	const bool green = dot && near(dot->property("color").value<QColor>(), themeColor(&ThemeColors::good), 2);
	const QString wanted = QStringLiteral("127.0.0.1:%1").arg(FAKE_DEVICE_PORT);
	check(session && up && session->tcp() && link == QStringLiteral("Connected: ") + wanted
					&& (pill == QLatin1String("Connected") || pill.startsWith(QStringLiteral("Connected · ")))
					&& green && phone.text("connectButton") == QLatin1String("Disconnect")
					&& session->log()->contains(QStringLiteral("connected: ") + wanted)
					&& QSettings().value(QStringLiteral("phone/host")).toString() == QLatin1String("127.0.0.1"),
			QStringLiteral("phone, connect: TCP chosen in the drawer, the host and port typed, Connect: connected to the fake "
					"device, the drawer says \"%1\", the pill \"%2\" with a green dot, the button Disconnect, the Log "
					"\"connected: ...\", the host saved").arg(link, pill));

	const bool read = session && QTest::qWaitFor([&] { return session->deviceId() == 0x1001; }, 3000);
	const QString device = phone.text("deviceText");
	check(read && device.startsWith(QStringLiteral("Device ID 0x1001 · protocol rev ")) && phone.item("deviceText")
					&& phone.item("deviceText")->isVisible() && phone.text("deviceWarning").isEmpty()
					&& session->log()->contains(QStringLiteral("device ID 0x1001")),
			QStringLiteral("phone, DEVICE_ID: the drawer shows \"%1\" (the example map's 0x1001, no warning), the Log the "
					"same").arg(device));

	/* polling: 50 ms gives about 20 polls a second, 200 ms about 5, off none */
	auto rateAfter = [&](int ms) {
		QTest::qWait(ms);
		return session ? session->pollHz() : -1;
	};
	/* the engine measures over a second or more: 4 s leaves a whole window after each change */
	if (session) session->setIntervalMs(50);
	const double fast = rateAfter(4000);
	if (session) session->setIntervalMs(200);
	const double slow = rateAfter(4000);
	phone.tap(phone.item("pollSwitch"));
	const double off = rateAfter(1200);
	const bool offSaved = !QSettings().value(QStringLiteral("phone/polling"), true).toBool();
	const QString offText = phone.text("pollText");
	phone.tap(phone.item("pollSwitch"));
	phone.tap(phone.item("writesSwitch"));
	const bool writes = session && session->writesEnabled() && session->registers()->writesEnabled()
			&& session->log()->contains(QStringLiteral("writes allowed"));
	phone.tap(phone.item("writesSwitch"));
	const bool writesOff = session && !session->registers()->writesEnabled();
	check(fast >= 12 && fast <= 24 && slow >= 3.5 && slow <= 6.5 && off == 0 && offSaved
					&& QSettings().value(QStringLiteral("phone/interval")).toInt() == 200 && !offText.contains(QLatin1String("polls"))
					&& writes && writesOff,
			QStringLiteral("phone, polling and writes: an interval of 50 ms polls %1 times a second, 200 ms %2, Polling off "
					"none (%3; the drawer's line \"%4\"), both saved; Allow writes turns the registers' writes on and off")
					.arg(fast, 0, 'f', 1).arg(slow, 0, 'f', 1).arg(off).arg(offText));

	/* a recording: Record, a while at 50 ms, Stop; the file in the app's folder, every row whole */
	if (session) session->setIntervalMs(50);
	phone.tap(phone.item("recordButton"));
	const bool recording = session && QTest::qWaitFor([&] { return session->recording(); }, 3000);
	const QString during = phone.text("recordButton");
	QTest::qWait(1500);
	phone.tap(phone.item("recordButton"));
	const bool stopped = session && QTest::qWaitFor([&] { return !session->recording(); }, 3000);
	const QString file = session ? session->recordFile() : QString();
	QFile csv(file);
	QStringList lines;
	if (csv.open(QIODevice::ReadOnly | QIODevice::Text)) lines = QString::fromUtf8(csv.readAll()).split(QLatin1Char('\n'));
	const bool endsWhole = !lines.isEmpty() && lines.constLast().isEmpty(); /* the last row ended by its newline */
	if (endsWhole) lines.removeLast();
	const int columns = lines.isEmpty() ? 0 : int(lines.constFirst().count(QLatin1Char(',')));
	bool rowsWhole = lines.size() > 1;
	for (const QString &line : lines) rowsWhole = rowsWhole && line.count(QLatin1Char(',')) == columns;
	const int rows = int(lines.size()) - 1;
	const QString saved = phone.text("recordText");
	check(recording && during == QLatin1String("Stop recording") && stopped && endsWhole && rowsWhole && rows >= 10
					&& columns >= 3 && lines.value(0).startsWith(QLatin1String("time_s,datetime,"))
					&& QFileInfo(file).absolutePath() == QFileInfo(PhoneSession::recordingsFolder()).absoluteFilePath()
					&& saved == QStringLiteral("Saved %1 · rows: %2").arg(QFileInfo(file).fileName()).arg(rows)
					&& session->log()->contains(QStringLiteral("CSV recording stopped: %1 rows").arg(rows)),
			QStringLiteral("phone, recording: Record writes a CSV into the app's folder (%1), Stop closes it whole: %2 rows "
					"of %3 columns each, the last ended by its newline; the drawer says \"%4\"")
					.arg(QDir::toNativeSeparators(file)).arg(rows).arg(columns + 1).arg(saved));
	QFile::remove(file);

	phone.tap(phone.item("connectButton"));
	const bool off2 = waitForState(session, PhoneSession::Off, 2000);
	const QString closedText = phone.text("linkText"), closedPill = phone.text("linkPillText");
	const QString lastSeen = phone.text("deviceText");
	check(off2 && closedText == QLatin1String("Not connected") && closedPill == QLatin1String("Not connected")
					&& phone.text("connectButton") == QLatin1String("Connect") && session->log()->contains(QStringLiteral("disconnected"))
					&& lastSeen.startsWith(QStringLiteral("last seen: Device ID 0x1001")),
			QStringLiteral("phone, disconnect: Disconnect closes the link, the drawer and the pill say \"%1\", the Log "
					"\"disconnected\", the device's ID kept as \"%2\", Connect offered").arg(closedText, lastSeen));
}

/* a port where nothing listens: the reason in the drawer and the Log, no try by itself with Reconnect by itself off */
void checkRefusedLink() {
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	if (session) {
		session->setAutoReconnect(false);
		session->connectTcp(QStringLiteral("127.0.0.1"), NOBODY_PORT, QString());
	}
	const bool closed = waitForState(session, PhoneSession::Closed, 8000);
	const QString why = phone.text("linkText");
	const QObject *dot = phone.object("linkPillDot");
	const bool red = dot && near(dot->property("color").value<QColor>(), themeColor(&ThemeColors::bad), 2);
	check(closed && why.contains(QLatin1String("refused"), Qt::CaseInsensitive) && red
					&& phone.text("linkPillText") == QLatin1String("Not connected") && !session->wantConnected()
					&& session->log()->contains(QStringLiteral("not connected: ") + why),
			QStringLiteral("phone, a refused connection: nothing listens on port %1, the drawer says why (\"%2\"), the pill "
					"\"Not connected\" with a red dot, the Log \"not connected: ...\", no try by itself").arg(NOBODY_PORT).arg(why));
	if (session) session->setAutoReconnect(true);
}

/* the link dropped under the phone: it says so and comes back by itself */
void checkReconnect() {
	OnThread<DroppingProxy> proxy;
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	if (session) {
		session->setAutoReconnect(true);
		session->connectTcp(QStringLiteral("127.0.0.1"), proxy.stand->port, QString());
	}
	const bool up = waitForState(session, PhoneSession::Connected);
	/* every state the session goes through, with the pill's and the drawer's words then: a busy machine may pass
	 * through Reconnecting between two looks */
	bool lost = false;
	QString pill, why;
	QObject *pillText = phone.object("linkPillText"), *detail = phone.object("linkDetail");
	QMetaObject::Connection watch;
	if (session && pillText)
		watch = QObject::connect(session, &PhoneSession::linkChanged, pillText, [&] {
			if (session->state() != PhoneSession::Reconnecting || lost) return;
			lost = true;
			pill = pillText->property("text").toString();
			why = detail ? detail->property("text").toString() : QString();
		});
	proxy.run([](DroppingProxy *stand) { stand->drop(); });
	(void) QTest::qWaitFor([&] { return lost; }, 3000);
	const bool back = QTest::qWaitFor([&] { return lost && session->state() == PhoneSession::Connected; }, 5000);
	const int links = proxy.stand->connections;
	check(up && lost && pill == QStringLiteral("Reconnecting…") && why.startsWith(QLatin1String("closed by 127.0.0.1:")) && back
					&& links == 2 && session->log()->contains(QStringLiteral("connection lost: closed by")),
			QStringLiteral("phone, reconnect: the link dropped, the pill says \"%1\" and the drawer why (\"%2\"), the Log "
					"\"connection lost: ...\"; connected again by itself (%3 links; up %4, lost %5, back %6)").arg(pill, why).arg(links)
					.arg(up).arg(lost).arg(back));
	QObject::disconnect(watch);
	if (session) session->disconnectLink();
}

/* a refused token closes the link: one try, no more by itself, the drawer says what to do */
void checkTokenStops() {
	OnThread<RefusingDevice> device;
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	if (session) {
		session->setAutoReconnect(true);
		session->connectTcp(QStringLiteral("127.0.0.1"), device.stand->port, QStringLiteral("not-the-token"));
	}
	QTest::qWait(3000); /* at least five tries twice a second, were they made */
	const int tries = device.stand->connections;
	const QString said = phone.text("linkText");
	check(session && tries == 1 && session->state() == PhoneSession::Closed && !session->wantConnected()
					&& said == QLatin1String("token refused: type the right token, then Connect")
					&& session->log()->contains(QStringLiteral("not reconnecting: token refused"))
					&& phone.text("connectButton") == QLatin1String("Connect"),
			QStringLiteral("phone, a wrong token: the device refuses it and closes the link; one try in 3 s (%1), not "
					"reconnected by itself, the drawer says \"%2\", Connect offered").arg(tries).arg(said));
}

/* a map with the USB IDs of a device plugged in, written to `folder` */
QString mapWithUsb(const QString &folder) {
	QFile in(phone::mapsFolder() + QStringLiteral("/example_device.json"));
	if (!in.open(QIODevice::ReadOnly)) return QString();
	QByteArray text = in.readAll();
	text.replace("\"slave\": 1,", "\"slave\": 1,\n  \"usb\": { \"vid\": \"0x0483\", \"pid\": \"0x5740\" },");
	const QString file = folder + QStringLiteral("/example_usb.json");
	QFile out(file);
	if (!out.open(QIODevice::WriteOnly) || out.write(text) != text.size()) return QString();
	return file;
}

/* the drawer's USB list: the bridge's devices, the map's own named by it; one unplugged leaves the list while open */
void checkUsbPorts() {
	FakeUsbBridge bridge;
	bridge.list = { { USB_DEVICE, 0x0483, 0x5740, QStringLiteral("STM32 Virtual ComPort") },
		{ QStringLiteral("/dev/bus/usb/001/003"), 0x1a86, 0x7523, QStringLiteral("USB Serial") } };
	PhoneSession::setUsbBridgeForTests(&bridge);
	QTemporaryDir folder;
	{
		Phone phone(PORTRAIT);
		PhoneSession *session = phone.session();
		const QString file = mapWithUsb(folder.path());
		const bool loaded = session && !file.isEmpty() && session->loadMap(file);
		phone.openDrawer();
		phone.tap(phone.item("linkUsb"));
		QObject *box = phone.object("portBox");
		const QString first = session ? session->ports()->data(session->ports()->index(0), PortList::LabelRole).toString() : QString();
		const QString second = session ? session->ports()->data(session->ports()->index(1), PortList::LabelRole).toString() : QString();
		const bool listed = box && phone.item("portBox")->isVisible() && box->property("count").toInt() == 2
				&& phone.item("noPorts") && !phone.item("noPorts")->isVisible() && phone.item("usbPanel")->isVisible()
				&& !phone.item("tcpPanel")->isVisible();
		bridge.list.removeLast();
		const bool gone = session && QTest::qWaitFor([&] { return session->ports()->count() == 1; }, 4000);
		check(loaded && listed && first == USB_DEVICE + QStringLiteral(" · Example device · 0483:5740")
						&& second == QStringLiteral("/dev/bus/usb/001/003 · USB Serial · 1a86:7523") && gone,
				QStringLiteral("phone, USB ports: USB chosen in the drawer lists the bridge's devices, the map's own named by "
						"the map (\"%1\", \"%2\"); one unplugged leaves the list while the drawer is open").arg(first, second));
		if (session) session->loadMap(phone::mapsFolder() + QStringLiteral("/example_device.json"));
	}
	PhoneSession::setUsbBridgeForTests(nullptr);
}

/* Android's permission dialog: waiting, then refused; no try by itself after a refusal */
void checkUsbPermission() {
	FakeUsbBridge bridge;
	bridge.list = { { USB_DEVICE, 0x0483, 0x5740, QString() } };
	PhoneSession::setUsbBridgeForTests(&bridge);
	{
		Phone phone(PORTRAIT);
		PhoneSession *session = phone.session();
		phone.openDrawer();
		phone.tap(phone.item("linkUsb"));
		if (session) session->setAutoReconnect(true);
		phone.tap(phone.item("connectButton"));
		const bool asked = QTest::qWaitFor([&] { return bridge.opens == 1 && bridge.sink; }, 3000);
		bridge.fromJava([](evre::UsbBridge::Sink *sink) { sink->usbAsking(); });
		const bool waiting = waitForState(session, PhoneSession::Waiting, 3000);
		const QString pill = phone.text("linkPillText"), hint = phone.text("linkDetail");
		check(asked && waiting && pill == QStringLiteral("Waiting for permission…") && phone.text("linkText") == pill
						&& hint.contains(USB_DEVICE) && phone.text("connectButton") == QLatin1String("Cancel"),
				QStringLiteral("phone, USB permission: Connect opens the device through the bridge; Android's dialog up, the "
						"pill says \"%1\", the drawer names the device (\"%2\"), Cancel offered (asked %3, button %4)").arg(pill, hint)
						.arg(asked).arg(phone.text("connectButton")));
		bridge.fromJava([](evre::UsbBridge::Sink *sink) { sink->usbRefused(); });
		const bool closed = waitForState(session, PhoneSession::Closed, 3000);
		const QString why = phone.text("linkText");
		QTest::qWait(2500); /* a try by itself would have come by now */
		check(closed && why == QStringLiteral("permission refused for ") + USB_DEVICE && bridge.opens == 1
						&& !session->wantConnected() && session->log()->contains(QStringLiteral("not reconnecting: permission refused")),
				QStringLiteral("phone, USB refused: the person says no, the drawer says \"%1\", the Log too, and the "
						"permission is not asked again by itself (%2 opens)").arg(why).arg(bridge.opens.load()));
	}
	PhoneSession::setUsbBridgeForTests(nullptr);
}

/* the board unplugged while connected: said, and tried again by itself */
void checkUsbGone() {
	FakeUsbBridge bridge;
	bridge.list = { { USB_DEVICE, 0x0483, 0x5740, QString() } };
	PhoneSession::setUsbBridgeForTests(&bridge);
	{
		Phone phone(PORTRAIT);
		PhoneSession *session = phone.session();
		if (session) {
			session->setAutoReconnect(true);
			session->connectUsb(USB_DEVICE, 115200);
		}
		(void) QTest::qWaitFor([&] { return bridge.opens == 1 && bridge.sink; }, 3000);
		bridge.fromJava([](evre::UsbBridge::Sink *sink) { sink->usbOpened(); });
		const bool up = waitForState(session, PhoneSession::Connected, 3000);
		const QString link = phone.text("linkText");
		bridge.fromJava([](evre::UsbBridge::Sink *sink) { sink->usbGone(); });
		const bool lost = waitForState(session, PhoneSession::Reconnecting, 3000);
		const QString why = phone.text("linkDetail");
		const bool again = QTest::qWaitFor([&] { return bridge.opens == 2; }, 3000);
		check(up && link == QStringLiteral("Connected: %1 @ 115200").arg(USB_DEVICE) && lost
						&& why == USB_DEVICE + QStringLiteral(" is gone: unplugged or reset?") && again
						&& session->log()->contains(QStringLiteral("connection lost: ") + why),
				QStringLiteral("phone, USB gone: connected (\"%1\"), the board unplugged: the drawer says \"%2\", the Log "
						"\"connection lost: ...\", and the device is opened again by itself").arg(link, why));
		if (session) session->disconnectLink();
	}
	PhoneSession::setUsbBridgeForTests(nullptr);
}

/* the Log screen: the session's lines, an error in red, Clear */
void checkLogScreen() {
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	if (session) session->loadMap(QStringLiteral("no_such_map.json")); /* an error line */
	phone.tap(phone.item("tabLog"));
	QQuickItem *list = phone.item("logList");
	const int lines = session ? session->log()->count() : 0;
	(void) QTest::qWaitFor([&] { return list && list->property("count").toInt() == lines; }, 2000);
	/* the newest line's text item: the list follows the end */
	QQuickItem *newest = nullptr;
	forEachItem(list, [&](QQuickItem *item) {
		if (item->objectName() == QLatin1String("logText") && item->property("text").toString().startsWith(QLatin1String("map not loaded")))
			newest = item;
	});
	const bool red = newest && newest->isVisible() && near(newest->property("color").value<QColor>(), themeColor(&ThemeColors::bad), 2);
	/* the levels' chips count the lines: "Info 1, Warnings 0, Errors 1" */
	int counted = 0;
	for (const char *chipName : { "logLevel0", "logLevel1", "logLevel2" })
		if (const QObject *levelChip = phone.object(chipName)) counted += levelChip->property("count").toInt();
	const QString count = QStringLiteral("%1 in the chips").arg(counted);
	const bool mapLine = session && session->log()->contains(QStringLiteral("map loaded: "));
	phone.tap(phone.item("logClear"));
	const bool cleared = session && session->log()->count() == 0 && list && list->property("count").toInt() == 0
			&& phone.item("logEmptyText") && phone.item("logEmptyText")->isVisible();
	check(lines >= 2 && mapLine && red && counted == lines && cleared,
			QStringLiteral("phone, Log screen: the session's lines (%1: the map loaded, a map that did not), an error in "
					"red, \"%2\"; Clear empties it and says nothing is there yet").arg(lines).arg(count));
}

/* ---- the Registers screen (B.2): the rows over TCP against the fake device */

/* an item under `root` by its objectName (a row's own: every row has a "regValue") */
QQuickItem *within(QQuickItem *root, const QString &name) {
	QQuickItem *found = nullptr;
	forEachItem(root, [&](QQuickItem *item) {
		if (!found && item->objectName() == name) found = item;
	});
	return found;
}

/* a register's row, by its name: created only while it is in the list's view (search for it first). The list's own
 * item for its index (ListView.itemAtIndex): a row the list keeps aside for reuse keeps the name it last had and its
 * place, and a tap there met nothing */
QQuickItem *regRow(const Phone &phone, const QString &reg) {
	QQuickItem *list = phone.item("registerList");
	PhoneSession *session = phone.session();
	if (!list || !session) return nullptr;
	const QVector<RegisterModel::Row> &rows = session->registers()->rows();
	int source = -1;
	for (int i = 0; i < rows.size() && source < 0; ++i)
		if (rows[i].def.name == reg) source = i;
	const int index = session->registerList()->rowOf(source);
	QQuickItem *item = nullptr;
	if (index >= 0) QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, index));
	return item && item->objectName() == QStringLiteral("row_") + reg ? item : nullptr;
}

QString rowText(const Phone &phone, const QString &reg, const char *part) {
	QQuickItem *found = within(regRow(phone, reg), QLatin1String(part));
	return found ? found->property("text").toString() : QString();
}

/* the register's row in RegisterModel, by its name */
int modelRow(PhoneSession *session, const QString &reg) {
	if (!session) return -1;
	const QVector<RegisterModel::Row> &rows = session->registers()->rows();
	for (int i = 0; i < rows.size(); ++i)
		if (rows[i].def.name == reg) return i;
	return -1;
}

QString modelValue(PhoneSession *session, const QString &reg) {
	const int row = modelRow(session, reg);
	if (row < 0) return QString();
	return session->registers()->data(session->registers()->index(row, RegisterModel::ColValue), Qt::DisplayRole).toString();
}

/* connected to the fake device (or `port`), polling every 100 ms, its values in the table; the session or null */
PhoneSession *connectFake(const Phone &phone, quint16 port = FAKE_DEVICE_PORT) {
	PhoneSession *session = phone.session();
	if (!session) return nullptr;
	session->setAutoReconnect(true);
	session->setPolling(true);
	session->setIntervalMs(100);
	session->connectTcp(QStringLiteral("127.0.0.1"), port, QString());
	if (!waitForState(session, PhoneSession::Connected)) return session;
	(void) QTest::qWaitFor([&] {
		const int row = modelRow(session, QStringLiteral("SUPPLY_V"));
		return row >= 0 && session->registers()->rows()[row].valid;
	}, 4000);
	return session;
}

/* the search field given a text, as typing would; a frame or two for the list */
void search(const Phone &phone, const QString &text) {
	phone.type("registerSearch", text);
	phone.settle(250);
}

bool sheetOpen(const Phone &phone) {
	const QObject *sheet = phone.object("registerSheet");
	return sheet && sheet->property("opened").toBool();
}

/* an item's place in the window once it holds still: a list laid out again after a search, or a sheet that grew,
 * moves its items at the next frame's polish, and a tap at the old place met nothing (a hidden desktop draws a frame
 * in 200 ms) */
void waitStill(const Phone &phone, const std::function<QQuickItem *()> &find) {
	QPointF last(-1e9, -1e9);
	QElapsedTimer since;
	since.start();
	(void) QTest::qWaitFor([&] {
		const QQuickItem *item = find();
		const QPointF now = item ? item->mapToScene(QPointF(0, 0)) : QPointF(-1e9, -1e9);
		if (!item || now != last) since.restart();
		last = now;
		return item && since.elapsed() >= 300;
	}, 4000);
	(void) phone;
}

/* a register's sheet, by a tap on its row (once the list shows it after the search, held still), and its content
 * held still in turn */
void openSheet(const Phone &phone, const QString &reg) {
	search(phone, reg);
	waitStill(phone, [&] { return regRow(phone, reg); });
	phone.tap(regRow(phone, reg));
	(void) QTest::qWaitFor([&] { return sheetOpen(phone); }, 3000);
	waitStill(phone, [&] { return phone.item("sheetCopy"); });
	phone.settle();
}

void closeSheet(const Phone &phone) {
	if (QObject *sheet = phone.object("registerSheet")) QMetaObject::invokeMethod(sheet, "close");
	(void) QTest::qWaitFor([&] { return !phone.object("registerSheet")->property("visible").toBool(); }, 3000);
	phone.settle();
}

/* a point of an item, in the window's logical pixels */
QPointF at(const QQuickItem *item, double x, double y) { return item ? item->mapToScene(QPointF(x, y)) : QPointF(-1, -1); }

/* how far two colours are apart, channel by channel */
int distance(const QColor &a, const QColor &b) {
	return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
}

void checkRegisterRows() {
	DeviceMap map;
	QString error;
	map.load(phone::mapsFolder() + QStringLiteral("/example_device.json"), error);
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	RegisterList *list = session ? session->registerList() : nullptr;
	QStringList names, mapNames, groups;
	for (const RegDef &def : map.regs) mapNames << def.name;
	bool together = true;
	for (int i = 0; list && i < list->rowCount(); ++i) {
		names << list->data(list->index(i), RegisterList::NameRole).toString();
		const QString group = list->data(list->index(i), RegisterList::GroupRole).toString();
		if (groups.isEmpty() || groups.constLast() != group) {
			together = together && !groups.contains(group);
			groups << group;
		}
	}
	QStringList sortedNames = names, sortedMap = mapNames;
	sortedNames.sort();
	sortedMap.sort();
	/* the first section's header and row drawn: the map's first group and register */
	QQuickItem *view = phone.item("registerList");
	const QString header = view ? within(view, QStringLiteral("regGroup")) ? within(view, QStringLiteral("regGroup"))->property("text").toString() : QString() : QString();
	const QString firstAddr = rowText(phone, map.regs.value(0).name, "regAddr");
	const bool offline = phone.item("registersOffline") && phone.item("registersOffline")->isVisible();
	check(list && list->total() == map.regs.size() && list->count() == map.regs.size() && sortedNames == sortedMap && together
					&& groups.size() == 5 && header == map.regs.value(0).group && firstAddr == QLatin1String("0xA000")
					&& offline && phone.text("registerCount") == QStringLiteral("registers: %1").arg(map.regs.size()),
			QStringLiteral("phone, Registers: a row per register of the map (%1 of %2), each group's together in sections "
					"(%3), the first header \"%4\" and row's address %5; not connected, a strip says so with Connect")
					.arg(names.size()).arg(map.regs.size()).arg(groups.join(QStringLiteral(", ")), header, firstAddr));
}

void checkRegisterValues() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	search(phone, QStringLiteral("SUPPLY_V"));
	(void) QTest::qWaitFor([&] { return !rowText(phone, QStringLiteral("SUPPLY_V"), "regValue").startsWith(QStringLiteral("—")); }, 3000);
	const QString value = rowText(phone, QStringLiteral("SUPPLY_V"), "regValue");
	const QString unit = rowText(phone, QStringLiteral("SUPPLY_V"), "regUnit");
	bool number = false;
	value.toDouble(&number);
	/* the fake device's sine moves it: a second later another value, from the table */
	QTest::qWait(1000);
	const QString later = rowText(phone, QStringLiteral("SUPPLY_V"), "regValue");
	const QString inModel = modelValue(session, QStringLiteral("SUPPLY_V"));
	const bool offline = phone.item("registersOffline") && phone.item("registersOffline")->isVisible();
	check(session && session->connected() && number && unit == QLatin1String("V") && later != value
					&& (later == inModel || QTest::qWaitFor([&] {
						   return rowText(phone, QStringLiteral("SUPPLY_V"), "regValue") == modelValue(session, QStringLiteral("SUPPLY_V"));
					   }, 1000))
					&& !offline,
			QStringLiteral("phone, Registers: connected to the fake device, SUPPLY_V's row shows its value with its unit "
					"(\"%1\" \"%2\", then \"%3\"), the table's (RegisterModel's text); the not-connected line gone")
					.arg(value, unit, later));
	if (session) session->disconnectLink();
}

/* a write through the sheet: Write, the answer, the row shows what the device holds (read back) */
void checkRegisterWrite() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	if (session) session->setWritesEnabled(true);
	openSheet(phone, QStringLiteral("FAN_SPEED"));
	const bool opened = sheetOpen(phone);
	const QString before = phone.text("sheetField");
	const bool startsWithNow = before == modelValue(session, QStringLiteral("FAN_SPEED"));
	const QString wanted = before == QLatin1String("42") ? QStringLiteral("43") : QStringLiteral("42");
	const QString limits = phone.text("sheetLimits");
	phone.type("sheetField", wanted);
	phone.tap(phone.item("sheetApply"));
	const bool answered = QTest::qWaitFor([&] { return phone.text("sheetAnswer") == QLatin1String("FAN_SPEED written"); }, 3000);
	const QQuickItem *answer = phone.item("sheetAnswer");
	const bool green = answer && near(answer->property("color").value<QColor>(), themeColor(&ThemeColors::good), 2);
	const bool readBack = QTest::qWaitFor([&] {
		return modelValue(session, QStringLiteral("FAN_SPEED")) == wanted && rowText(phone, QStringLiteral("FAN_SPEED"), "regValue") == wanted
				&& phone.text("sheetValue") == wanted && phone.text("sheetValueUnit") == QLatin1String("%")
				&& phone.item("sheetReadBack") && phone.item("sheetReadBack")->isVisible()
				&& phone.text("sheetReadBack") == QStringLiteral("Read back: %1 %").arg(wanted);
	}, 3000);
	check(opened && startsWithNow && limits == QStringLiteral("The map's limits: from 0 to 100 %")
					&& answered && green && readBack
					&& session->log()->contains(QStringLiteral("written: FAN_SPEED = %1").arg(wanted)),
			QStringLiteral("phone, write: a tap on FAN_SPEED's row opens its sheet (the field starts with the value now \"%1\", "
					"\"%2\"); %3 typed, Write: \"%4\" in green, then \"%5\"; the row and the sheet's value show %3 read back "
					"from the device, the Log \"written: ...\"")
					.arg(before, limits, wanted, phone.text("sheetAnswer"), phone.text("sheetReadBack")));
	if (session) session->disconnectLink();
}

/* past the map's maximum: asked first, nothing written until Write anyway */
void checkRegisterLimit() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	if (session) session->setWritesEnabled(true);
	openSheet(phone, QStringLiteral("FAN_SPEED"));
	const QString before = modelValue(session, QStringLiteral("FAN_SPEED"));
	phone.type("sheetField", QStringLiteral("150"));
	phone.tap(phone.item("sheetApply"));
	const QQuickItem *question = phone.item("sheetQuestion");
	const bool asked = question && question->isVisible();
	const QString text = phone.text("sheetQuestionText");
	QTest::qWait(600);
	const bool notYet = modelValue(session, QStringLiteral("FAN_SPEED")) == before && phone.text("sheetAnswer").isEmpty()
			&& !phone.item("sheetApply")->isEnabled();
	phone.tap(phone.item("sheetCancel"));
	const bool cancelled = question && !question->isVisible() && phone.text("sheetAnswer") == QLatin1String("Not written")
			&& modelValue(session, QStringLiteral("FAN_SPEED")) == before;
	phone.tap(phone.item("sheetApply"));
	phone.tap(phone.item("sheetWriteAnyway"));
	const bool written = QTest::qWaitFor([&] { return modelValue(session, QStringLiteral("FAN_SPEED")) == QLatin1String("150"); }, 3000);
	check(asked && text.startsWith(QStringLiteral("FAN_SPEED = 150 is above the maximum 100 % the map gives it."))
					&& text.endsWith(QStringLiteral("Write it anyway?")) && notYet && cancelled && written,
			QStringLiteral("phone, write past the limits: 150 for FAN_SPEED (0 to 100) asks first (\"%1\"), nothing is written "
					"(still %2); Cancel says \"Not written\"; Write anyway writes it (read back 150)").arg(text, before));
	if (session) session->disconnectLink();
}

/* a register the device does not have: its row in red, a write to it refused with the device's text */
void checkRegisterRefused() {
	QTemporaryDir dir;
	QFile in(phone::mapsFolder() + QStringLiteral("/example_device.json"));
	QString json = in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()) : QString();
	const int registers = int(json.indexOf(QLatin1String("\"registers\"")));
	const int open = int(json.indexOf(QLatin1Char('['), registers));
	json.insert(open + 1, QStringLiteral("{ \"name\": \"NOWHERE\", \"addr\": \"0xD0F0\", \"type\": \"u8\", \"access\": \"rw\", "
										  "\"group\": \"Settings\" },"));
	const QString file = dir.filePath(QStringLiteral("nowhere.json"));
	QFile out(file);
	if (out.open(QIODevice::WriteOnly)) out.write(json.toUtf8());
	out.close();

	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	const bool loaded = session && session->loadMap(file);
	session = connectFake(phone);
	if (session) session->setWritesEnabled(true);
	search(phone, QStringLiteral("NOWHERE"));
	const bool red = QTest::qWaitFor([&] {
		const QQuickItem *value = within(regRow(phone, QStringLiteral("NOWHERE")), QStringLiteral("regValue"));
		const QQuickItem *error = within(regRow(phone, QStringLiteral("NOWHERE")), QStringLiteral("regError"));
		return value && error && error->isVisible() && near(value->property("color").value<QColor>(), themeColor(&ThemeColors::bad), 2);
	}, 4000);
	const QString shown = rowText(phone, QStringLiteral("NOWHERE"), "regValue");
	const QString why = rowText(phone, QStringLiteral("NOWHERE"), "regError");
	const QQuickItem *redBar = within(regRow(phone, QStringLiteral("NOWHERE")), QStringLiteral("rowErrorBar"));
	const bool barShown = redBar && redBar->isVisible();
	openSheet(phone, QStringLiteral("NOWHERE"));
	phone.type("sheetField", QStringLiteral("1"));
	phone.tap(phone.item("sheetApply"));
	const bool refused = QTest::qWaitFor([&] { return phone.text("sheetAnswer").startsWith(QLatin1String("write refused: NOWHERE = 1")); }, 3000);
	const QString answer = phone.text("sheetAnswer");
	const QQuickItem *answerItem = phone.item("sheetAnswer");
	const bool answerRed = answerItem && near(answerItem->property("color").value<QColor>(), themeColor(&ThemeColors::bad), 2);
	check(loaded && red && barShown && shown == QLatin1String("not available") && !why.isEmpty() && refused && answerRed
					&& answer.contains(QLatin1String("offset out of range"))
					&& session->log()->contains(QStringLiteral("write refused: NOWHERE")),
			QStringLiteral("phone, refused write: a register the device does not have shows \"%1\" in red, a red bar at its row's "
					"start and why (\"%2\"); "
					"a write to it answers \"%3\" in red, the Log the same").arg(shown, why, answer));
	if (session) {
		session->disconnectLink();
		session->loadMap(phone::mapsFolder() + QStringLiteral("/example_device.json"));
	}
}

/* writes off, or no link: the sheet says so and offers the drawer, no Apply */
void checkWritesOff() {
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	if (session) session->setWritesEnabled(false);
	openSheet(phone, QStringLiteral("SETPOINT"));
	const QString offline = phone.text("sheetBlocked");
	const bool offlineShown = phone.item("sheetBlocked") && phone.item("sheetBlocked")->isVisible()
			&& !phone.item("sheetApply")->isVisible() && !phone.item("sheetField")->isVisible();
	closeSheet(phone);
	session = connectFake(phone);
	openSheet(phone, QStringLiteral("SETPOINT"));
	const QString writesOff = phone.text("sheetBlocked");
	const bool writesOffShown = phone.item("sheetBlocked")->isVisible() && !phone.item("sheetApply")->isVisible();
	phone.tap(phone.item("sheetConnect"));
	(void) QTest::qWaitFor([&] { return phone.drawerOpen(); }, 3000);
	const bool drawer = phone.drawerOpen() && !sheetOpen(phone);
	const bool themeKept = QSettings().value(QStringLiteral("ui/dark"), true).toBool(); /* the tap reached nothing in it */
	check(themeKept && offlineShown && offline == QLatin1String("Not connected: connect in the Connect drawer first.") && writesOffShown
					&& writesOff == QLatin1String("Writes are off: turn on Allow writes in the Connect drawer.") && drawer,
			QStringLiteral("phone, register sheet: with no link it says \"%1\", connected with Allow writes off \"%2\", no field and "
					"no Apply; its button opens the Connect drawer").arg(offline, writesOff));
	if (session) session->disconnectLink();
}

/* a change glows (the bar and the tint in the picture) and fades */
void checkRegisterGlow() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	if (session) session->setWritesEnabled(true);
	search(phone, QStringLiteral("FAN_SPEED"));
	const QString before = modelValue(session, QStringLiteral("FAN_SPEED"));
	const QString wanted = before == QLatin1String("7") ? QStringLiteral("8") : QStringLiteral("7");
	/* a value glows when it changes after holding still (RegisterModel::GLOW_REST_MS) */
	QTest::qWait(int(RegisterModel::GLOW_REST_MS) + 300);
	const int row = modelRow(session, QStringLiteral("FAN_SPEED"));
	if (session) session->write(row, wanted);
	const bool changed = QTest::qWaitFor([&] { return rowText(phone, QStringLiteral("FAN_SPEED"), "regValue") == wanted; }, 3000);
	QQuickItem *panel = within(regRow(phone, QStringLiteral("FAN_SPEED")), QStringLiteral("rowPanel"));
	/* the bar at the row's start edge, and a spot of the row with nothing on it (its end, near its foot) */
	const QPointF barAt = at(panel, 1.5, panel ? panel->height() / 2 : 0);
	const QPointF tintAt = at(panel, panel ? panel->width() - 6 : 0, panel ? panel->height() - 5 : 0);
	const QImage lit = phone.grab();
	const QColor surface = themeColor(&ThemeColors::surface), accent = themeColor(&ThemeColors::accent);
	const QColor barLit = Phone::pixel(lit, barAt), tintLit = Phone::pixel(lit, tintAt);
	QTest::qWait(int(RegisterModel::GLOW_MS) + 400);
	const QImage plain = phone.grab();
	const QColor barPlain = Phone::pixel(plain, barAt), tintPlain = Phone::pixel(plain, tintAt);
	const QQuickItem *bar = within(regRow(phone, QStringLiteral("FAN_SPEED")), QStringLiteral("rowGlowBar"));
	check(changed && distance(barLit, surface) > 60 && distance(barLit, accent) < distance(barLit, surface)
					&& tintLit.blue() > surface.blue() + 4 && near(barPlain, surface, 8) && near(tintPlain, surface, 4)
					&& bar && bar->opacity() == 0,
			QStringLiteral("phone, glow: FAN_SPEED written after a rest, its row glows in the picture (dpr %1: the bar %2, the "
					"row %3, on %4) and fades within %5 ms (the bar %6, the row %7)")
					.arg(lit.devicePixelRatio()).arg(barLit.name(), tintLit.name(), surface.name()).arg(RegisterModel::GLOW_MS)
					.arg(barPlain.name(), tintPlain.name()));
	if (session) session->disconnectLink();
}

/* polling off: the values are not refreshed, the rows say so and grey them */
void checkRegisterStale() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	search(phone, QStringLiteral("SUPPLY_V"));
	const QQuickItem *stale = within(regRow(phone, QStringLiteral("SUPPLY_V")), QStringLiteral("regStale"));
	const bool freshAtFirst = stale && !stale->isVisible();
	if (session) session->setPolling(false);
	const bool greyed = QTest::qWaitFor([&] {
		const QQuickItem *value = within(regRow(phone, QStringLiteral("SUPPLY_V")), QStringLiteral("regValue"));
		return stale && stale->isVisible() && value && near(value->property("color").value<QColor>(), themeColor(&ThemeColors::muted), 2);
	}, 3000);
	const QString said = rowText(phone, QStringLiteral("SUPPLY_V"), "regStale");
	if (session) session->setPolling(true);
	const bool freshAgain = QTest::qWaitFor([&] { return stale && !stale->isVisible(); }, 3000);
	check(freshAtFirst && greyed && said == QLatin1String("not refreshed") && freshAgain,
			QStringLiteral("phone, stale: Polling off, SUPPLY_V's value greys and its row says \"%1\"; polling again, it is "
					"fresh again").arg(said));
	if (session) session->disconnectLink();
}

void checkRegisterSearch() {
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	RegisterList *list = session ? session->registerList() : nullptr;
	search(phone, QStringLiteral("fan"));
	const int byName = list ? list->count() : -1;
	const bool fanCard = regRow(phone, QStringLiteral("FAN_SPEED")) && !regRow(phone, QStringLiteral("SUPPLY_V"));
	const QString count = phone.text("registerCount");
	search(phone, QStringLiteral("0xd08"));
	const int byAddr = list ? list->count() : -1;
	search(phone, QStringLiteral("zzz"));
	const bool none = list && list->count() == 0 && phone.item("registersEmptyText") && phone.item("registersEmptyText")->isVisible();
	const QString empty = phone.text("registersEmptyText");
	phone.tap(phone.item("searchClear"));
	const bool all = list && list->count() == list->total() && phone.text("registerSearch").isEmpty();
	check(byName == 1 && fanCard && count == QStringLiteral("registers: 1 of %1").arg(list ? list->total() : 0) && byAddr == 4
					&& none && empty == QLatin1String("No register matches \"zzz\".") && all,
			QStringLiteral("phone, search: \"fan\" keeps FAN_SPEED alone (\"%1\"), \"0xd08\" the 4 at 0xD08x, \"zzz\" none "
					"(\"%2\"); Clear shows them all again").arg(count, empty));
}

/* the sheet's Plot marks it plotted, the chart's limit says why not, Copy */
void checkRegisterPlot() {
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	RegisterModel *model = session ? session->registers() : nullptr;
	openSheet(phone, QStringLiteral("SUPPLY_V"));
	const bool opened = sheetOpen(phone);
	const QString plotText = phone.text("sheetPlot");
	phone.tap(phone.item("sheetPlot"));
	const int row = modelRow(session, QStringLiteral("SUPPLY_V"));
	const bool plotted = model && row >= 0 && model->rows()[row].plot;
	const QString plotDone = phone.text("sheetDone");
	const QString offText = phone.text("sheetPlot");
	const QQuickItem *sheetTag = phone.item("sheetPlottedTag");
	const bool sheetMarked = sheetTag && sheetTag->isVisible();
	closeSheet(phone);
	const QQuickItem *mark = within(regRow(phone, QStringLiteral("SUPPLY_V")), QStringLiteral("regPlotted"));
	const bool marked = mark && mark->isVisible() && mark->property("text").toString() == QLatin1String("on the chart");

	/* one line at most: the next is refused with the desktop's words, in the sheet */
	if (model) model->setPlotLimit(1);
	openSheet(phone, QStringLiteral("SUPPLY_I"));
	phone.tap(phone.item("sheetPlot"));
	const int other = modelRow(session, QStringLiteral("SUPPLY_I"));
	const bool refused = model && other >= 0 && !model->rows()[other].plot;
	const QString limitDone = phone.text("sheetDone");
	const QQuickItem *done = phone.item("sheetDone");
	const bool doneShown = done && done->isVisible() && near(done->property("color").value<QColor>(), themeColor(&ThemeColors::warn), 2);

	/* Copy: the name, the value and the unit on the clipboard */
	phone.tap(phone.item("sheetCopy"));
	const QString copied = QGuiApplication::clipboard() ? QGuiApplication::clipboard()->text() : QString();
	const QString copyDone = phone.text("sheetDone");
	closeSheet(phone);
	check(opened && plotText == QLatin1String("Plot") && plotted && sheetMarked && offText == QLatin1String("Take off the chart")
					&& marked && plotDone == QLatin1String("SUPPLY_V on the chart") && refused && doneShown
					&& limitDone == QLatin1String("At most 1 registers on the chart at 10 samples a second: take one off first"),
			QStringLiteral("phone, Plot: a row's sheet has Plot; it marks SUPPLY_V plotted (\"%1\", the button now \"%2\", \"on "
					"the chart\" on the sheet and the row); with a limit of one line the next is refused in the sheet: \"%3\"")
					.arg(plotDone, offText, limitDone));
	check(copied == QStringLiteral("SUPPLY_I = %1 A").arg(modelValue(session, QStringLiteral("SUPPLY_I")))
					&& copyDone == QStringLiteral("Copied: ") + copied,
			QStringLiteral("phone, Copy: the sheet's Copy puts \"%1\" on the clipboard and says so").arg(copied));
}

/* no symbol characters a phone's font may not have: the menu's bars, the rows' and the pill's chevrons are drawn */
void checkDrawnSymbols() {
	Phone phone(PORTRAIT);
	static const QString symbols = QStringLiteral("☰⋯▾▸▴▲▼◀▶›‹✎✕");
	QStringList found;
	forEachItem(topItem(phone.window), [&](QQuickItem *item) {
		if (!item->inherits("QQuickText")) return;
		const QString text = item->property("text").toString();
		for (const QChar c : symbols)
			if (text.contains(c)) found << text;
	});
	int bars = 0;
	forEachItem(phone.item("menuButton"), [&](QQuickItem *item) { bars += item->objectName() == QLatin1String("menuBar"); });
	search(phone, QStringLiteral("SUPPLY_V"));
	const QQuickItem *chevron = within(regRow(phone, QStringLiteral("SUPPLY_V")), QStringLiteral("rowChevron"));
	const QQuickItem *pillChevron = phone.item("linkPillChevron");
	const bool drawn = chevron && chevron->isVisible() && chevron->property("kind").toString() == QLatin1String("next")
			&& pillChevron && pillChevron->isVisible() && pillChevron->property("kind").toString() == QLatin1String("down");
	const QString label = phone.item("menuButton") ? phone.item("menuButton")->property("text").toString() : QStringLiteral("?");
	check(found.isEmpty() && bars == 3 && drawn && label.isEmpty(),
			QStringLiteral("phone, symbols: the menu button's three bars, a row's chevron and the pill's are drawn (%1 bars, "
					"chevrons %2), no Text holds a symbol a phone's font may lack (\"☰\" showed as a box on a Samsung phone) %3")
					.arg(bars).arg(drawn).arg(found.join(QStringLiteral(" | "))));
}

/* A stand-in between the phone and the fake device that drops the first DEVICE_ID request of each link, as a board still
 * booting when the link comes back answers nothing. On a thread of its own. */
class SleepyProxy : public QObject {
public:
	std::atomic<int> connections{ 0 }, dropped{ 0 }, asked{ 0 };
	std::atomic<quint16> port{ 0 };

	void start() {
		auto *server = new QTcpServer(this);
		QObject::connect(server, &QTcpServer::newConnection, this, [this, server] {
			while (QTcpSocket *client = server->nextPendingConnection()) {
				connections++;
				auto *device = new QTcpSocket(client);
				device->connectToHost(QStringLiteral("127.0.0.1"), FAKE_DEVICE_PORT);
				auto parser = std::make_shared<evre::Parser>();
				auto first = std::make_shared<bool>(true);
				QObject::connect(client, &QTcpSocket::readyRead, device, [this, client, device, parser, first] {
					parser->feed(client->readAll());
					evre::Frame frame;
					while (parser->next(frame)) {
						const bool deviceId = frame.fn == evre::READ && frame.addr == 0xA000 && frame.cnt == 4;
						if (deviceId) asked++;
						if (deviceId && *first) {
							*first = false;
							dropped++;
							continue;
						}
						device->write(evre::build(frame.slave, frame.fn, frame.addr, frame.cnt, frame.data));
					}
				});
				QObject::connect(device, &QTcpSocket::readyRead, client, [client, device] { client->write(device->readAll()); });
				QObject::connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
			}
		});
		if (server->listen(QHostAddress::LocalHost)) port = server->serverPort();
	}
};

/* DEVICE_ID unanswered after a (re)connect: read again by itself, "reading the device ID…" meanwhile */
void checkDeviceIdRetry() {
	OnThread<SleepyProxy> proxy;
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	QStringList texts;
	if (session) {
		QObject::connect(session, &PhoneSession::deviceChanged, session, [&] { texts << session->deviceText(); });
		session->setAutoReconnect(true);
		session->connectTcp(QStringLiteral("127.0.0.1"), proxy.stand->port, QString());
	}
	QElapsedTimer clock;
	clock.start();
	const bool up = waitForState(session, PhoneSession::Connected);
	const bool read = session && QTest::qWaitFor([&] { return session->deviceId() == 0x1001; }, 5000);
	const qint64 ms = clock.elapsed();
	const QString shown = phone.text("deviceText");
	check(up && read && ms < 4000 && proxy.stand->dropped == 1 && proxy.stand->asked >= 2 && proxy.stand->connections == 1
					&& texts.contains(QStringLiteral("reading the device ID…")) && shown.startsWith(QLatin1String("Device ID 0x1001"))
					&& !session->log()->contains(QStringLiteral("DEVICE_ID not read")),
			QStringLiteral("phone, DEVICE_ID again: the first request after the connect gets no answer (a board still booting); "
					"\"reading the device ID…\", then read again by itself: \"%1\" after %2 ms, one link, no failure in the Log "
					"(asked %3 times; %4)").arg(shown).arg(ms).arg(proxy.stand->asked.load()).arg(texts.join(QStringLiteral(" > "))));
	if (session) session->disconnectLink();
}

/* the Registers screen at 412 px in both languages, connected, and its write sheet with a question: every text inside
 * its parent and not cut; Arabic mirrored but the value, its unit and the address left to right */
void checkRegisterTexts() {
	QStringList bad, latin, mirror;
	int seen = 0;
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		{
			Phone phone(PORTRAIT);
			PhoneSession *session = connectFake(phone);
			if (session) session->setWritesEnabled(true);
			phone.settle(600);
			for (const QString &text : textsOutside(phone.window, seen)) bad << code + QStringLiteral(" list: ") + text;
			if (code == QLatin1String("ar")) latin << latinTexts(phone.window);
			/* a row's parts: the name at the start edge, the value then its unit left to right, the chevron at the end */
			search(phone, QStringLiteral("SETPOINT"));
			QQuickItem *setpoint = regRow(phone, QStringLiteral("SETPOINT"));
			auto x = [&](const char *part) {
				const QQuickItem *item = within(setpoint, QLatin1String(part));
				return item ? item->mapToScene(QPointF(item->width() / 2, 0)).x() : -1.0;
			};
			const double width = phone.window ? phone.window->width() : 0;
			const double name = x("regName"), value = x("regValue"), unit = x("regUnit"), more = x("rowChevron");
			const bool rtl = code == QLatin1String("ar");
			/* the map's names (Latin) at the start edge too: their text, not only their item */
			auto align = [&](QQuickItem *in, const char *part) {
				const QQuickItem *item = within(in, QLatin1String(part));
				return item ? item->property("effectiveHorizontalAlignment").toInt() : -1;
			};
			const int start = rtl ? Qt::AlignRight : Qt::AlignLeft;
			const int nameAlign = align(setpoint, "regName"), groupAlign = align(phone.item("registerList"), "regGroup");
			/* the name at the start edge, the value and the chevron at the end (the left in Arabic, the chevron outermost) */
			auto left = [&](const char *part) {
				const QQuickItem *item = within(setpoint, QLatin1String(part));
				return item ? item->mapToScene(QPointF(0, 0)).x() : -1.0;
			};
			const bool ok = left("regValue") < left("regUnit")
					&& (rtl ? (name > width / 2 && value < width / 2 && more < value) : (name < width / 2 && value > width / 2 && more > unit))
					&& nameAlign == start && groupAlign == start;
			if (!ok)
				mirror << QStringLiteral("%1: name %2, value %3, unit %4, more %5, aligned %6 %7").arg(code).arg(name).arg(value)
								  .arg(unit).arg(more).arg(nameAlign).arg(groupAlign);
			openSheet(phone, QStringLiteral("FAN_SPEED"));
			phone.type("sheetField", QStringLiteral("150"));
			phone.tap(phone.item("sheetApply"));
			for (const QString &text : textsOutside(phone.window, seen)) bad << code + QStringLiteral(" sheet: ") + text;
			if (code == QLatin1String("ar")) latin << latinTexts(phone.window);
			if (session) session->disconnectLink();
		}
	}
	language::apply(*qApp, QStringLiteral("en"));
	latin.removeDuplicates();
	check(bad.isEmpty() && seen > 60, QStringLiteral("phone, Registers texts: at 412 px, connected, in English and Arabic, the "
			"rows and a register's sheet asking first: every Text inside its parent and not cut (%1 seen) %2")
			.arg(seen).arg(bad.join(QStringLiteral(" | "))));
	check(latin.isEmpty() && mirror.isEmpty(), QStringLiteral("phone, Registers in Arabic: every text translated but the "
			"map's names and the values; the row mirrored (its value and chevron at the left), the name and the group at the "
			"right, the value and its unit left to right %1 %2").arg(latin.join(QStringLiteral(" | ")), mirror.join(QStringLiteral(" | "))));
}

/* the Registers screen in both themes: the window's background, a row's panel and its name in the picture */
void checkRegisterThemes() {
	QStringList seen;
	bool ok = true;
	for (bool dark : { true, false }) {
		QSettings().setValue(QStringLiteral("ui/dark"), dark);
		const ThemeColors colors = Theme::colorsFor(dark);
		Phone phone(PORTRAIT);
		QQuickItem *panel = within(regRow(phone, QStringLiteral("DEVICE_ID")), QStringLiteral("rowPanel"));
		const QImage image = phone.grab();
		const QColor rowColor = Phone::pixel(image, at(panel, panel ? panel->width() - 6 : 0, panel ? panel->height() - 5 : 0));
		/* the background: behind a section's header, between the groups' panels */
		const QQuickItem *group = within(phone.item("registerList"), QStringLiteral("regGroup"));
		const QColor bg = Phone::pixel(image, at(group ? group->parentItem() : nullptr, 4, 4));
		const QQuickItem *name = within(regRow(phone, QStringLiteral("DEVICE_ID")), QStringLiteral("regName"));
		const bool nameColor = name && near(name->property("color").value<QColor>(), colors.text, 2);
		const bool fine = panel && near(rowColor, colors.surface, 4) && near(bg, colors.bg, 4) && nameColor;
		seen << QStringLiteral("%1: row %2, background %3").arg(dark ? "dark" : "light", rowColor.name(), bg.name());
		ok = ok && fine;
	}
	QSettings().remove(QStringLiteral("ui/dark"));
	check(ok, QStringLiteral("phone, Registers themes: the rows on the theme's surface, the sections' headers on its "
			"background, the names in its text colour, dark and light (%1)").arg(seen.join(QStringLiteral("; "))));
}

/* perf: a line a second while the Registers screen shows, none on another screen */
void checkPerfLine() {
	Phone phone(PORTRAIT, false, true);
	auto *meter = phone.window ? phone.window->findChild<P9Meter *>() : nullptr;
	QSignalSpy lines(meter, &P9Meter::logged);
	const bool runner = phone.item("perfRunner") && phone.item("perfRunner")->isVisible();
	(void) QTest::qWaitFor([&] { return lines.count() >= 2; }, 4000);
	const int onRegisters = int(lines.count());
	const QString line = lines.isEmpty() ? QString() : lines.constLast().constFirst().toString();
	phone.tap(phone.item("tabLog"));
	lines.clear();
	QTest::qWait(2300);
	const int onLog = int(lines.count());
	phone.tap(phone.item("tabRegisters"));
	const bool again = QTest::qWaitFor([&] { return lines.count() >= 1; }, 3000);
	static const QRegularExpression shape(QStringLiteral("^fps [0-9.]+, longest [0-9.]+ ms, over 20 ms: [0-9]+$"));
	check(meter && runner && onRegisters >= 2 && shape.match(line).hasMatch() && onLog == 0 && again,
			QStringLiteral("phone, perf: with perf (am start ... --ez perf true) a line a second for logcat (EVRE_PERF) while the "
					"Registers screen shows (\"%1\"), a bar running at its top; none on the Log screen").arg(line));
}

/* ---- B.2b: the keyboard, the drawn icons, the touch targets, Arabic's start edge, the muted texts' contrast */

/* the bottom of an item, in the window's logical pixels */
double bottomOf(const QQuickItem *item) { return item ? item->mapToScene(QPointF(0, item->height())).y() : 1e9; }
double topOf(const QQuickItem *item) { return item ? item->mapToScene(QPointF(0, 0)).y() : -1e9; }

/* a keyboard of `height` stood for (Main.qml's testKeyboardHeight), a frame or two for the sheet and the drawer */
void fakeKeyboard(const Phone &phone, double height) {
	if (phone.window) phone.window->setProperty("testKeyboardHeight", height);
	phone.settle(500);
}

/* the field typed in: focused as a tap on it would */
void focusField(const Phone &phone, const char *name) {
	if (QObject *field = phone.object(name)) QMetaObject::invokeMethod(field, "forceActiveFocus");
	phone.settle(300);
}

/* the owner's bug (B.2 on a Samsung phone): the keyboard over the write sheet's field. The sheet's field and Apply, and
 * the drawer's token field, stay above a keyboard (one stood for: a desktop has none), in portrait and landscape; and
 * the sheet stays inside a window made smaller for the keyboard (Android's adjustResize) */
void checkKeyboard() {
	QStringList seen;
	bool ok = true;
	for (QSize size : { PORTRAIT, LANDSCAPE }) {
		const double keyboard = size == PORTRAIT ? 330 : 190;
		Phone phone(size);
		PhoneSession *session = connectFake(phone);
		if (session) session->setWritesEnabled(true);
		openSheet(phone, QStringLiteral("FAN_SPEED"));
		focusField(phone, "sheetField");
		fakeKeyboard(phone, keyboard);
		const double top = phone.window ? phone.window->height() - keyboard : 0;
		const double field = bottomOf(phone.item("sheetField")), apply = bottomOf(phone.item("sheetApply"));
		const double fieldTop = topOf(phone.item("sheetField"));
		const bool sheetOk = sheetOpen(phone) && field <= top + 0.5 && apply <= top + 0.5 && fieldTop >= 0;
		fakeKeyboard(phone, 0);
		closeSheet(phone);

		/* the window made smaller for the keyboard, as adjustResize does: the sheet at its new foot */
		const QSize full = phone.window ? phone.window->size() : size;
		openSheet(phone, QStringLiteral("FAN_SPEED"));
		focusField(phone, "sheetField");
		if (phone.window) phone.window->resize(full.width(), int(full.height() - keyboard));
		phone.settle(600);
		const double shortHeight = phone.window ? phone.window->height() : 0;
		const double fieldShort = bottomOf(phone.item("sheetField")), applyShort = bottomOf(phone.item("sheetApply"));
		const bool resizedOk = fieldShort <= shortHeight + 0.5 && applyShort <= shortHeight + 0.5
				&& topOf(phone.item("sheetField")) >= 0;
		closeSheet(phone);
		if (phone.window) phone.window->resize(full);
		phone.settle();
		if (session) session->disconnectLink();

		/* the drawer's token field, the lowest of its fields */
		if (session) session->setTcp(true);
		phone.openDrawer();
		focusField(phone, "tokenField");
		fakeKeyboard(phone, keyboard);
		const double token = bottomOf(phone.item("tokenField")), tokenTop = topOf(phone.item("tokenField"));
		const bool drawerOk = phone.drawerOpen() && token <= top + 0.5 && tokenTop >= 0;
		fakeKeyboard(phone, 0);
		phone.closeDrawer();

		seen << QStringLiteral("%1, keyboard from %2: field %3, Apply %4, token %5; window %6 high: field %7, Apply %8")
						.arg(sizeText(size)).arg(top).arg(field).arg(apply).arg(token).arg(shortHeight).arg(fieldShort).arg(applyShort);
		ok = ok && sheetOk && resizedOk && drawerOk;
	}
	check(ok, QStringLiteral("phone, keyboard: the write sheet's field and Apply and the drawer's token field above the soft "
			"keyboard, and the sheet inside a window made smaller for it (%1)").arg(seen.join(QStringLiteral("; "))));
}

/* the search field's clear cross: drawn, 48 dp, shown only with a text; a tap empties the field and shows them all */
void checkSearchClear() {
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	RegisterList *list = session ? session->registerList() : nullptr;
	QQuickItem *clear = phone.item("searchClear");
	const bool hiddenEmpty = clear && !clear->isVisible();
	search(phone, QStringLiteral("fan"));
	const bool shown = clear && clear->isVisible() && clear->width() >= 48 && clear->height() >= 48;
	int strokes = 0;
	forEachItem(phone.item("clearIcon"), [&](QQuickItem *item) { strokes += item->isVisible() && item->inherits("QQuickRectangle"); });
	const bool magnifier = phone.item("searchIcon") && phone.item("searchIcon")->isVisible();
	const QString label = clear ? clear->property("text").toString() : QStringLiteral("?");
	phone.tap(clear);
	const bool emptied = phone.text("registerSearch").isEmpty() && list && list->count() == list->total() && clear
			&& !clear->isVisible();
	check(hiddenEmpty && shown && strokes == 2 && magnifier && label.isEmpty() && emptied,
			QStringLiteral("phone, search: the magnifier drawn at its start; the clear cross drawn (%1 strokes, no text), 48 dp, "
					"shown only while the field holds a text; a tap empties the field and shows every register").arg(strokes));
}

/* no link: the strip at the top of the Registers screen says so, its button opens the drawer */
void checkOfflineButton() {
	Phone phone(PORTRAIT);
	QQuickItem *card = phone.item("registersOffline");
	const bool shown = card && card->isVisible() && !phone.text("offlineText").isEmpty();
	/* a slim strip, no longer a card: 56 high (64 in Arabic's longer words at most), right over the list, in view */
	const bool slim = card && card->height() <= 72;
	const QRectF strip = sceneRect(card), view = sceneRect(phone.item("registerList"));
	const bool inView = !strip.isEmpty() && strip.top() >= 48 && std::fabs(strip.bottom() - view.top()) <= 0.5;
	QQuickItem *button = phone.item("registersConnect");
	const bool target = button && button->height() >= 48;
	phone.tap(button);
	(void) QTest::qWaitFor([&] { return phone.drawerOpen(); }, 3000);
	const bool opened = phone.drawerOpen();
	check(shown && slim && inView && target && opened, QStringLiteral("phone, not connected: the Registers screen says so in a "
			"slim strip right over the list (%1 high at %2, the list from %3, \"%4\") and its Connect button (48 dp) opens the "
			"Connect drawer").arg(card ? card->height() : -1).arg(strip.top()).arg(view.top()).arg(phone.text("offlineText")));
}

/* the tab bar: an icon drawn over each name (beside it in landscape), no symbol glyph; the chosen tab in the accent on
 * its pill, the others muted */
void checkTabIcons() {
	QStringList bad;
	for (QSize size : { PORTRAIT, LANDSCAPE }) {
		Phone phone(size);
		phone.tap(phone.item("tabLog"));
		const QImage image = phone.grab();
		const QColor surface = themeColor(&ThemeColors::surface);
		for (const char *name : { "tabRegisters", "tabChart", "tabMonitor", "tabLog" }) {
			QQuickItem *tab = phone.item(name);
			QQuickItem *icon = within(tab, QStringLiteral("tabIcon"));
			QQuickItem *label = within(tab, QStringLiteral("tabName"));
			int strokes = 0;
			forEachItem(icon, [&](QQuickItem *item) { strokes += item->isVisible() && item->inherits("QQuickRectangle"); });
			bool glyph = false;
			forEachItem(tab, [&](QQuickItem *item) {
				if (item->inherits("QQuickText") && item->property("text").toString() != tab->property("text").toString()
						&& !item->property("text").toString().isEmpty())
					glyph = true;
			});
			/* the icon's pixels: some far from the bar's surface */
			int inked = 0;
			if (icon) {
				const QPointF a = icon->mapToScene(QPointF(0, 0));
				for (int y = 0; y < int(icon->height()); ++y)
					for (int x = 0; x < int(icon->width()); ++x)
						inked += distance(Phone::pixel(image, a + QPointF(x + 0.5, y + 0.5)), surface) > 60;
			}
			const bool chosen = QLatin1String(name) == QLatin1String("tabLog");
			const QColor want = themeColor(chosen ? &ThemeColors::accent : &ThemeColors::muted);
			const bool coloured = icon && label && near(icon->property("color").value<QColor>(), want, 2)
					&& near(label->property("color").value<QColor>(), want, 2);
			/* upright in the tab bar, across in the rail: over its name either way */
			const bool placed = icon && label && bottomOf(icon) <= topOf(label) + 1;
			if (!icon || !icon->isVisible() || icon->width() < 20 || strokes < 3 || glyph || inked < 15 || !coloured || !placed)
				bad << QStringLiteral("%1 %2: %3 strokes, %4 inked px%5%6%7").arg(sizeText(size), QLatin1String(name)).arg(strokes)
								.arg(inked).arg(glyph ? ", a glyph" : "").arg(coloured ? "" : ", colour").arg(placed ? "" : ", placed");
		}
	}
	check(bad.isEmpty(), QStringLiteral("phone, tab icons: each tab's icon drawn (rectangles, no glyph) over its name, in the "
			"tab bar upright and the rail across; the chosen tab's in the accent, the others muted %1").arg(bad.join(QStringLiteral(" | "))));
}

/* the buttons on the cards, in the write sheet and around them: at least 48 dp high and wide (Material's own) */
void checkTouchTargets() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	if (session) session->setWritesEnabled(true);
	QStringList small;
	auto measure = [&](QQuickItem *item, const QString &what) {
		if (!item || !item->isVisible()) {
			small << what + QStringLiteral(" (not shown)");
			return;
		}
		if (item->width() < 47.5 || item->height() < 47.5)
			small << QStringLiteral("%1 %2x%3").arg(what).arg(item->width()).arg(item->height());
	};
	measure(phone.item("menuButton"), QStringLiteral("menu"));
	measure(phone.item("linkPill"), QStringLiteral("pill"));
	search(phone, QStringLiteral("SETPOINT"));
	measure(regRow(phone, QStringLiteral("SETPOINT")), QStringLiteral("a row"));
	measure(phone.item("searchClear"), QStringLiteral("clear"));
	openSheet(phone, QStringLiteral("FAN_SPEED"));
	measure(phone.item("sheetApply"), QStringLiteral("Write"));
	measure(phone.item("sheetClose"), QStringLiteral("Close"));
	measure(phone.item("sheetPlot"), QStringLiteral("Plot"));
	measure(phone.item("sheetCopy"), QStringLiteral("Copy"));
	phone.type("sheetField", QStringLiteral("150"));
	phone.tap(phone.item("sheetApply"));
	measure(phone.item("sheetWriteAnyway"), QStringLiteral("Write anyway"));
	measure(phone.item("sheetCancel"), QStringLiteral("Cancel"));
	closeSheet(phone);
	openSheet(phone, QStringLiteral("LED_MODE"));
	measure(phone.item("sheetChoice_2"), QStringLiteral("a choice"));
	closeSheet(phone);
	for (const char *tab : { "tabRegisters", "tabChart", "tabMonitor", "tabLog" }) measure(phone.item(tab), QLatin1String(tab));
	check(small.isEmpty(), QStringLiteral("phone, touch targets: the menu, the pill, a register's row, search's clear, the "
			"sheet's Write, Close, Plot, Copy, Write anyway, Cancel and an enum's choice, the tabs: each at least 48 x 48 dp %1")
			.arg(small.join(QStringLiteral(" | "))));
	if (session) session->disconnectLink();
}

/* an enum register's sheet: its values as a list, one chosen; a tap chooses another, Apply writes it */
void checkEnumChoices() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	if (session) session->setWritesEnabled(true);
	openSheet(phone, QStringLiteral("LED_MODE"));
	const bool list = phone.item("sheetChoices") && phone.item("sheetChoices")->isVisible() && phone.item("sheetField")
			&& !phone.item("sheetField")->isVisible();
	int rows = 0;
	forEachItem(phone.item("sheetChoices"), [&](QQuickItem *item) { rows += item->objectName().startsWith(QLatin1String("sheetChoice_")); });
	const QString before = modelValue(session, QStringLiteral("LED_MODE"));
	const QString wanted = before.startsWith(QLatin1String("blink")) ? QStringLiteral("1") : QStringLiteral("2");
	phone.tap(phone.item(wanted == QLatin1String("2") ? "sheetChoice_2" : "sheetChoice_1"));
	const bool picked = phone.object("registerSheet") && phone.object("registerSheet")->property("chosen").toString() == wanted;
	phone.tap(phone.item("sheetApply"));
	const bool written = QTest::qWaitFor([&] {
		return session && session->log()->contains(QStringLiteral("written: LED_MODE = %1").arg(wanted));
	}, 3000);
	check(list && rows == 3 && picked && written, QStringLiteral("phone, enum: LED_MODE's sheet lists its 3 values to choose from "
			"(no field); a tap chooses %1, Apply writes it (was \"%2\")").arg(wanted, before));
	if (session) session->disconnectLink();
}

/* the visible Texts with room to spare that are not at the start edge (in Arabic: the right), but the values, units and
 * other texts kept left to right on purpose */
QStringList notAtStart(QQuickWindow *window, const QString &where) {
	static const QStringList leftToRight{ QStringLiteral("regValue"), QStringLiteral("regUnit"), QStringLiteral("sheetUnit") };
	QStringList found;
	forEachItem(topItem(window), [&](QQuickItem *item) {
		if (!item->inherits("QQuickText") || !item->isVisible() || leftToRight.contains(item->objectName())) return;
		const QString text = item->property("text").toString();
		if (text.isEmpty() || item->width() <= item->property("contentWidth").toDouble() + 2) return;
		const int align = item->property("effectiveHorizontalAlignment").toInt();
		if (align != Qt::AlignRight && align != Qt::AlignHCenter)
			found << QStringLiteral("%1: \"%2\" (%3)").arg(where, text.left(24), item->objectName());
	});
	return found;
}

void checkArabicStart() {
	language::apply(*qApp, QStringLiteral("ar"));
	QStringList bad;
	{
		Phone phone(PORTRAIT);
		bad << notAtStart(phone.window, QStringLiteral("not connected"));
		PhoneSession *session = connectFake(phone);
		if (session) session->setWritesEnabled(true);
		phone.settle(600);
		bad << notAtStart(phone.window, QStringLiteral("registers"));
		openSheet(phone, QStringLiteral("FAN_SPEED"));
		phone.type("sheetField", QStringLiteral("150"));
		phone.tap(phone.item("sheetApply"));
		bad << notAtStart(phone.window, QStringLiteral("sheet"));
		closeSheet(phone);
		search(phone, QString());
		phone.openDrawer();
		bad << notAtStart(phone.window, QStringLiteral("drawer"));
		phone.closeDrawer();
		phone.tap(phone.item("tabLog"));
		bad << notAtStart(phone.window, QStringLiteral("log"));
		if (session) session->disconnectLink();
	}
	language::apply(*qApp, QStringLiteral("en"));
	bad.removeDuplicates();
	check(bad.isEmpty(), QStringLiteral("phone, Arabic: every text with room to spare at the start edge (the right), the "
			"map's Latin names too, on the Registers screen, in the sheet, the drawer and the Log; the values and units left "
			"to right %1").arg(bad.join(QStringLiteral(" | "))));
}

/* WCAG's contrast of two colours */
double contrast(const QColor &a, const QColor &b) {
	auto lum = [](const QColor &c) {
		auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
		return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
	};
	const double x = lum(a), y = lum(b);
	return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

/* the muted texts in view: each one's colour against what is drawn under it (the commonest colour of its box in the
 * picture), and its letters there (pixels far from that); the lowest contrast found, and any under 4.5:1 */
void mutedContrast(const Phone &phone, const QString &where, double &lowest, QStringList &bad, int &seen,
		QQuickItem *layer = nullptr) {
	const QImage image = phone.grab();
	const QColor muted = themeColor(&ThemeColors::muted);
	const QRectF view(0, 0, phone.window->width(), phone.window->height());
	/* with a popup open, only its own texts: the rest lies under its scrim or under it */
	forEachItem(layer ? layer : topItem(phone.window), [&](QQuickItem *item) {
		if (!item->inherits("QQuickText") || !item->isVisible() || item->opacity() < 0.99) return;
		if (item->property("text").toString().isEmpty() || !near(item->property("color").value<QColor>(), muted, 2)) return;
		const QRectF box = item->mapRectToScene(QRectF(0, 0, std::min(item->width(), item->property("contentWidth").toDouble()),
				std::min(item->height(), item->property("contentHeight").toDouble())));
		if (!view.contains(box) || box.width() < 4) return;
		QHash<QRgb, int> counts;
		for (double y = box.top() + 0.5; y < box.bottom(); y += 1)
			for (double x = box.left() + 0.5; x < box.right(); x += 1) counts[Phone::pixel(image, QPointF(x, y)).rgb()]++;
		QRgb under = 0;
		int most = 0;
		for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
			if (it.value() > most) {
				most = it.value();
				under = it.key();
			}
		int ink = 0;
		for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
			ink += distance(QColor(it.key()), QColor(under)) > 60 ? it.value() : 0;
		if (ink == 0) return; /* covered: the letters are not in the picture */
		++seen;
		const double ratio = contrast(muted, QColor(under));
		lowest = std::min(lowest, ratio);
		if (ratio < 4.5)
			bad << QStringLiteral("%1: \"%2\" %3 on %4").arg(where, item->property("text").toString().left(20)).arg(ratio, 0, 'f', 2)
							.arg(QColor(under).name());
	});
}

/* the drawer's USB / TCP choice: the chosen half's text white on the accent's fill, at least 4.5:1, in the picture, dark
 * and light (B.2b's light theme drew it black on blue); the other half's in the text colour */
void checkSegmentContrast() {
	QStringList seen;
	bool ok = true;
	for (bool dark : { true, false }) {
		QSettings().setValue(QStringLiteral("ui/dark"), dark);
		const ThemeColors colors = Theme::colorsFor(dark);
		Phone phone(PORTRAIT);
		if (PhoneSession *session = phone.session()) session->setTcp(true);
		phone.openDrawer();
		QQuickItem *chosen = phone.item("linkTcp"), *other = phone.item("linkUsb");
		const QQuickItem *label = chosen ? chosen->property("contentItem").value<QQuickItem *>() : nullptr;
		const QQuickItem *otherLabel = other ? other->property("contentItem").value<QQuickItem *>() : nullptr;
		const QColor text = label ? label->property("color").value<QColor>() : QColor();
		const QImage image = phone.grab();
		/* the fill: beside the text, inside the button's insets; the text's own pixels: near its colour */
		const QColor fill = Phone::pixel(image, at(chosen, 12, chosen ? chosen->height() / 2 : 0));
		int inked = 0;
		if (label)
			for (double y = 0.5; y < label->height(); y += 1)
				for (double x = 0.5; x < label->width(); x += 1)
					inked += distance(Phone::pixel(image, at(label, x, y)), text) < 60;
		const double ratio = contrast(text, fill);
		const bool otherText = otherLabel && near(otherLabel->property("color").value<QColor>(), colors.text, 2);
		const bool fine = near(fill, colors.accentFill, 6) && near(text, Qt::white, 2) && ratio >= 4.5 && inked >= 10
				&& otherText;
		seen << QStringLiteral("%1: %2 on %3, %4:1, %5 px").arg(dark ? "dark" : "light", text.name(), fill.name())
						.arg(ratio, 0, 'f', 2).arg(inked);
		ok = ok && fine;
	}
	QSettings().remove(QStringLiteral("ui/dark"));
	check(ok, QStringLiteral("phone, USB / TCP: the chosen half's text white on the accent's fill in the picture, at least "
			"4.5:1, dark and light; the other half's in the text colour (%1)").arg(seen.join(QStringLiteral("; "))));
}

/* this run's settings and recordings are its own (phone::useOwnFolder): an ini file in a folder of the run, not the
 * registry's key that two runs at once shared */
void checkOwnSettings() {
	QSettings settings;
	const QString file = settings.fileName();
	const QString folder = QFileInfo(PhoneSession::recordingsFolder()).absolutePath();
	settings.setValue(QStringLiteral("test/own"), 1);
	settings.sync();
	const bool written = QFileInfo::exists(file);
	settings.remove(QStringLiteral("test/own"));
	const bool own = settings.format() == QSettings::IniFormat && file.startsWith(folder)
			&& QDir(QDir::tempPath()).relativeFilePath(folder).startsWith(QLatin1String("..")) == false;
	check(own && written, QStringLiteral("phone, isolation: this run's settings in an ini file of its own folder (%1), its "
			"recordings beside it (%2): two runs at once share nothing").arg(file, PhoneSession::recordingsFolder()));
}

/* ---- B.5: the service, the screen kept on, the orientation, the Help, the back gesture */

/* the system's stand-in: what the session asked of Android's (phone_system.h), noted */
struct FakeSystem : SystemBridge {
	int holds = 0, releases = 0, toBack = 0, buzzes = 0;
	double scale = 1; /* the animator scale it says: 0, Remove animations */
	bool serviceUp = false, screenOn = false;
	QString text;
	QStringList orientations;
	void holdService(const QString &, const QString &line) override {
		++holds;
		serviceUp = true;
		text = line;
	}
	void releaseService() override {
		++releases;
		serviceUp = false;
	}
	void keepScreenOn(bool on) override { screenOn = on; }
	void moveToBack() override { ++toBack; }
	void haptic() override { ++buzzes; }
	double animationScale() override { return scale; }
	void requestOrientation(Orientation orientation) override {
		orientations << (orientation == Portrait ? QStringLiteral("portrait")
						: orientation == Landscape ? QStringLiteral("landscape") : QStringLiteral("any"));
	}
};

/* the service follows the link (A.4, A-6): up with the link's state while one is wanted, the recording's time in it,
 * gone after Disconnect; the screen kept on while connected (A-5), not with the setting off */
void checkService() {
	FakeSystem system;
	PhoneSession::setSystemBridgeForTests(&system);
	{
		Phone phone(PORTRAIT);
		const bool quietAtStart = !system.serviceUp && !system.screenOn && system.holds == 0;
		PhoneSession *session = connectFake(phone);
		const bool up = session && system.serviceUp && system.text.startsWith(QLatin1String("Connected: 127.0.0.1:"))
				&& system.screenOn && session->serviceText() == system.text;
		const QString connectedText = system.text;
		if (session) session->startRecord();
		const bool recording = QTest::qWaitFor([&] { return system.text.contains(QStringLiteral(" · recording 00:00:0")); }, 3000);
		const QString first = system.text;
		const bool ticks = QTest::qWaitFor([&] { return system.text != first; }, 2500);
		const QString recordingText = system.text;
		if (session) session->stopRecord();
		(void) QTest::qWaitFor([&] { return session && !session->recording(); }, 3000);
		const bool backToLink = QTest::qWaitFor([&] { return system.text == connectedText; }, 2000);
		/* the setting off: the screen let go while still connected, and saved */
		if (session) session->setKeepScreenOn(false);
		const bool screenLetGo = !system.screenOn && system.serviceUp
				&& !QSettings().value(QStringLiteral("android/keepScreenOn"), true).toBool();
		if (session) session->setKeepScreenOn(true);
		const bool screenAgain = system.screenOn;
		if (session) session->disconnectLink();
		const bool gone = QTest::qWaitFor([&] { return !system.serviceUp && system.releases >= 1; }, 3000) && !system.screenOn
				&& session && session->serviceText().isEmpty();
		check(quietAtStart && up && recording && ticks && backToLink && screenLetGo && screenAgain && gone,
				QStringLiteral("phone, service: up while a link is wanted with the link's state (\"%1\"), the recording's time in it "
						"once a second (\"%2\"), back to the link's state when it stops; gone after Disconnect; the screen kept on "
						"while connected, let go with the setting off (android/keepScreenOn saved); %3")
						.arg(connectedText, recordingText)
						.arg(QStringLiteral("start %1, up %2, recording %3, ticks %4, back %5, let go %6, again %7, gone %8")
								.arg(quietAtStart).arg(up).arg(recording).arg(ticks).arg(backToLink).arg(screenLetGo)
								.arg(screenAgain).arg(gone)));
	}
	PhoneSession::setSystemBridgeForTests(nullptr);
}

/* each tab's orientation (A-16): the Chart and the Monitor across, the Registers and the Log as the phone is held
 * (Android's unspecified: "any" here, the session's orientation ""), so going back from the Chart does not turn the
 * phone upright */
void checkOrientation() {
	FakeSystem system;
	PhoneSession::setSystemBridgeForTests(&system);
	QStringList seen;
	bool ok = true;
	{
		Phone phone(PORTRAIT);
		PhoneSession *session = phone.session();
		const char *tabs[] = { "tabChart", "tabRegisters", "tabMonitor", "tabLog" };
		const char *wanted[] = { "landscape", "any", "landscape", "any" };
		for (int i = 0; i < 4; ++i) {
			phone.tap(phone.item(tabs[i]));
			const QString asked = system.orientations.isEmpty() ? QString() : system.orientations.constLast();
			const QLatin1String held(i % 2 == 0 ? "landscape" : "");
			seen << QStringLiteral("%1 %2").arg(QLatin1String(tabs[i]).mid(3), asked);
			ok = ok && asked == QLatin1String(wanted[i]) && session && session->orientation() == held;
		}
	}
	PhoneSession::setSystemBridgeForTests(nullptr);
	check(ok, QStringLiteral("phone, orientation: each tab asks Android for its own: the Chart and the Monitor landscape, the "
			"Registers and the Log any, as the phone is held (%1)").arg(seen.join(QStringLiteral(", "))));
}

bool helpOpen(const Phone &phone) {
	const QObject *help = phone.object("helpScreen");
	return help && help->property("opened").toBool();
}

/* the drawer's Help button: the Help screen over the window, its list */
void openHelp(const Phone &phone) {
	phone.openDrawer();
	phone.tap(phone.item("helpButton"));
	(void) QTest::qWaitFor([&] { return helpOpen(phone) && phone.drawerClosed(); }, 3000);
	phone.settle();
}

/* every topic of the phone's Help in both languages: the list holds them all, each opens its page (the desktop's, translated), the
 * back button goes to the list; every text of the list inside its row */
void checkHelpTopics() {
	QStringList bad;
	int seen = 0;
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		Phone phone(PORTRAIT);
		openHelp(phone);
		QQuickItem *list = phone.item("helpList");
		const int rows = list ? list->property("count").toInt() : -1;
		if (!helpOpen(phone) || rows != help::phoneTopicCount()) bad << QStringLiteral("%1: %2 of %3 topics").arg(code).arg(rows).arg(help::phoneTopicCount());
		for (const QString &text : textsOutside(phone.window, seen)) bad << code + QStringLiteral(" list: ") + text;
		static const QRegularExpression arabic(QStringLiteral("[\\x{0600}-\\x{06FF}]"));
		for (int i = 0; i < help::phoneTopicCount(); ++i) {
			if (QObject *help = phone.object("helpScreen")) QMetaObject::invokeMethod(help, "show", Q_ARG(QVariant, i));
			phone.settle(150);
			QQuickItem *page = phone.item("helpText");
			const QString title = phone.text("helpTitle"), text = page ? page->property("text").toString() : QString();
			const double height = page ? page->property("contentHeight").toDouble() : 0;
			const bool translated = code == QLatin1String("en") || (title.contains(arabic) && text.contains(arabic));
			if (!page || !page->isVisible() || title != help::phoneTitle(i) || text.isEmpty() || height < 40 || !translated)
				bad << QStringLiteral("%1 topic %2 \"%3\": %4 px%5").arg(code).arg(i).arg(title).arg(height)
								.arg(translated ? "" : ", not translated");
		}
		phone.tap(phone.item("helpBack"));
		if (!phone.item("helpList") || !phone.item("helpList")->isVisible()) bad << code + QStringLiteral(": Back did not go to the list");
	}
	language::apply(*qApp, QStringLiteral("en"));
	check(bad.isEmpty(), QStringLiteral("phone, Help: the drawer's Help shows every topic of the phone's list (%1), each page "
			"in English and in Arabic, its title the topic's; Back goes to the list; the list's texts inside their rows %2")
			.arg(help::phoneTopicCount()).arg(bad.join(QStringLiteral(" | "))));
}


/* the topics the phone lists: its own first, then the desktop's that apply on a phone, none of the five desktop-only
 * ones (the Map editor, the API, the Dump, the command line, Keys & mouse); the desktop's dialog keeps its 14 topics,
 * in its order; the phone's topic in Arabic, its tags and markers as in the English */
void checkHelpPhoneList() {
	language::apply(*qApp, QStringLiteral("en"));
	const QStringList desktopTitles = { QStringLiteral("Getting started"), QStringLiteral("Connecting"),
			QStringLiteral("Polling & speed"), QStringLiteral("Fast streams"), QStringLiteral("Registers & writes"),
			QStringLiteral("Chart & recording"), QStringLiteral("Device maps"), QStringLiteral("Map editor"),
			QStringLiteral("API (MATLAB, LabVIEW, Python)"), QStringLiteral("Dump"), QStringLiteral("Monitor"),
			QStringLiteral("Log & pop-ups"), QStringLiteral("Command line"), QStringLiteral("Keys & mouse") };
	const QStringList desktopOnly = { QStringLiteral("Map editor"), QStringLiteral("API (MATLAB, LabVIEW, Python)"),
			QStringLiteral("Dump"), QStringLiteral("Command line"), QStringLiteral("Keys & mouse") };
	QStringList desktop, phone;
	for (int i = 0; i < help::topicCount(); ++i) desktop << help::title(i);
	for (int i = 0; i < help::phoneTopicCount(); ++i) phone << help::phoneTitle(i);
	QStringList wanted = { QStringLiteral("Using the phone app") };
	for (const QString &title : desktopTitles)
		if (!desktopOnly.contains(title)) wanted << title;
	const bool desktopSame = desktop == desktopTitles;
	const bool phoneList = phone == wanted;
	/* the desktop's pages are not the phone's: its own topic is in no page of the desktop */
	bool notOnDesktop = true;
	for (int i = 0; i < help::topicCount(); ++i)
		notOnDesktop = notOnDesktop && !help::page(i, Theme::colorsFor(true)).contains(QLatin1String("Using the phone app"));
	/* the phone's page in English and in Arabic: the same tags and markers (filled the same), Arabic in both */
	const auto tags = [](const QString &html) {
		static const QRegularExpression token(QStringLiteral("%[1-9][0-9]*|%[A-Z_]+%|<[^<>]+>"));
		QStringList found;
		for (auto it = token.globalMatch(html); it.hasNext();) found << it.next().captured();
		found.sort();
		return found;
	};
	const QString english = help::phonePage(0, Theme::colorsFor(true));
	language::apply(*qApp, QStringLiteral("ar"));
	const QString arabic = help::phonePage(0, Theme::colorsFor(true)), arabicTitle = help::phoneTitle(0);
	language::apply(*qApp, QStringLiteral("en"));
	static const QRegularExpression letters(QStringLiteral("[\x{0600}-\x{06FF}]"));
	const bool translated = arabic != english && arabic.contains(letters) && arabicTitle.contains(letters);
	const bool sameTags = !english.isEmpty() && tags(english) == tags(arabic);
	check(desktopSame && phoneList && notOnDesktop && translated && sameTags,
			QStringLiteral("phone, Help's list: \"Using the phone app\" first, then the desktop's topics but the Map editor, "
					"the API, the Dump, the command line and Keys & mouse (%1 topics: %2); the desktop's dialog keeps its %3 "
					"(same: %4, the phone's page in none: %5); the phone's page in Arabic (\"%6\": %7) with the English's "
					"tags and markers (%8)")
					.arg(phone.size()).arg(phone.join(QStringLiteral(", "))).arg(desktop.size()).arg(desktopSame)
					.arg(notOnDesktop).arg(arabicTitle).arg(translated).arg(sameTags));
}

/* Android's back (Qt::Key_Back), as the gesture sends it: accepted when it closed something. Not accepted to begin
 * with, as QGuiApplication delivers a key (a new event is accepted, and with no item in focus nothing ignores it: on
 * Linux the back at the root read as taken) */
bool pressBack(const Phone &phone) {
	QKeyEvent press(QEvent::KeyPress, Qt::Key_Back, Qt::NoModifier);
	press.setAccepted(false);
	QCoreApplication::sendEvent(phone.window, &press);
	QKeyEvent release(QEvent::KeyRelease, Qt::Key_Back, Qt::NoModifier);
	QCoreApplication::sendEvent(phone.window, &release);
	phone.settle();
	return press.isAccepted();
}

/* back closes in order: a Help page to its list, the Help, the drawer, the write sheet; with nothing open it is left to
 * Android, which leaves the app */
void checkBackOrder() {
	Phone phone(PORTRAIT);
	QStringList steps;
	openHelp(phone);
	if (QObject *help = phone.object("helpScreen")) QMetaObject::invokeMethod(help, "show", Q_ARG(QVariant, 2));
	phone.settle(150);
	pressBack(phone);
	const bool toList = helpOpen(phone) && phone.item("helpList")->isVisible();
	pressBack(phone);
	/* each popup's slide waited for by its state, not by a time: the Help gone before the menu button is tapped (while
	 * it fades it still covers the window), the drawer fully open before its back (drawerOpen: `opened`) */
	const QObject *help = phone.object("helpScreen");
	const bool helpClosed = !helpOpen(phone)
			&& QTest::qWaitFor([&] { return help && !help->property("visible").toBool(); }, 3000);
	phone.openDrawer();
	const bool drawerWasOpen = phone.drawerOpen();
	const bool drawerBack = pressBack(phone);
	const bool drawerClosed = QTest::qWaitFor([&] { return phone.drawerClosed(); }, 3000);
	openSheet(phone, QStringLiteral("FAN_SPEED"));
	const bool sheetWasOpen = sheetOpen(phone);
	const bool sheetBack = pressBack(phone);
	const bool sheetClosed
			= QTest::qWaitFor([&] { return !phone.object("registerSheet")->property("visible").toBool(); }, 3000);
	search(phone, QString());
	const bool leftToAndroid = !pressBack(phone);
	check(toList && helpClosed && drawerWasOpen && drawerBack && drawerClosed && sheetWasOpen && sheetBack && sheetClosed
					&& leftToAndroid,
			QStringLiteral("phone, back: a Help page to the list (%1), the Help closed (%2), the drawer closed (open %3, "
					"taken %4, closed %5), the write sheet closed (open %6, taken %7, closed %8); with nothing open the "
					"back is left to Android, which leaves the app (%9)")
					.arg(toList).arg(helpClosed).arg(drawerWasOpen).arg(drawerBack).arg(drawerClosed).arg(sheetWasOpen)
					.arg(sheetBack).arg(sheetClosed).arg(leftToAndroid));
}

/* the owner's phone (Android 16): connected, nothing open, one back finished the activity and the process with it, the
 * link, the service and a recording. With a link (and with a recording) the back at the root moves the app to the
 * background and is taken; with no link it is left to Android, which closes the app; the window stays meanwhile */
void checkBackToBackground() {
	FakeSystem system;
	PhoneSession::setSystemBridgeForTests(&system);
	bool connected = false, takenLinked = false, takenRecording = false, leftOff = false, windowStays = false;
	int linked = 0, recorded = 0, off = 0;
	QString where;
	{
		Phone phone(PORTRAIT);
		PhoneSession *session = connectFake(phone);
		connected = session && session->state() == PhoneSession::Connected;
		search(phone, QString());
		takenLinked = pressBack(phone);
		linked = system.toBack;
		if (session) session->startRecord();
		(void) QTest::qWaitFor([&] { return session && session->recording(); }, 3000);
		takenRecording = pressBack(phone);
		recorded = system.toBack;
		if (session) {
			session->stopRecord();
			(void) QTest::qWaitFor([&] { return !session->recording(); }, 3000);
			session->disconnectLink();
			(void) QTest::qWaitFor([&] { return session->state() == PhoneSession::Off; }, 3000);
		}
		leftOff = !pressBack(phone);
		off = system.toBack;
		windowStays = phone.window && phone.window->isVisible();
		const QQuickItem *focus = phone.window ? phone.window->activeFocusItem() : nullptr;
		where = QStringLiteral("in focus %1 %2, keyboard %3").arg(focus ? QLatin1String(focus->metaObject()->className()) : QLatin1String("none"),
				focus ? focus->objectName() : QString()).arg(QGuiApplication::inputMethod()->isVisible());
	}
	PhoneSession::setSystemBridgeForTests(nullptr);
	check(connected && takenLinked && linked == 1 && takenRecording && recorded == 2 && leftOff && off == 2 && windowStays,
			QStringLiteral("phone, back: with nothing open a back moves the app to the background while connected (taken %1, "
					"moved %2) and while recording (taken %3, moved %4), the window kept; with no link it is left to "
					"Android, which closes the app (left %5, moved %6; %7)")
					.arg(takenLinked).arg(linked).arg(takenRecording).arg(recorded).arg(leftOff).arg(off).arg(where));
}

/* the owner's phone: typed in the search field, its keyboard up, one back quit the app while connected. A back with the
 * keyboard up (one stood for) only hides it and takes the focus off the field, the link kept and nothing moved to the
 * background; the next back, the keyboard gone, is the root's (the app to the background) */
void checkBackKeyboard() {
	FakeSystem system;
	PhoneSession::setSystemBridgeForTests(&system);
	bool connected = false, focused = false, taken = false, unfocused = false, stayed = false, rootNext = false;
	{
		Phone phone(PORTRAIT);
		PhoneSession *session = connectFake(phone);
		connected = session && session->state() == PhoneSession::Connected;
		QQuickItem *field = phone.item("registerSearch");
		if (field) field->forceActiveFocus();
		phone.type("registerSearch", QStringLiteral("SUP"));
		fakeKeyboard(phone, 330);
		focused = field && field->hasActiveFocus();
		taken = pressBack(phone);
		unfocused = field && !field->hasActiveFocus();
		stayed = system.toBack == 0 && session && session->state() == PhoneSession::Connected;
		fakeKeyboard(phone, 0); /* Android hid it */
		rootNext = pressBack(phone) && system.toBack == 1;
		search(phone, QString());
	}
	PhoneSession::setSystemBridgeForTests(nullptr);
	check(connected && focused && taken && unfocused && stayed && rootNext,
			QStringLiteral("phone, back: with the keyboard up over the search field a back only hides it and takes the "
					"focus off the field (focused %1, taken %2, unfocused %3, still connected and not moved %4); the next "
					"back is the root's, the app to the background (%5)")
					.arg(focused).arg(taken).arg(unfocused).arg(stayed).arg(rootNext));
}

/* a tap on a row while the search field has the focus: the sheet, then the list has the focus, not the field (whose
 * keyboard came up again over the list after the old menu's Plot) */
void checkMenuFocus() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	QQuickItem *field = phone.item("registerSearch"), *list = phone.item("registerList");
	search(phone, QStringLiteral("SUPPLY_V"));
	if (field) field->forceActiveFocus();
	const bool focused = field && field->hasActiveFocus();
	phone.tap(regRow(phone, QStringLiteral("SUPPLY_V")));
	const bool opened = QTest::qWaitFor([&] { return sheetOpen(phone); }, 3000);
	phone.tap(phone.item("sheetPlot"));
	closeSheet(phone);
	const bool closed = !phone.object("registerSheet")->property("visible").toBool();
	phone.settle(200);
	const bool fieldLetGo = field && !field->hasActiveFocus();
	const bool listHas = list && list->hasActiveFocus();
	const int row = modelRow(session, QStringLiteral("SUPPLY_V"));
	if (session && row >= 0) session->setPlot(row, false);
	search(phone, QString());
	check(focused && opened && closed && fieldLetGo && listHas,
			QStringLiteral("phone, Registers: after a row's sheet (Plot) the focus goes to the list, not back to the search "
					"field and its keyboard (field focused before %1, sheet opened %2 and closed %3, field let go %4, the list "
					"has it %5)").arg(focused).arg(opened).arg(closed).arg(fieldLetGo).arg(listHas));
	if (session) session->disconnectLink();
}

/* a back while a popup is still sliding in (its `opened` not yet true) closes it, as one fully open: it went to
 * Android before, and the app went to the background with the drawer half open. The drawer and the Help, each caught
 * in its slide */
void checkBackWhileOpening() {
	Phone phone(PORTRAIT);
	QStringList seen;
	bool ok = true;
	for (const char *name : { "connectDrawer", "helpScreen" }) {
		QObject *popup = phone.object(name);
		if (!popup) {
			ok = false;
			seen << QLatin1String(name) + QStringLiteral(": not found");
			continue;
		}
		QMetaObject::invokeMethod(popup, "open");
		const bool caught = QTest::qWaitFor([&] {
			return popup->property("visible").toBool() && !popup->property("opened").toBool();
		}, 2000) && !popup->property("opened").toBool();
		const bool taken = pressBack(phone);
		const bool closed = QTest::qWaitFor([&] { return !popup->property("visible").toBool(); }, 3000);
		seen << QStringLiteral("%1: in its slide %2, back taken %3, closed %4").arg(QLatin1String(name)).arg(caught)
						.arg(taken).arg(closed);
		ok = ok && caught && taken && closed;
	}
	check(ok, QStringLiteral("phone, back: a popup still sliding in is closed by a back, not left to Android (%1)")
			.arg(seen.join(QStringLiteral("; "))));
}

/* ---- fixes from the review of the phone's line */

/* a board reset or plugged in again comes back under another name: Reconnect by itself finds it by its USB IDs */
void checkUsbBack() {
	FakeUsbBridge bridge;
	const QString renamed = QStringLiteral("/dev/bus/usb/001/003");
	bridge.list = { { USB_DEVICE, 0x0483, 0x5740, QString() } };
	PhoneSession::setUsbBridgeForTests(&bridge);
	{
		Phone phone(PORTRAIT);
		PhoneSession *session = phone.session();
		if (session) {
			session->setAutoReconnect(true);
			session->connectUsb(USB_DEVICE, 115200);
		}
		(void) QTest::qWaitFor([&] { return bridge.opens == 1 && bridge.sink; }, 3000);
		bridge.fromJava([](evre::UsbBridge::Sink *sink) { sink->usbOpened(); });
		const bool up = waitForState(session, PhoneSession::Connected, 3000);
		/* reset: gone, and back under the next number */
		bridge.list = { { renamed, 0x0483, 0x5740, QString() } };
		bridge.fromJava([](evre::UsbBridge::Sink *sink) { sink->usbGone(); });
		const bool again = QTest::qWaitFor([&] { return bridge.opens >= 2 && bridge.sink; }, 4000);
		const QString opened = bridge.lastOpened();
		bridge.fromJava([](evre::UsbBridge::Sink *sink) { sink->usbOpened(); });
		const bool back = waitForState(session, PhoneSession::Connected, 3000);
		const bool saved = QSettings().value(QStringLiteral("phone/usbDevice")).toString() == renamed;
		check(up && again && opened == renamed && back && saved && session->usbDevice() == renamed
						&& session->log()->contains(QStringLiteral("%1 is back as %2").arg(USB_DEVICE, renamed)),
				QStringLiteral("phone, USB back under another name: the board reset comes back as %1; the retry opens it by its USB "
						"IDs (\"%2\"), connected again, the new name saved and said in the Log").arg(renamed, opened));
		if (session) session->disconnectLink();
	}
	PhoneSession::setUsbBridgeForTests(nullptr);
}

/* A TCP device that takes the link and never answers: every request waits for its time */
class SilentDevice : public QObject {
public:
	std::atomic<quint16> port{ 0 };
	void start() {
		auto *server = new QTcpServer(this);
		QObject::connect(server, &QTcpServer::newConnection, this, [server] {
			while (QTcpSocket *client = server->nextPendingConnection()) {
				QObject::connect(client, &QTcpSocket::readyRead, client, [client] { client->readAll(); });
				QObject::connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
			}
		});
		if (server->listen(QHostAddress::LocalHost)) port = server->serverPort();
	}
};

/* no DEVICE_ID answer, then Disconnect: the drawer's line no longer says it is reading, and the read the disconnect
 * cancelled is not an error in the Log */
void checkDeviceLineAfterDisconnect() {
	OnThread<SilentDevice> device;
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	if (session) {
		session->setAutoReconnect(false);
		session->connectTcp(QStringLiteral("127.0.0.1"), device.stand->port, QString());
	}
	const bool up = waitForState(session, PhoneSession::Connected);
	const bool reading = QTest::qWaitFor([&] { return session->deviceText() == QStringLiteral("reading the device ID…"); }, 3000);
	if (session) session->disconnectLink();
	QTest::qWait(1500);
	const QString shown = phone.text("deviceText");
	const bool noError = session && !session->log()->contains(QStringLiteral("DEVICE_ID not read"));
	check(up && reading && shown == QLatin1String("Device ID not read yet") && noError,
			QStringLiteral("phone, device line after Disconnect: a device that never answers DEVICE_ID (\"reading the device "
					"ID…\"), then Disconnect: the drawer says \"%1\", the cancelled read is no error in the Log").arg(shown));
}

/* the write sheet takes a number as a phone's keypad types it: a decimal comma, Arabic digits; hex as typed */
void checkTypedNumbers() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	if (session) session->setWritesEnabled(true);
	QStringList seen;
	bool ok = bool(session);
	struct Case { const char *reg; QString typed; QString wanted; };
	const Case cases[] = { { "SETPOINT", QStringLiteral("1,5"), QStringLiteral("1.5") },
		{ "FAN_SPEED", QStringLiteral("١٢"), QStringLiteral("12") },
		{ "FAN_SPEED", QStringLiteral("۴۵"), QStringLiteral("45") },
		{ "FAN_SPEED", QStringLiteral("0x1A"), QStringLiteral("26") } };
	for (const Case &c : cases) {
		const QString reg = QLatin1String(c.reg);
		openSheet(phone, reg);
		phone.type("sheetField", c.typed);
		phone.tap(phone.item("sheetApply"));
		const bool read = QTest::qWaitFor([&] { return modelValue(session, reg) == c.wanted; }, 3000);
		seen << QStringLiteral("%1 \"%2\" -> %3 (%4)").arg(reg, c.typed, modelValue(session, reg), phone.text("sheetAnswer"));
		ok = ok && read;
		closeSheet(phone);
	}
	check(ok, QStringLiteral("phone, typed numbers: a decimal comma and Arabic digits written as numbers, hex as typed: %1")
			.arg(seen.join(QStringLiteral("; "))));
	if (session) session->disconnectLink();
}



void checkMutedContrast() {
	QStringList bad, lows;
	int seen = 0;
	for (bool dark : { true, false }) {
		QSettings().setValue(QStringLiteral("ui/dark"), dark);
		double lowest = 99;
		{
			Phone phone(PORTRAIT);
			mutedContrast(phone, QStringLiteral("not connected"), lowest, bad, seen);
			PhoneSession *session = connectFake(phone);
			if (session) session->setWritesEnabled(true);
			phone.settle(600);
			mutedContrast(phone, QStringLiteral("registers"), lowest, bad, seen);
			openSheet(phone, QStringLiteral("FAN_SPEED"));
			QQuickItem *sheetFlick = phone.item("sheetFlick");
			mutedContrast(phone, QStringLiteral("sheet"), lowest, bad, seen, sheetFlick ? sheetFlick->parentItem() : nullptr);
			closeSheet(phone);
			phone.openDrawer();
			QQuickItem *drawerFlick = phone.item("drawerFlick");
			mutedContrast(phone, QStringLiteral("drawer"), lowest, bad, seen, drawerFlick ? drawerFlick->parentItem() : nullptr);
			phone.closeDrawer();
			phone.tap(phone.item("tabLog"));
			mutedContrast(phone, QStringLiteral("log"), lowest, bad, seen);
			if (session) session->disconnectLink();
		}
		lows << QStringLiteral("%1 %2").arg(dark ? "dark" : "light").arg(lowest, 0, 'f', 2);
	}
	QSettings().remove(QStringLiteral("ui/dark"));
	check(bad.isEmpty() && seen >= 20, QStringLiteral("phone, contrast: every muted text in the picture at least 4.5:1 against "
			"what is drawn under it, dark and light (%1 texts, lowest %2) %3")
			.arg(seen).arg(lows.join(QStringLiteral(", ")), bad.join(QStringLiteral(" | "))));
}

/* ---- the chart, wired (B.3b): the session's lines, the legend, Y, fast lines, the empty state */

/* the legend's chip of a line, by its name */
QQuickItem *chip(const Phone &phone, const QString &name) {
	QQuickItem *legend = phone.item("legendView");
	QQuickItem *found = nullptr;
	forEachItem(legend, [&](QQuickItem *item) {
		if (found || item->objectName() != QLatin1String("legendChip")) return;
		const QQuickItem *label = within(item, QStringLiteral("chipName"));
		if (label && label->property("text").toString() == name) found = item;
	});
	return found;
}

int chipCount(const Phone &phone) {
	int n = 0;
	forEachItem(phone.item("legendView"), [&](QQuickItem *item) { n += item->objectName() == QLatin1String("legendChip"); });
	return n;
}

/* where a colour lies in the plot of a picture: its pixels, and the rows (device pixels) of the highest and lowest */
struct Seen {
	int pixels = 0, top = -1, bottom = -1, outside = 0;
};

QRectF plotInPicture(const ChartItem *chart, const QImage &image) {
	if (!chart) return QRectF();
	const qreal dpr = image.devicePixelRatio();
	const QRectF plot(chart->mapToScene(chart->plotRect().topLeft()), chart->plotRect().size());
	return QRectF(plot.x() * dpr, plot.y() * dpr, plot.width() * dpr, plot.height() * dpr);
}

/* outside: counted only inside `area` when one is given (the chart's area: the chips and buttons above it may show the
 * colour) */
Seen colourIn(const QImage &image, const QRectF &device, const QColor &colour, int tolerance = 8, const QRectF &area = QRectF()) {
	Seen seen;
	const QRectF room = device.adjusted(-1, -1, 1, 1);
	for (int y = 0; y < image.height(); ++y)
		for (int x = 0; x < image.width(); ++x) {
			if (!near(image.pixelColor(x, y), colour, tolerance)) continue;
			const QPointF p(x + 0.5, y + 0.5);
			if (device.contains(p)) {
				++seen.pixels;
				if (seen.top < 0) seen.top = y;
				seen.bottom = y;
			} else if (!room.contains(p) && (area.isNull() || area.contains(p))) {
				++seen.outside;
			}
		}
	return seen;
}

/* the fake device's SUPPLY_V and SUPPLY_I plotted (B.2's Plot: the session's setPlot), polled, then the link closed
 * (its lines and samples stay) and the view held: the session's lines on the chart, binned as the desktop bins */
void checkChartPlotted() {
	Phone phone(LANDSCAPE);
	PhoneSession *session = connectFake(phone);
	ChartItem *chart = showChart(phone);
	const int voltRow = modelRow(session, QStringLiteral("SUPPLY_V")), ampRow = modelRow(session, QStringLiteral("SUPPLY_I"));
	const bool plotted = session && voltRow >= 0 && ampRow >= 0 && session->setPlot(voltRow, true) && session->setPlot(ampRow, true);
	const int volt = session && voltRow >= 0 ? int(regKey(session->registers()->rows()[voltRow].def)) : -1;
	const int amp = session && ampRow >= 0 ? int(regKey(session->registers()->rows()[ampRow].def)) : -1;
	if (chart) chart->setWindowSeconds(10);
	QTest::qWait(2000);
	const qsizetype polled = chart && chart->series(volt) ? chart->series(volt)->times.size() : 0;
	/* the link closed: nothing more comes, the lines and their samples stay (the desktop's way) */
	if (session) session->disconnectLink();
	(void) waitForState(session, PhoneSession::Off, 3000);
	QTest::qWait(300);
	const qsizetype kept = chart && chart->series(volt) ? chart->series(volt)->times.size() : 0;
	if (chart) chart->setLive(false);
	chartFrame(phone, chart, 3);
	const ChartData::Series *s = chart ? chart->series(volt) : nullptr;
	const QVector<ChartData::Bin> phoneBins = chart ? chart->bins(volt) : QVector<ChartData::Bin>();
	const QVector<ChartData::Bin> desktop = s ? desktopBins(s->times, s->values, chart->viewStart(), chart->viewEnd(),
			chart->columns()) : QVector<ChartData::Bin>();
	const ThemeColors colors = Theme::colorsFor(true);
	const QColor first = chart ? chart->lineColour(volt) : QColor(), second = chart ? chart->lineColour(amp) : QColor();
	const bool coloured = first == colors.series.value(0) && second == colors.series.value(1);
	check(plotted && chart && chart->lineCount() == 2 && s && s->name == QLatin1String("SUPPLY_V")
					&& s->unit == QLatin1String("V") && polled >= 8 && !phoneBins.isEmpty() && sameBins(phoneBins, desktop)
					&& coloured,
			QStringLiteral("phone, chart: Plot on two registers gives two lines in the theme's first series colours (%1, %2), "
					"named with their units; the session's samples (%3 in 2 s at 100 ms) binned as the desktop chart bins "
					"them (%4 bins against %5)").arg(first.name(), second.name()).arg(polled).arg(phoneBins.size())
					.arg(desktop.size()));
	check(chart && chart->lineCount() == 2 && kept >= polled && kept > 0 && session && !session->connected(),
			QStringLiteral("phone, chart: a disconnect keeps the lines and their samples, as the desktop does (%1 samples "
					"before, %2 after)").arg(polled).arg(kept));

	/* the legend: a chip per line, its name and latest value in view with its unit (the desktop's number) */
	phone.settle(400);
	double latest = 0;
	const bool any = s && ChartData::latestInView(*s, chart->viewStart(), chart->viewEnd(), latest);
	const QString want = any ? ChartData::number(latest) + QStringLiteral(" V") : QStringLiteral("?");
	QQuickItem *voltChip = chip(phone, QStringLiteral("SUPPLY_V"));
	const QQuickItem *value = within(voltChip, QStringLiteral("chipValue"));
	const QString shown = value ? value->property("text").toString() : QString();
	check(chipCount(phone) == 2 && voltChip && chip(phone, QStringLiteral("SUPPLY_I")) && shown == want,
			QStringLiteral("phone, legend: a chip per line, SUPPLY_V's showing its latest value in view with its unit "
					"(\"%1\", want \"%2\")").arg(shown, want));
}

/* a fed line's chip tapped: the line hidden (none of its colour in the plot, the chip's dot a ring), tapped again:
 * drawn again */
void checkChartHide() {
	Phone phone(LANDSCAPE);
	ChartItem *chart = showFedChart(phone);
	QVector<double> times, values;
	sine(5, 20, times, values);
	if (chart) {
		chart->setWindowSeconds(10);
		chart->addLine(1, SINE_COLOUR, QStringLiteral("SINE"), QStringLiteral("V"));
		chart->appendSamples(1, times, values);
	}
	chartFrame(phone, chart, 3);
	phone.settle(300);
	QImage image = phone.grab();
	const int before = colourIn(image, plotInPicture(chart, image), SINE_COLOUR).pixels;
	QQuickItem *sineChip = chip(phone, QStringLiteral("SINE"));
	const QQuickItem *name = within(sineChip, QStringLiteral("chipName"));
	if (name) phone.tap(name->mapToScene(QPointF(name->width() / 2, name->height() / 2)));
	chartFrame(phone, chart, 3);
	const bool hidden = chart && !chart->lineShown(1);
	image = phone.grab();
	const int whileHidden = colourIn(image, plotInPicture(chart, image), SINE_COLOUR).pixels;
	const QQuickItem *dot = within(chip(phone, QStringLiteral("SINE")), QStringLiteral("chipDot"));
	const bool ring = dot && dot->property("color").value<QColor>().alpha() == 0;
	if (name) phone.tap(name->mapToScene(QPointF(name->width() / 2, name->height() / 2)));
	chartFrame(phone, chart, 3);
	image = phone.grab();
	const int after = colourIn(image, plotInPicture(chart, image), SINE_COLOUR).pixels;
	check(before > 100 && hidden && whileHidden == 0 && ring && chart->lineShown(1) && after > 100,
			QStringLiteral("phone, legend: a tap on a chip hides its line (%1 pixels of its colour in the plot, then %2; the "
					"chip's dot a ring: %4) and a second tap draws it again (%3, shown %5, %6 bins)").arg(before).arg(whileHidden)
					.arg(after).arg(ring).arg(chart && chart->lineShown(1)).arg(chart ? chart->bins(1).size() : -1));
}

/* the chip's "..." menu: Take off the chart unplots the register, as the card's menu does */
void checkChartUnplot() {
	Phone phone(LANDSCAPE);
	PhoneSession *session = phone.session();
	ChartItem *chart = showChart(phone);
	const int voltRow = modelRow(session, QStringLiteral("SUPPLY_V")), ampRow = modelRow(session, QStringLiteral("SUPPLY_I"));
	if (session && voltRow >= 0 && ampRow >= 0) {
		session->setPlot(voltRow, true);
		session->setPlot(ampRow, true);
	}
	phone.settle(300);
	const int linesBefore = chart ? chart->lineCount() : 0;
	QQuickItem *more = within(chip(phone, QStringLiteral("SUPPLY_V")), QStringLiteral("chipMore"));
	int dots = 0;
	forEachItem(more, [&](QQuickItem *item) { dots += item->objectName() == QLatin1String("chipMoreDot"); });
	/* touch targets: the chip and its "..." at least 48 dp (the drawn chip is 40) */
	const QQuickItem *toggle = within(chip(phone, QStringLiteral("SUPPLY_V")), QStringLiteral("chipToggle"));
	const bool targets = more && more->width() >= 47.5 && more->height() >= 47.5 && toggle && toggle->height() >= 47.5;
	phone.tap(more);
	QObject *menu = phone.object("chipMenu");
	const bool open = menu && menu->property("opened").toBool();
	const QString unplotText = phone.text("chipMenuUnplot");
	phone.tap(phone.item("chipMenuUnplot"));
	phone.settle(300);
	const bool off = session && voltRow >= 0 && !session->registers()->rows()[voltRow].plot;
	const bool ampStays = session && ampRow >= 0 && session->registers()->rows()[ampRow].plot;
	check(targets && linesBefore == 2 && dots == 3 && open && unplotText == QLatin1String("Take off the chart") && off && ampStays
					&& chart->lineCount() == 1 && chipCount(phone) == 1 && !chip(phone, QStringLiteral("SUPPLY_V"))
					&& phone.text("chartNoticeText") == QLatin1String("SUPPLY_V off the chart"),
			QStringLiteral("phone, legend: the chip's \"...\" button (three drawn dots: %1; it and the chip 48 dp high) opens a menu whose Take off the "
					"chart unplots the register (%2 lines -> %3, its card's Plot off, the other kept, \"%4\")")
					.arg(dots).arg(linesBefore).arg(chart ? chart->lineCount() : -1).arg(phone.text("chartNoticeText")));
	if (session && ampRow >= 0) session->setPlot(ampRow, false);
}

/* Clear: every line's samples go, the lines stay */
void checkChartClear() {
	Phone phone(LANDSCAPE);
	ChartItem *chart = showFedChart(phone);
	QVector<double> times, values;
	sine(5, 20, times, values);
	if (chart) {
		chart->addLine(1, SINE_COLOUR, QStringLiteral("SINE"), QStringLiteral("V"));
		chart->appendSamples(1, times, values);
	}
	chartFrame(phone, chart);
	const bool hadBins = chart && !chart->bins(1).isEmpty();
	phone.tap(phone.item("chartClear"));
	chartFrame(phone, chart);
	const QImage image = phone.grab();
	const int pixels = colourIn(image, plotInPicture(chart, image), SINE_COLOUR).pixels;
	check(hadBins && chart->lineCount() == 1 && chart->series(1) && chart->series(1)->times.isEmpty()
					&& chart->bins(1).isEmpty() && pixels == 0 && chipCount(phone) == 1,
			QStringLiteral("phone, chart: Clear empties every line (no samples, no bins, %1 pixels of it) and keeps the "
					"lines (%2, its chip too)").arg(pixels).arg(chart ? chart->lineCount() : -1));
}

/* Y: Auto spans the values in view; off, the sheet's Min and Max hold the axis, and the line's rows move to where
 * those values put them */
void checkChartYRange() {
	Phone phone(LANDSCAPE);
	ChartItem *chart = showFedChart(phone);
	QVector<double> times, values; /* 0.5 .. 3.5 */
	sine(5, 20, times, values);
	if (chart) {
		chart->setWindowSeconds(10);
		chart->addLine(1, SINE_COLOUR, QStringLiteral("SINE"), QStringLiteral("V"));
		chart->appendSamples(1, times, values);
	}
	chartFrame(phone, chart, 3);
	/* the line's highest and lowest rows where the axis puts 3.5 and 0.5, give or take the line's width and a pixel */
	auto rowsRight = [&](QString &said) {
		const QImage image = phone.grab();
		const qreal dpr = image.devicePixelRatio();
		const Seen seen = colourIn(image, plotInPicture(chart, image), SINE_COLOUR);
		const double top = chart->mapToScene(QPointF(0, chart->plotRect().bottom() - (3.5 - chart->yLow())
				/ (chart->yHigh() - chart->yLow()) * chart->plotRect().height())).y() * dpr;
		const double bottom = chart->mapToScene(QPointF(0, chart->plotRect().bottom() - (0.5 - chart->yLow())
				/ (chart->yHigh() - chart->yLow()) * chart->plotRect().height())).y() * dpr;
		const double slack = (ChartItem::LINE_WIDTH / 2 + 1.5) * dpr;
		said = QStringLiteral("Y %1 .. %2: rows %3 .. %4, want %5 .. %6").arg(chart->yLow(), 0, 'f', 2)
				.arg(chart->yHigh(), 0, 'f', 2).arg(seen.top).arg(seen.bottom).arg(top, 0, 'f', 1).arg(bottom, 0, 'f', 1);
		return seen.pixels > 0 && std::fabs(seen.top - top) <= slack && std::fabs(seen.bottom - bottom) <= slack;
	};
	QString autoSaid, manualSaid;
	const bool autoOn = chart && chart->yAuto();
	/* Auto: the values in view and a twentieth of their span each side */
	const bool spans = chart && std::fabs(chart->yLow() - (0.5 - 0.15)) < 0.02 && std::fabs(chart->yHigh() - (3.5 + 0.15)) < 0.02;
	const bool autoRows = chart && rowsRight(autoSaid);
	/* the Y button opens the sheet with the range drawn; Min 0 and Max 10 applied */
	const QString autoText = phone.text("yButton");
	phone.tap(phone.item("yButton"));
	QObject *sheet = phone.object("ySheet");
	const bool sheetOpen = sheet && sheet->property("opened").toBool();
	const bool stillAuto = chart && chart->yAuto(); /* nothing applied yet */
	phone.type("yMinField", QStringLiteral("0"));
	phone.type("yMaxField", QStringLiteral("10"));
	phone.tap(phone.item("ySheetApply"));
	chartFrame(phone, chart, 3);
	const bool manual = chart && !chart->yAuto() && chart->yLow() == 0 && chart->yHigh() == 10;
	const bool manualRows = chart && rowsRight(manualSaid);
	const QString rangeText = phone.text("yButton");
	/* a max under the min is refused with a reason */
	phone.tap(phone.item("yButton"));
	phone.type("yMinField", QStringLiteral("5"));
	phone.type("yMaxField", QStringLiteral("1"));
	phone.tap(phone.item("ySheetApply"));
	const QString refused = phone.text("ySheetError");
	/* Auto again: the sheet's Auto, the sheet closed */
	phone.tap(phone.item("ySheetAuto"));
	chartFrame(phone, chart, 3);
	const bool autoAgain = chart && chart->yAuto() && chart->yHigh() < 4 && !phone.object("ySheet")->property("visible").toBool()
			&& phone.text("yButton") == QLatin1String("Y auto");
	check(autoOn && spans && autoRows && sheetOpen && stillAuto && manual && manualRows && autoText == QLatin1String("Y auto")
					&& rangeText == QLatin1String("Y 0 to 10") && refused == QLatin1String("Max must be above min.") && autoAgain,
			QStringLiteral("phone, chart Y: Auto spans the values in view (%1, the button \"%5\"), its sheet's Min 0 and Max 10 "
					"hold it (%2; \"%3\"), a max under the min is refused (\"%4\"), the sheet's Auto spans the values again")
					.arg(autoSaid, manualSaid, rangeText, refused, autoText));
}

/* a fast line's bins as the desktop's check compares them (ChartView::freshBins against lastBins) */
bool sameFastBins(const QVector<ChartData::Bin> &a, const QVector<ChartData::Bin> &b) {
	if (a.size() != b.size()) return false;
	for (qsizetype i = 0; i < a.size(); ++i) {
		const ChartData::Bin &x = a[i], &y = b[i];
		if (x.t0 != y.t0 || x.t1 != y.t1 || x.min != y.min || x.max != y.max || x.count != y.count || x.gap != y.gap
				|| x.bar != y.bar)
			return false;
	}
	return true;
}

/* tests: evre_fake_fast on a port of its own (EVRE_FAKE_FAST_PORT; Docker holds 1221-1320 on the owner's PC) */
const quint16 FAKE_FAST_PORT = quint16(qEnvironmentVariableIsSet("EVRE_FAKE_FAST_PORT")
		? qEnvironmentVariableIntValue("EVRE_FAKE_FAST_PORT") : 1213);

/* a map with a stream (maps/example_fast.json) against evre_fake_fast: Fast lines lists its channels, one plotted
 * switches the stream on, its records drawn as the desktop's binFast bins them */
void checkChartFast() {
	const QString fastMap = phone::mapsFolder() + QStringLiteral("/example_fast.json");
	QProcess fake;
	fake.start(QCoreApplication::applicationDirPath() + QStringLiteral("/evre_fake_fast"),
			{ QString::number(FAKE_FAST_PORT), fastMap });
	const bool started = fake.waitForStarted(3000);
	/* listening: a busy run took longer than half a second to open the port */
	(void) QTest::qWaitFor([&] {
		QTcpSocket probe;
		probe.connectToHost(QStringLiteral("127.0.0.1"), FAKE_FAST_PORT);
		return probe.waitForConnected(200);
	}, 5000);
	QTest::qWait(300);
	{
		Phone phone(LANDSCAPE);
		PhoneSession *session = phone.session();
		ChartItem *chart = showChart(phone);
		const bool loaded = session && session->loadMap(fastMap);
		if (session) {
			session->setPolling(true);
			session->setIntervalMs(100);
			session->connectTcp(QStringLiteral("127.0.0.1"), FAKE_FAST_PORT, QString());
		}
		const bool connected = waitForState(session, PhoneSession::Connected);
		phone.settle(300);
		QQuickItem *button = phone.item("fastLinesButton");
		const bool offered = button && button->isVisible();
		phone.tap(button);
		QStringList names;
		QQuickItem *first = nullptr;
		forEachItem(topItem(phone.window), [&](QQuickItem *item) {
			if (item->objectName() != QLatin1String("fastChannel") || !item->isVisible()) return;
			names << item->property("text").toString();
			if (!first) first = item;
		});
		phone.tap(first);
		const int id = PhoneSession::fastLineId(0, 0);
		const bool line = chart && chart->hasLine(id);
		if (chart) chart->setWindowSeconds(10);
		(void) QTest::qWaitFor([&] {
			const fast::Store *store = session ? session->fastStore(0) : nullptr;
			return store && store->hasTime() && store->size() > 20000;
		}, 10000);
		/* nothing more comes while the bins are compared */
		if (session) session->disconnectLink();
		(void) waitForState(session, PhoneSession::Off, 3000);
		QTest::qWait(300);
		if (chart) chart->setLive(false);
		chartFrame(phone, chart, 3);
		const fast::Store *store = session ? session->fastStore(0) : nullptr;
		const qsizetype records = store ? store->size() : 0;
		const ChartData::Series *s = chart ? chart->series(id) : nullptr;
		ChartData::BinnedLine fresh;
		std::atomic<qint64> counted{ 0 };
		if (s) ChartData::binFast(*s, chart->viewStart(), chart->viewEnd(), chart->columns(), fresh, nullptr, counted);
		const QVector<ChartData::Bin> phoneBins = chart ? chart->bins(id) : QVector<ChartData::Bin>();
		phone.settle(300);
		const QImage image = phone.grab();
		const Seen seen = chart ? colourIn(image, plotInPicture(chart, image), chart->lineColour(id), 6,
				areaInPicture(phone, image)) : Seen();
		const QString chipName = chip(phone, QStringLiteral("ADC.I_LOAD")) ? QStringLiteral("ADC.I_LOAD") : QString();
		check(started && loaded && connected && offered && names == QStringList{ QStringLiteral("ADC.I_LOAD (A)"),
						QStringLiteral("ADC.V_BUS (V)") } && line && records > 20000 && s && s->fast
						&& !phoneBins.isEmpty() && sameFastBins(phoneBins, fresh.bins) && seen.pixels > 200 && seen.outside == 0
						&& !chipName.isEmpty(),
				QStringLiteral("phone, fast lines: a map with a stream offers Fast lines (%1); a channel chosen switches the "
						"stream on (%2 records from evre_fake_fast), its line binned as the desktop's binFast bins it (%3 "
						"bins against %4) and drawn in its colour inside the plot (%5 pixels, %6 elsewhere in the chart's area)")
						.arg(names.join(QStringLiteral(", "))).arg(records).arg(phoneBins.size()).arg(fresh.bins.size())
						.arg(seen.pixels).arg(seen.outside)
						+ (records > 20000 || !session ? QString() : QStringLiteral(" [the Log: %1]").arg(session->log()->text().right(400))));
		if (session) {
			session->setFastPlot(0, 0, false);
			session->loadMap(phone::mapsFolder() + QStringLiteral("/example_device.json"));
		}
	}
	fake.kill();
	fake.waitForFinished(3000);
}

/* no line: the plot shows the chart's icon, "No lines yet" and how to plot one; its button goes to the Registers screen */
void checkChartEmpty() {
	Phone phone(PORTRAIT);
	showChart(phone);
	QQuickItem *empty = phone.item("chartEmpty");
	const bool shown = empty && empty->isVisible() && phone.item("chartEmptyIcon") && phone.item("chartEmptyIcon")->isVisible()
			&& phone.item("chartEmptyIcon")->property("kind").toString() == QLatin1String("chart");
	const QString title = phone.text("chartEmptyTitle"), text = phone.text("chartEmptyText");
	const bool chartTab = phone.item("screens") && phone.item("screens")->property("currentIndex").toInt() == 1;
	phone.tap(phone.item("chartEmptyButton"));
	const int tab = phone.item("screens") ? phone.item("screens")->property("currentIndex").toInt() : -1;
	check(shown && chartTab && title == QLatin1String("No lines yet")
					&& text == QLatin1String("Tap a register on the Registers screen, then Plot.") && tab == 0,
			QStringLiteral("phone, chart: with no line the plot shows its icon, \"%1\" and \"%2\", and its Go to Registers "
					"button shows the Registers screen (tab %3)").arg(title, text).arg(tab));
}

/* both themes: a line in the theme's series colour, its chip on the surface with its name in the text colour */
void checkChartLegendThemes() {
	QStringList seen;
	bool ok = true;
	for (bool dark : { true, false }) {
		QSettings().setValue(QStringLiteral("ui/dark"), dark);
		Phone phone(LANDSCAPE);
		ChartItem *chart = showFedChart(phone);
		QVector<double> times, values;
		sine(5, 20, times, values);
		if (chart) {
			chart->setWindowSeconds(10);
			chart->addThemedLine(1, 0, QStringLiteral("SINE"), QStringLiteral("V"));
			chart->appendSamples(1, times, values);
		}
		chartFrame(phone, chart, 3);
		phone.settle(300);
		const ThemeColors colors = Theme::colorsFor(dark);
		const QImage image = phone.grab();
		const Seen line = colourIn(image, plotInPicture(chart, image), colors.series.value(0), 4);
		QQuickItem *sineChip = chip(phone, QStringLiteral("SINE"));
		const QQuickItem *name = within(sineChip, QStringLiteral("chipName"));
		/* the chip's face between its dot and its name's start: its colour */
		const QColor face = sineChip ? Phone::pixel(image, sineChip->mapToScene(QPointF(sineChip->width() - 4,
				sineChip->height() / 2))) : QColor();
		const QColor nameColour = name ? name->property("color").value<QColor>() : QColor();
		const bool good = line.pixels > 100 && near(face, colors.surface, 3) && nameColour == colors.text;
		seen << QStringLiteral("%1: line %2 px of %3, chip %4 (%5), name %6").arg(dark ? "dark" : "light")
				.arg(line.pixels).arg(colors.series.value(0).name(), face.name(), colors.surface.name(), nameColour.name());
		ok = ok && good;
	}
	QSettings().remove(QStringLiteral("ui/dark"));
	check(ok, QStringLiteral("phone, chart themes: the line in the theme's first series colour, its chip on the theme's "
			"surface with its name in the text colour, dark and light (%1)").arg(seen.join(QStringLiteral("; "))));
}

/* the chart's pictures for the eye: two plotted registers, a hidden line, Manual Y, the empty state, a chip's menu, six
 * lines (the legend at its start and at its end), a card's menu; both themes and languages, portrait and landscape:
 * <prefix>_<en|ar>_<dark|light>_chart_<case>_<portrait|landscape>.png */
void saveChartPictures(const QString &prefix) {
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		for (bool dark : { true, false }) {
			QSettings().setValue(QStringLiteral("ui/dark"), dark);
			for (QSize size : { PORTRAIT, LANDSCAPE }) {
				const QString name = QStringLiteral("%1_%2_%3_chart_").arg(prefix, code, dark ? "dark" : "light");
				const QString orient = size == PORTRAIT ? QStringLiteral("_portrait.png") : QStringLiteral("_landscape.png");
				{
					Phone phone(size);
					showChart(phone);
					phone.grab().save(name + QStringLiteral("empty") + orient);
				}
				Phone phone(size);
				PhoneSession *session = connectFake(phone);
				ChartItem *chart = showChart(phone);
				for (const char *reg : { "SUPPLY_V", "TEMPERATURE" }) {
					const int row = modelRow(session, QLatin1String(reg));
					if (session && row >= 0) session->setPlot(row, true);
				}
				if (chart) chart->setWindowSeconds(10);
				QTest::qWait(6000);
				phone.grab().save(name + QStringLiteral("two") + orient);
				const int row = modelRow(session, QStringLiteral("TEMPERATURE"));
				if (chart && session && row >= 0) chart->setLineShown(int(regKey(session->registers()->rows()[row].def)), false);
				phone.settle(500);
				phone.grab().save(name + QStringLiteral("hidden") + orient);
				if (chart) chart->setLineShown(int(regKey(session->registers()->rows()[row].def)), true);
				if (chart) chart->setYRange(0, 30);
				phone.settle(1500); /* the switch's handle slides slowly on a desktop of its own */
				phone.grab().save(name + QStringLiteral("manual") + orient);
				/* a popup's animations run slower on a desktop of its own: waited for, not timed */
				QObject *sheet = phone.object("ySheet");
				if (sheet) QMetaObject::invokeMethod(sheet, "openFor");
				(void) QTest::qWaitFor([&] { return sheet && sheet->property("opened").toBool(); }, 3000);
				phone.settle(300);
				phone.grab().save(name + QStringLiteral("ysheet") + orient);
				if (sheet) QMetaObject::invokeMethod(sheet, "close");
				(void) QTest::qWaitFor([&] { return sheet && !sheet->property("visible").toBool(); }, 3000);
				phone.tap(within(chip(phone, QStringLiteral("SUPPLY_V")), QStringLiteral("chipMore")));
				QObject *menu = phone.object("chipMenu");
				(void) QTest::qWaitFor([&] { return menu && menu->property("opened").toBool(); }, 3000);
				phone.settle(300);
				phone.grab().save(name + QStringLiteral("chipmenu") + orient);
				if (menu) QMetaObject::invokeMethod(menu, "close");
				(void) QTest::qWaitFor([&] { return menu && !menu->property("visible").toBool(); }, 3000);
				/* six lines: the legend scrolls (the fade at its end), then scrolled to its end */
				if (chart) chart->setProperty("yAuto", true);
				for (const char *reg : { "SUPPLY_I", "PRESSURE", "SETPOINT", "MOTOR_SPEED" }) {
					const int more = modelRow(session, QLatin1String(reg));
					if (session && more >= 0) session->setPlot(more, true);
				}
				QTest::qWait(4000);
				phone.grab().save(name + QStringLiteral("six") + orient);
				if (QObject *legend = phone.object("legendView")) QMetaObject::invokeMethod(legend, "positionViewAtEnd");
				phone.settle(500);
				phone.grab().save(name + QStringLiteral("sixend") + orient);
				/* a plotted register's sheet on the Registers screen: its trend, Take off the chart */
				phone.tap(phone.item("tabRegisters"));
				openSheet(phone, QStringLiteral("SUPPLY_V"));
				phone.settle(300);
				phone.grab().save(name + QStringLiteral("regsheet") + orient);
				closeSheet(phone);
				search(phone, QString());
				if (session) session->disconnectLink();
			}
		}
	}
	QSettings().remove(QStringLiteral("ui/dark"));
	language::apply(*qApp, QStringLiteral("en"));
}

/* ---- B.4: the cursors and their readouts, the Monitor */

/* a fed sine (and a second line, its double) in a 10 s view, the cursors placed by their button */
ChartItem *chartWithCursors(const Phone &phone, QVector<double> &times, QVector<double> &values) {
	ChartItem *chart = showFedChart(phone);
	sine(5, 20, times, values);
	QVector<double> doubled = values;
	for (double &v : doubled) v *= 2;
	if (chart) {
		chart->setWindowSeconds(10);
		chart->addLine(1, SINE_COLOUR, QStringLiteral("SINE"), QStringLiteral("V"));
		chart->addLine(2, Qt::cyan, QStringLiteral("DOUBLE"), QStringLiteral("A"));
		chart->appendSamples(1, times, values);
		chart->appendSamples(2, times, doubled);
	}
	chartFrame(phone, chart, 3);
	phone.tap(phone.item("cursorsButton"));
	chartFrame(phone, chart, 2);
	return chart;
}

/* a finger on an item's middle, dragged by dx in steps */
void dragItem(const Phone &phone, QQuickItem *on, double dx) {
	static QPointingDevice *finger = QTest::createTouchDevice();
	if (!on) return;
	const QPointF c = on->mapToScene(QPointF(on->width() / 2, on->height() / 2));
	QTest::touchEvent(phone.window, finger).press(0, c.toPoint(), phone.window.data());
	QTest::qWait(20);
	const int steps = 15;
	for (int i = 1; i <= steps; ++i) {
		QTest::touchEvent(phone.window, finger).move(0, (c + QPointF(dx * i / steps, 0)).toPoint(), phone.window.data());
		QTest::qWait(16);
	}
	QTest::touchEvent(phone.window, finger).release(0, (c + QPointF(dx, 0)).toPoint(), phone.window.data());
	QTest::qWait(80);
}

/* the readout row of a line, by its name */
QQuickItem *readoutRow(const Phone &phone, const QString &name) {
	QQuickItem *found = nullptr;
	forEachItem(phone.item("readoutPanel"), [&](QQuickItem *item) {
		if (found || item->objectName() != QLatin1String("readoutRow")) return;
		const QQuickItem *label = within(item, QStringLiteral("readoutName"));
		if (label && label->property("text").toString() == name) found = item;
	});
	return found;
}

QString readoutText(const Phone &phone, const QString &line, const char *part) {
	const QQuickItem *item = within(readoutRow(phone, line), QLatin1String(part));
	return item ? item->property("text").toString() : QString();
}

/* the samples of a line (times rising) in [t0, t1]: the first and the last there */
bool spanIn(const QVector<double> &times, double t0, double t1, double &first, double &last) {
	const qsizetype k0 = std::lower_bound(times.begin(), times.end(), t0) - times.begin();
	const qsizetype k1 = std::upper_bound(times.begin(), times.end(), t1) - times.begin() - 1;
	if (k0 > k1) return false;
	first = times[k0];
	last = times[k1];
	return true;
}

/* the Cursors button: A at a quarter and B at three quarters of the samples in view (here the whole view), the view
 * held; both drawn there with their handles, 48 dp; a second tap takes them off */
void checkCursorButtons() {
	Phone phone(LANDSCAPE);
	QVector<double> times, values;
	ChartItem *chart = chartWithCursors(phone, times, values);
	const double t0 = chart ? chart->viewStart() : 0, t1 = chart ? chart->viewEnd() : 0;
	double first = 0, last = 0;
	const bool inView = spanIn(times, t0, t1, first, last);
	const double wantA = first + (last - first) / 4, wantB = first + (last - first) * 3 / 4;
	const bool placed = chart && inView && chart->cursorsOn() && std::fabs(chart->cursorA() - wantA) < 1e-9
			&& std::fabs(chart->cursorB() - wantB) < 1e-9 && !chart->live();
	QQuickItem *lineA = phone.item("cursorA"), *handleA = phone.item("cursorHandleA"), *handleB = phone.item("cursorHandleB");
	const double plotLeft = chart ? chart->mapToScene(chart->plotRect().topLeft()).x() : 0;
	const double plotWidth = chart ? chart->plotRect().width() : 0;
	const double drawnA = lineA ? lineA->mapToScene(QPointF(lineA->width() / 2, 0)).x() : -1;
	const double wantX = t1 > t0 ? plotLeft + plotWidth * (wantA - t0) / (t1 - t0) : 0;
	const bool drawn = lineA && lineA->isVisible() && std::fabs(drawnA - wantX) <= 1;
	const bool handles = handleA && handleB && handleA->width() >= 47.5 && handleA->height() >= 47.5
			&& handleB->width() >= 47.5 && handleB->isVisible()
			&& handleA->mapToScene(QPointF(0, 0)).y() < handleB->mapToScene(QPointF(0, 0)).y();
	const bool panel = phone.item("readoutPanel") && phone.item("readoutPanel")->isVisible();
	const double a = chart ? chart->cursorA() : 0, b = chart ? chart->cursorB() : 0;
	phone.tap(phone.item("cursorsButton"));
	const bool off = chart && !chart->cursorsOn() && !phone.item("readoutPanel")->isVisible();
	check(placed && drawn && handles && panel && off,
			QStringLiteral("phone, cursors: the Cursors button puts A at a quarter and B at three quarters of the samples "
					"in view (%1 s, %2 s; samples %3 .. %4 s) and holds it; A drawn there (x %5, want %6), the handles "
					"48 dp, A's over B's; the readouts shown; a second tap takes them off")
					.arg(a, 0, 'f', 3).arg(b, 0, 'f', 3).arg(first, 0, 'f', 2).arg(last, 0, 'f', 2)
					.arg(drawnA, 0, 'f', 1).arg(wantX, 0, 'f', 1));
}

/* lines fed from the test: `count` sines (each its name, unit and colour) at 1 kHz from `from[i]` to `to[i]` s, in a
 * 10 s view, the cursors placed by their button */
ChartItem *chartWithLines(const Phone &phone, int count, const QVector<double> &from, const QVector<double> &to) {
	ChartItem *chart = showFedChart(phone);
	if (!chart) return nullptr;
	static const char *const names[] = { "SINE", "DOUBLE", "THIRD", "FOURTH", "FIFTH", "SIXTH" };
	static const char *const units[] = { "V", "A", "W", "°C", "Hz", "%" };
	static const QColor colours[] = { SINE_COLOUR, Qt::cyan, Qt::magenta, Qt::yellow, Qt::green, Qt::red };
	chart->setWindowSeconds(10);
	for (int i = 0; i < count && i < 6; ++i) {
		QVector<double> times, values;
		for (int k = int(from[i] * 1000); k <= int(to[i] * 1000); ++k) {
			times << k / 1000.0;
			values << (i + 1) * (2.0 + 1.5 * std::sin(2 * M_PI * 0.5 * k / 1000.0));
		}
		chart->addLine(i + 1, colours[i], QString::fromLatin1(names[i]), QString::fromUtf8(units[i]));
		chart->appendSamples(i + 1, times, values);
	}
	chartFrame(phone, chart, 3);
	phone.tap(phone.item("cursorsButton"));
	chartFrame(phone, chart, 2);
	return chart;
}

/* the owner's picture: the data started at 1.3 s of a view from -2.5 s, A sat before it and every A, B - A and the
 * span's min, max and mean read a dash. Two lines that start late in the view (6 s and 7 s of 0 .. 10 s): A and B
 * inside the span both have samples in, and every readout of both lines has a value */
void checkCursorLateData() {
	Phone phone(LANDSCAPE);
	ChartItem *chart = chartWithLines(phone, 2, { 6, 7 }, { 10, 10 });
	QStringList empty;
	for (const QString &name : { QStringLiteral("SINE"), QStringLiteral("DOUBLE") })
		for (const char *part : { "readoutA", "readoutB", "readoutDelta", "readoutMin", "readoutMax", "readoutMean" }) {
			const QString text = readoutText(phone, name, part);
			if (text.isEmpty() || text == QStringLiteral("\u2014")) empty << name + QLatin1Char(' ') + QLatin1String(part);
		}
	const double a = chart ? chart->cursorA() : 0, b = chart ? chart->cursorB() : 0;
	const bool inside = chart && chart->cursorsOn() && std::fabs(a - 7.75) < 1e-6 && std::fabs(b - 9.25) < 1e-6
			&& chart->viewStart() < 6;
	check(inside && empty.isEmpty(),
			QStringLiteral("phone, cursors: with the data starting late in the view (%1 .. %2 s; the lines from 6 s and 7 s) "
					"A and B are put where both have samples (A %3 s, B %4 s: a quarter and three quarters of 7 .. 10 s), and "
					"every readout of both lines has a value (without one: %5)")
					.arg(chart ? chart->viewStart() : 0, 0, 'f', 2).arg(chart ? chart->viewEnd() : 0, 0, 'f', 2)
					.arg(a, 0, 'f', 3).arg(b, 0, 'f', 3).arg(empty.isEmpty() ? QStringLiteral("none") : empty.join(QStringLiteral(", "))));
}

/* a finger dragging an item's middle by dy, then still until the flick stops */
void dragUp(const Phone &phone, QQuickItem *on, double dy) {
	static QPointingDevice *finger = QTest::createTouchDevice();
	if (!on) return;
	const QPointF c = on->mapToScene(QPointF(on->width() / 2, on->height() / 2));
	QTest::touchEvent(phone.window, finger).press(0, c.toPoint(), phone.window.data());
	QTest::qWait(20);
	const int steps = 15;
	for (int i = 1; i <= steps; ++i) {
		QTest::touchEvent(phone.window, finger).move(0, (c - QPointF(0, dy * i / steps)).toPoint(), phone.window.data());
		QTest::qWait(16);
	}
	QTest::qWait(120); /* still before the finger lifts: no fling */
	QTest::touchEvent(phone.window, finger).release(0, (c - QPointF(0, dy)).toPoint(), phone.window.data());
	(void) QTest::qWaitFor([on] { return !on->property("moving").toBool(); }, 3000);
}

/* the readouts never cut (the owner's landscape picture: the panel's last row cut through its letters at the foot): with
 * six lines, in landscape and in portrait, the readouts scroll: the content holds every row, a scroll bar stays shown,
 * a fade at the edge with more; each row is inside the visible part or reached by scrolling, and a finger scrolls the
 * last row fully into view */
void checkCursorPanelScroll() {
	QStringList seen;
	bool ok = true;
	for (QSize size : { LANDSCAPE, PORTRAIT }) {
		Phone phone(size);
		const QVector<double> from(6, 0.0), to(6, 10.0);
		ChartItem *chart = chartWithLines(phone, 6, from, to);
		phone.settle(300);
		QQuickItem *flick = phone.item("readoutFlick"), *bar = phone.item("readoutBar");
		QQuickItem *content = flick ? flick->property("contentItem").value<QQuickItem *>() : nullptr;
		QList<QQuickItem *> rows;
		forEachItem(phone.item("readoutPanel"), [&](QQuickItem *item) {
			if (item->objectName() == QLatin1String("readoutRow") && item->isVisible()) rows << item;
		});
		std::sort(rows.begin(), rows.end(), [content](QQuickItem *x, QQuickItem *y) {
			return x->mapToItem(content, QPointF(0, 0)).y() < y->mapToItem(content, QPointF(0, 0)).y();
		});
		const double height = flick ? flick->height() : 0, contentHeight = flick ? flick->property("contentHeight").toDouble() : 0;
		double rowsHeight = 0;
		int reachable = 0;
		for (QQuickItem *row : rows) {
			rowsHeight += row->height();
			const double bottom = row->mapToItem(content, QPointF(0, row->height())).y();
			if (bottom <= contentHeight + 0.5) ++reachable;
		}
		const bool overflows = contentHeight > height + 0.5;
		const bool barShown = bar && bar->isVisible() && bar->property("policy").toInt() == Qt::ScrollBarAlwaysOn
				&& bar->property("size").toDouble() < 1;
		const bool footFade = phone.item("readoutFadeFoot") && phone.item("readoutFadeFoot")->isVisible();
		for (int i = 0; i < 12 && flick && !flick->property("atYEnd").toBool(); ++i) dragUp(phone, flick, height * 0.6);
		phone.settle(200);
		const double contentY = flick ? flick->property("contentY").toDouble() : 0;
		const double lastTop = rows.isEmpty() ? -1 : rows.last()->mapToItem(content, QPointF(0, 0)).y();
		const double lastBottom = rows.isEmpty() ? -1 : lastTop + rows.last()->height();
		const bool lastShown = !rows.isEmpty() && lastTop >= contentY - 0.5 && lastBottom <= contentY + height + 0.5;
		const bool topFade = phone.item("readoutFadeTop") && phone.item("readoutFadeTop")->isVisible()
				&& !phone.item("readoutFadeFoot")->isVisible();
		const bool good = chart && rows.size() == 6 && contentHeight >= rowsHeight && reachable == rows.size() && overflows
				&& barShown && footFade && lastShown && topFade;
		seen << QStringLiteral("%1: %2 rows (%3 reachable), content %4 >= rows %5, room %6, bar %7, fade at the foot %8; "
							   "scrolled to %9: the last row %10 .. %11 inside, the fade at the top %12")
						.arg(size == LANDSCAPE ? "landscape" : "portrait").arg(rows.size()).arg(reachable)
						.arg(contentHeight, 0, 'f', 0).arg(rowsHeight, 0, 'f', 0).arg(height, 0, 'f', 0).arg(barShown)
						.arg(footFade).arg(contentY, 0, 'f', 0).arg(lastTop, 0, 'f', 0).arg(lastBottom, 0, 'f', 0).arg(topFade);
		ok = ok && good;
	}
	check(ok, QStringLiteral("phone, cursors: six lines' readouts scroll, never cut: the content holds every row, a scroll "
			"bar stays shown, a fade at the edge with more, the last row scrolled fully into view (%1)")
			.arg(seen.join(QStringLiteral("; "))));
}

/* the pairs in both languages: each label beside its value as one piece, its own gap to the next pair; a symbol's pair
 * (A, B, B - A) left to right in Arabic too ("B 5.11 V", not "5.11 V B"), a word's (min) in the language's direction;
 * in Arabic the pairs run from the right (A's right of B's). A line with no value at B: an em dash, never an en dash
 * (beside "B - A" it read as one more minus) */
void checkCursorPairs() {
	QStringList bad;
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		const bool rtl = code == QLatin1String("ar");
		Phone phone(PORTRAIT);
		/* SINE 1 .. 4 s, DOUBLE 6 .. 9 s: they share no span, so A and B are put over 1 .. 9 s, SINE has no value at B */
		ChartItem *chart = chartWithLines(phone, 2, { 1, 6 }, { 4, 9 });
		phone.settle(300);
		QQuickItem *row = readoutRow(phone, QStringLiteral("SINE"));
		const auto x = [&](const char *name) {
			const QQuickItem *item = within(row, QLatin1String(name));
			return item ? item->mapToScene(QPointF(0, 0)).x() : -1.0;
		};
		const auto labelX = [&](const char *value) {
			const QQuickItem *v = within(row, QLatin1String(value));
			const QQuickItem *pairRow = v ? v->parentItem() : nullptr;
			const QQuickItem *label = pairRow && !pairRow->childItems().isEmpty() ? pairRow->childItems().first() : nullptr;
			return label ? label->mapToScene(QPointF(0, 0)).x() : -1.0;
		};
		if (!chart || !row) {
			bad << code + QStringLiteral(": no readouts");
			continue;
		}
		for (const char *symbol : { "readoutA", "readoutB", "readoutDelta" })
			if (!(labelX(symbol) < x(symbol))) bad << code + QStringLiteral(": ") + QLatin1String(symbol) + QStringLiteral(" not label first, left to right");
		if (rtl ? !(labelX("readoutMin") > x("readoutMin")) : !(labelX("readoutMin") < x("readoutMin")))
			bad << code + QStringLiteral(": min's label not at the language's start");
		if (rtl ? !(x("readoutA") > x("readoutB")) : !(x("readoutA") < x("readoutB")))
			bad << code + QStringLiteral(": the pairs not from the start edge");
		const QString atB = readoutText(phone, QStringLiteral("SINE"), "readoutB");
		const QString delta = readoutText(phone, QStringLiteral("SINE"), "readoutDelta");
		if (atB != QStringLiteral("\u2014") || delta != QStringLiteral("\u2014"))
			bad << code + QStringLiteral(": no value reads \"%1\", \"%2\"").arg(atB, delta);
		/* the gap between two pairs wider than a label's to its value */
		const QQuickItem *bValue = within(row, QStringLiteral("readoutB"));
		const QQuickItem *deltaValue = within(row, QStringLiteral("readoutDelta"));
		if (bValue && deltaValue && bValue->parentItem() && deltaValue->parentItem()) {
			const QRectF b = bValue->parentItem()->mapRectToScene(bValue->parentItem()->childrenRect());
			const QRectF d = deltaValue->parentItem()->mapRectToScene(deltaValue->parentItem()->childrenRect());
			const double gap = rtl ? b.left() - d.right() : d.left() - b.right();
			if (std::fabs(b.top() - d.top()) < 1 && gap < 10) bad << code + QStringLiteral(": pairs %1 apart").arg(gap);
		}
	}
	language::apply(*qApp, QStringLiteral("en"));
	check(bad.isEmpty(), QStringLiteral("phone, cursors: each readout's label and value one piece, A, B and B - A left to "
			"right in Arabic too, a word's label at the language's start, the pairs from the start edge, 10 dp and more "
			"apart; no value an em dash (%1)").arg(bad.isEmpty() ? QStringLiteral("so in English and Arabic") : bad.join(QStringLiteral("; "))));
}

/* a drag on A's handle moves A by the finger's time (its pixels in the view's seconds), B stays, the view stays */
void checkCursorDrag() {
	Phone phone(LANDSCAPE);
	QVector<double> times, values;
	ChartItem *chart = chartWithCursors(phone, times, values);
	const double aBefore = chart ? chart->cursorA() : 0, bBefore = chart ? chart->cursorB() : 0;
	const double endBefore = chart ? chart->viewEnd() : 0;
	const double perPixel = chart && chart->plotRect().width() > 0 ? chart->windowSeconds() / chart->plotRect().width() : 0;
	dragItem(phone, phone.item("cursorHandleA"), 120);
	chartFrame(phone, chart);
	const double moved = chart ? chart->cursorA() - aBefore : 0, want = 120 * perPixel;
	/* B's handle to the left past A: the cursors may cross (B - A negative) */
	const bool bSame = chart && chart->cursorB() == bBefore;
	const bool viewSame = chart && chart->viewEnd() == endBefore && !chart->live();
	check(chart && std::fabs(moved - want) <= 2 * perPixel && bSame && viewSame,
			QStringLiteral("phone, cursors: a drag of A's handle by 120 px moves A by %1 s (want %2 s: the view's seconds a "
					"pixel), B and the view stay (A %3 -> %4, B %5 -> %6, end %7 -> %8)").arg(moved, 0, 'f', 4).arg(want, 0, 'f', 4)
					.arg(aBefore).arg(chart ? chart->cursorA() : 0).arg(bBefore).arg(chart ? chart->cursorB() : 0)
					.arg(endBefore).arg(chart ? chart->viewEnd() : 0));
}

/* the readouts: each line's values at A and B, B - A, min, max and mean over A..B equal the desktop's statsOf over the
 * span, written as the legend writes a value; the times and the span over them */
void checkCursorReadouts() {
	Phone phone(PORTRAIT);
	QVector<double> times, values;
	ChartItem *chart = chartWithCursors(phone, times, values);
	phone.settle(300);
	bool ok = chart != nullptr;
	QStringList seen;
	for (const auto &[id, name, unit] : { std::tuple{ 1, QStringLiteral("SINE"), QStringLiteral(" V") },
			 std::tuple{ 2, QStringLiteral("DOUBLE"), QStringLiteral(" A") } }) {
		const ChartData::Series *s = chart ? chart->series(id) : nullptr;
		if (!s) {
			ok = false;
			continue;
		}
		const double a = chart->cursorA(), b = chart->cursorB();
		const ChartData::Stats want = ChartData::statsOf(s->times, s->values, std::min(a, b), std::max(a, b), a, b);
		const ChartData::Stats got = chart->cursorStats(id);
		const bool same = want.ok && got.ok && want.atA == got.atA && want.atB == got.atB && want.min == got.min
				&& want.max == got.max && want.mean == got.mean;
		const bool texts = readoutText(phone, name, "readoutA") == ChartData::number(want.atA) + unit
				&& readoutText(phone, name, "readoutB") == ChartData::number(want.atB) + unit
				&& readoutText(phone, name, "readoutDelta") == ChartData::number(want.atB - want.atA) + unit
				&& readoutText(phone, name, "readoutMin") == ChartData::number(want.min) + unit
				&& readoutText(phone, name, "readoutMax") == ChartData::number(want.max) + unit
				&& readoutText(phone, name, "readoutMean") == ChartData::number(want.mean) + unit;
		seen << QStringLiteral("%1: A %2, B %3, min %4, max %5, mean %6").arg(name, readoutText(phone, name, "readoutA"),
				readoutText(phone, name, "readoutB"), readoutText(phone, name, "readoutMin"),
				readoutText(phone, name, "readoutMax"), readoutText(phone, name, "readoutMean"));
		ok = ok && same && texts;
	}
	const QString span = phone.text("readoutSpan");
	const bool times_ = chart && phone.text("readoutTimeA") == chart->cursorAText() && !chart->cursorAText().isEmpty()
			&& span == ChartData::number(chart->cursorB() - chart->cursorA()) + QStringLiteral(" s");
	check(ok && times_, QStringLiteral("phone, cursors: the readouts equal the desktop's statsOf over A..B, as the legend "
			"writes a value (%1; A at %2, B - A %3)").arg(seen.join(QStringLiteral("; ")), phone.text("readoutTimeA"), span));
}

/* the Monitor against the fake device: frames arrive (each its time, its direction, its bytes) while the screen shows;
 * Pause holds them (none added, the engine writes none), Resume goes on, Clear empties; another screen stops them */
void checkMonitorFrames() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	phone.tap(phone.item("tabMonitor"));
	FrameLog *frames = session ? session->frames() : nullptr;
	const bool arrived = frames && QTest::qWaitFor([&] { return frames->count() >= 10; }, 4000);
	static const QRegularExpression shape(QStringLiteral("^\\d\\d:\\d\\d:\\d\\d\\.\\d{3}  (TX|RX)  ([0-9A-F]{2} )+[0-9A-F]{2}"));
	const QString first = frames ? frames->line(0) : QString();
	const bool parsed = shape.match(first).hasMatch();
	const bool monitoring = session && session->monitoring();
	phone.tap(phone.item("monitorPause"));
	QTest::qWait(300); /* what was under way comes in */
	const qint64 held = frames ? frames->added() : 0;
	QTest::qWait(1200);
	const bool holds = frames && frames->added() == held && frames->paused() && !session->monitoring()
			&& phone.text("monitorPause") == QLatin1String("Resume");
	/* Clear while paused: nothing comes in after it */
	phone.tap(phone.item("monitorClear"));
	const int afterClear = frames ? frames->count() : -1;
	phone.tap(phone.item("monitorPause"));
	const bool again = frames && QTest::qWaitFor([&] { return frames->added() > held + 5 && frames->count() > 5; }, 4000);
	phone.tap(phone.item("tabLog"));
	QTest::qWait(300);
	const bool stopped = session && !session->monitoring();
	check(arrived && parsed && monitoring && holds && again && afterClear == 0 && stopped,
			QStringLiteral("phone, Monitor: the fake device's frames arrive while the screen shows (\"%1\"), Pause holds "
					"them (none added in 1.2 s, the engine writes none), Clear empties (%2 left), Resume goes on, another "
					"screen stops them").arg(first.left(60)).arg(afterClear));
	if (session) session->disconnectLink();
}

/* the owner's phone: the Monitor's bytes in Roboto ("monospace" is not a family Qt finds on Android), the "1"s narrow
 * and the bytes of the rows out of line. The bytes, the time and the direction in a fixed-pitch family the font
 * database has (PhoneTheme.monoFamily): QFontInfo says fixed pitch, "1111" as wide as "WWWW" */
void checkMonitorMono() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	phone.tap(phone.item("tabMonitor"));
	(void) QTest::qWaitFor([&] { return session && session->frames()->count() >= 3; }, 4000);
	phone.settle(300);
	QStringList seen;
	bool ok = true;
	for (const char *name : { "frameBytes", "frameTime", "frameDirection" }) {
		QQuickItem *text = nullptr;
		forEachItem(phone.item("monitorList"), [&](QQuickItem *item) {
			if (!text && item->objectName() == QLatin1String(name) && item->isVisible()) text = item;
		});
		const QFont font = text ? text->property("font").value<QFont>() : QFont();
		const QFontInfo info(font);
		const QFontMetricsF metrics(font);
		const double ones = metrics.horizontalAdvance(QStringLiteral("1111")), ws = metrics.horizontalAdvance(QStringLiteral("WWWW"));
		const bool good = text && info.fixedPitch() && std::fabs(ones - ws) < 0.5;
		seen << QStringLiteral("%1: %2, fixed %3, 1111 %4 px, WWWW %5 px").arg(QLatin1String(name), info.family())
						.arg(info.fixedPitch()).arg(ones, 0, 'f', 1).arg(ws, 0, 'f', 1);
		ok = ok && good;
	}
	if (session) session->disconnectLink();
	check(ok, QStringLiteral("phone, Monitor: the frames in a fixed-pitch font the phone has (%1; %2)")
			.arg(PhoneTheme::monoFamily(), seen.join(QStringLiteral("; "))));
}

/* not connected: the Monitor says so and offers the drawer */
void checkMonitorEmpty() {
	Phone phone(PORTRAIT);
	phone.tap(phone.item("tabMonitor"));
	const bool shown = phone.item("monitorEmptyState") && phone.item("monitorEmptyState")->isVisible();
	const QString text = phone.text("monitorEmpty");
	phone.tap(phone.item("monitorConnect"));
	(void) QTest::qWaitFor([&] { return phone.drawerOpen(); }, 3000);
	check(shown && text == QLatin1String("Not connected: the frames on the link show here once it is up.")
					&& phone.drawerOpen(),
			QStringLiteral("phone, Monitor: not connected it says \"%1\", and Connect… opens the drawer").arg(text));
}

/* both languages at 412 px, the cursors' readouts and the Monitor's frames: nothing cut; in Arabic the frames and the
 * plot left to right, the readouts' labels translated; both themes: the frames in the text colour on the background */
void checkToolsTexts() {
	QStringList bad, latin, colours;
	int seen = 0;
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		for (bool dark : { true, false }) {
			QSettings().setValue(QStringLiteral("ui/dark"), dark);
			Phone phone(PORTRAIT);
			PhoneSession *session = connectFake(phone);
			phone.tap(phone.item("tabMonitor"));
			(void) QTest::qWaitFor([&] { return session && session->frames()->count() >= 10; }, 4000);
			phone.settle(300);
			for (const QString &text : textsOutside(phone.window, seen)) bad << code + QStringLiteral(" Monitor: ") + text;
			if (code == QLatin1String("ar")) latin << latinTexts(phone.window);
			/* the newest frame's bytes: left to right, at the left; in the theme's text colour */
			QQuickItem *bytes = nullptr;
			forEachItem(phone.item("monitorList"), [&](QQuickItem *item) {
				if (item->objectName() == QLatin1String("frameBytes") && item->isVisible()) bytes = item;
			});
			const bool ltr = bytes && bytes->mapToScene(QPointF(0, 0)).x() < phone.window->width() / 4.0
					&& bytes->property("effectiveHorizontalAlignment").toInt() == Qt::AlignLeft;
			const QColor colour = bytes ? bytes->property("color").value<QColor>() : QColor();
			const ThemeColors theme = Theme::colorsFor(dark);
			const QImage image = phone.grab();
			/* in the list's left margin (the frames start 16 dp in; the scroll indicator is at the right edge) */
			const QColor background = Phone::pixel(image, QPointF(6, phone.window->height() / 2.0));
			if (!ltr || colour != theme.text || !near(background, theme.bg, 3))
				colours << QStringLiteral("%1 %2: left to right %3, bytes %4, background %5").arg(code, dark ? "dark" : "light")
						.arg(ltr).arg(colour.name(), background.name());
			/* the chart with the cursors: two plotted registers, their readouts */
			for (const char *reg : { "SUPPLY_V", "SUPPLY_I" }) {
				const int row = modelRow(session, QLatin1String(reg));
				if (session && row >= 0) session->setPlot(row, true);
			}
			ChartItem *chart = showChart(phone);
			if (chart) chart->setWindowSeconds(10);
			QTest::qWait(1500);
			phone.tap(phone.item("cursorsButton"));
			phone.settle(500);
			for (const QString &text : textsOutside(phone.window, seen)) bad << code + QStringLiteral(" cursors: ") + text;
			if (code == QLatin1String("ar")) latin << latinTexts(phone.window);
			if (session) session->disconnectLink();
		}
	}
	QSettings().remove(QStringLiteral("ui/dark"));
	language::apply(*qApp, QStringLiteral("en"));
	latin.removeDuplicates();
	check(bad.isEmpty() && latin.isEmpty() && colours.isEmpty() && seen > 40,
			QStringLiteral("phone, cursors and Monitor at 412 px, English and Arabic, dark and light: every Text inside its "
					"parent and not cut (%1 seen), every text translated, the frames left to right in the theme's text "
					"colour %2 %3 %4").arg(seen).arg(bad.join(QStringLiteral(" | ")), latin.join(QStringLiteral(" | ")),
					colours.join(QStringLiteral(" | "))));
}

/* the pictures for the eye: the Chart screen with the cursors (two registers; six, and six scrolled to the readouts'
 * end), the Monitor live and paused; both languages and themes, portrait and landscape:
 * <prefix>_<en|ar>_<dark|light>_<cursors|cursors6|cursors6end|monitor|paused>_<orientation>.png */
void saveToolsPictures(const QString &prefix) {
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		for (bool dark : { true, false }) {
			QSettings().setValue(QStringLiteral("ui/dark"), dark);
			for (QSize size : { PORTRAIT, LANDSCAPE }) {
				const QString name = QStringLiteral("%1_%2_%3_").arg(prefix, code, dark ? "dark" : "light");
				const QString orient = size == PORTRAIT ? QStringLiteral("_portrait.png") : QStringLiteral("_landscape.png");
				Phone phone(size);
				PhoneSession *session = connectFake(phone);
				ChartItem *chart = showChart(phone);
				for (const char *reg : { "SUPPLY_V", "TEMPERATURE" }) {
					const int row = modelRow(session, QLatin1String(reg));
					if (session && row >= 0) session->setPlot(row, true);
				}
				if (chart) chart->setWindowSeconds(10);
				QTest::qWait(6000);
				phone.tap(phone.item("cursorsButton"));
				phone.settle(800);
				phone.grab().save(name + QStringLiteral("cursors") + orient);
				/* six lines: the readouts scroll (the fade at the foot), then scrolled to their end */
				for (const char *reg : { "SUPPLY_I", "PRESSURE", "SETPOINT", "MOTOR_SPEED" }) {
					const int row = modelRow(session, QLatin1String(reg));
					if (session && row >= 0) session->setPlot(row, true);
				}
				phone.tap(phone.item("cursorsButton")); /* off; live again, so the new lines' samples come into view */
				if (chart) chart->setLive(true);
				QTest::qWait(4000);
				phone.tap(phone.item("cursorsButton"));
				phone.settle(800);
				phone.grab().save(name + QStringLiteral("cursors6") + orient);
				if (QQuickItem *flick = phone.item("readoutFlick")) {
					flick->setProperty("contentY", std::max(0.0, flick->property("contentHeight").toDouble() - flick->height()));
					phone.settle(500);
				}
				phone.grab().save(name + QStringLiteral("cursors6end") + orient);
				for (const char *reg : { "SUPPLY_I", "PRESSURE", "SETPOINT", "MOTOR_SPEED" }) {
					const int row = modelRow(session, QLatin1String(reg));
					if (session && row >= 0) session->setPlot(row, false);
				}
				phone.tap(phone.item("tabMonitor"));
				QTest::qWait(1500);
				phone.grab().save(name + QStringLiteral("monitor") + orient);
				phone.tap(phone.item("monitorPause"));
				phone.settle(500);
				phone.grab().save(name + QStringLiteral("paused") + orient);
				if (session) session->disconnectLink();
			}
		}
	}
	QSettings().remove(QStringLiteral("ui/dark"));
	language::apply(*qApp, QStringLiteral("en"));
}

/* ---- fixes from the review */

/* a fast line's float channel with +inf, -inf and NaN among its values (fast::Store passes them on): the frame is made
 * (the ticks' loop ended), the Y range stays finite, the value axis has its labels */
void checkChartInfinite() {
	Phone phone(LANDSCAPE);
	ChartItem *chart = showFedChart(phone);
	StreamDef def;
	def.name = QStringLiteral("F");
	def.addr = 0xDC00;
	def.size = 1024;
	def.rate = 100;
	def.channels = { StreamChannel{ QStringLiteral("X"), RegType::F32, QStringLiteral("V") } };
	auto store = std::make_shared<fast::Store>(def);
	const int count = 1000;
	QByteArray records(count * 4, '\0');
	for (int i = 0; i < count; ++i) {
		float v = float(std::sin(i / 50.0));
		if (i % 97 == 10) v = std::numeric_limits<float>::infinity();
		if (i % 97 == 40) v = -std::numeric_limits<float>::infinity();
		if (i % 97 == 70) v = std::numeric_limits<float>::quiet_NaN();
		std::memcpy(records.data() + i * 4, &v, 4);
	}
	store->append(0, count, records.constData(), true, 0);
	store->mark(count, 10.0, 0.01); /* records 0 .. 999 at 0.0 .. 9.99 s */
	qint64 polishes = 0;
	if (chart) {
		chart->setClock([] { return 10.0; });
		chart->setWindowSeconds(10);
		chart->addThemedLine(7, 0, QStringLiteral("F.X"), QStringLiteral("V"), store, 0);
		polishes = chart->polishes();
	}
	chartFrame(phone, chart, 3);
	const bool framed = chart && chart->polishes() > polishes;
	const bool finite = chart && std::isfinite(chart->yLow()) && std::isfinite(chart->yHigh()) && chart->yHigh() > chart->yLow();
	const int ticks = chart ? int(chart->valueTicks()->ticks().size()) : 0;
	check(framed && finite && ticks >= 2 && ticks <= 64,
			QStringLiteral("phone, chart: a fast line with +inf, -inf and NaN among its values: the frame is made, Y stays "
					"finite (%1 .. %2), %3 value labels (the window's thread hung in the ticks' loop before)")
					.arg(chart ? chart->yLow() : 0).arg(chart ? chart->yHigh() : 0).arg(ticks));
}

/* the registers' limit leaves room for the fast lines, as the desktop's (MainWindow::updatePlotLimit) */
void checkFastPlotLimit() {
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	showChart(phone);
	const bool loaded = session && session->loadMap(phone::mapsFolder() + QStringLiteral("/example_fast.json"));
	const int before = session ? session->registers()->plotLimit() : -1;
	if (session) {
		session->setFastPlot(0, 0, true);
		session->setFastPlot(0, 1, true);
	}
	const int withTwo = session ? session->registers()->plotLimit() : -1;
	if (session) session->setFastPlot(0, 1, false);
	const int withOne = session ? session->registers()->plotLimit() : -1;
	if (session) {
		session->setFastPlot(0, 0, false);
		session->loadMap(phone::mapsFolder() + QStringLiteral("/example_device.json"));
	}
	const int max = RegisterModel::MAX_PLOTTED;
	check(loaded && before == max && withTwo == max - 2 && withOne == max - 1,
			QStringLiteral("phone, fast lines: the registers' limit leaves room for them, as on the desktop (%1 with none, "
					"%2 with two fast lines, %3 with one; at most %4 lines together)").arg(before).arg(withTwo).arg(withOne)
					.arg(max));
}

/* the view's buttons are no toggles: a tap on Live while live, or on 1 min at 60 s, keeps it chosen */
void checkChartButtonsStay() {
	Phone phone(LANDSCAPE);
	ChartItem *chart = showChart(phone);
	QQuickItem *live = phone.item("liveButton");
	QQuickItem *minute = windowItem(phone, 60);
	const bool before = live && live->property("checked").toBool() && minute && minute->property("checked").toBool();
	phone.tap(minute);
	phone.tap(live);
	minute = windowItem(phone, 60);
	phone.tap(minute);
	minute = windowItem(phone, 60);
	const bool after = live && live->property("checked").toBool() && minute && minute->property("checked").toBool()
			&& chart && chart->live() && chart->windowSeconds() == 60;
	if (QObject *menu = phone.object("windowMenu")) QMetaObject::invokeMethod(menu, "close");
	phone.settle();
	phone.tap(phone.item("holdButton"));
	const bool holdShown = phone.item("holdButton")->property("checked").toBool() && !live->property("checked").toBool();
	phone.tap(phone.item("holdButton"));
	const bool holdStays = phone.item("holdButton")->property("checked").toBool() && chart && !chart->live();
	check(before && after && holdShown && holdStays,
			QStringLiteral("phone, chart: a tap on the chosen Live, 1 min or Hold keeps it chosen (they were toggles: the "
					"chosen one went unmarked)"));
}

/* ---- B.6: the plot's room in landscape, the legend's scroll, the menus inside the window */

/* `count` lines fed from the test (sines at 1 kHz over 0 .. 10 s, named as chartWithLines names them) in a 10 s view,
 * without the cursors */
ChartItem *chartWithFedLines(const Phone &phone, int count) {
	ChartItem *chart = showFedChart(phone);
	if (!chart) return nullptr;
	static const char *const names[] = { "SINE", "DOUBLE", "THIRD", "FOURTH", "FIFTH", "SIXTH" };
	static const char *const units[] = { "V", "A", "W", "°C", "Hz", "%" };
	static const QColor colours[] = { SINE_COLOUR, Qt::cyan, Qt::magenta, Qt::yellow, Qt::green, Qt::red };
	chart->setWindowSeconds(10);
	for (int i = 0; i < count && i < 6; ++i) {
		QVector<double> times, values;
		for (int k = 0; k <= 10000; ++k) {
			times << k / 1000.0;
			values << (i + 1) * (2.0 + 1.5 * std::sin(2 * M_PI * 0.5 * k / 1000.0));
		}
		chart->addLine(i + 1, colours[i], QString::fromLatin1(names[i]), QString::fromUtf8(units[i]));
		chart->appendSamples(i + 1, times, values);
	}
	chartFrame(phone, chart, 3);
	phone.settle(300);
	return chart;
}

/* an item's rect in the window's logical pixels */
QRectF sceneRect(const QQuickItem *item) {
	return item ? QRectF(item->mapToScene(QPointF(0, 0)), QSizeF(item->width(), item->height())) : QRectF();
}

/* the owner's phone across (2340 x 1080 px at 450 dpi): the window beside its navigation buttons is about 760 x 384 dp,
 * and the plot had about 40 % of it (the header, the tab bar, two rows of controls and the legend's row). Across the
 * Chart is the whole window now: at 760 x 384, 832 x 384, under the status bar (755 x 354) and at the test's landscape
 * size, in English and Arabic, with two lines: no header and no tab bar; the chart's area (the plot and its labels) at
 * least 75 % of the window's height; every button of the controls shows, inside the window, at least 48 x 48 dp, its
 * text whole, none over another or over the legend; the legend beside them with room for a chip, its first chip's
 * value showing inside it; no text cut or outside its parent; the gesture line hidden. Upright, the gesture line still
 * shows, with the header and the tab bar */
void checkChartLandscapeRoom() {
	QStringList seen, bad;
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		for (QSize size : { QSize(760, 384), QSize(832, 384), QSize(755, 354), LANDSCAPE }) {
			Phone phone(size);
			ChartItem *chart = chartWithFedLines(phone, 2);
			phone.settle(300);
			const QQuickItem *area = phone.item("chartArea");
			const double share = area && phone.window ? area->height() / phone.window->height() : 0;
			const QString where = QStringLiteral("%1 %2").arg(code, sizeText(size));
			if (!chart || share < 0.75)
				bad << QStringLiteral("%1: the plot %2 % of the height").arg(where).arg(share * 100, 0, 'f', 1);
			const QQuickItem *header = phone.item("header"), *tabs = phone.item("tabBar");
			if (!header || !tabs || header->isVisible() || tabs->isVisible())
				bad << QStringLiteral("%1: the header or the tab bar shows").arg(where);
			QList<QRectF> taken;
			QStringList widths;
			for (const char *name : { "barsButton", "liveButton", "holdButton", "windowButton", "yButton", "cursorsButton",
						 "chartClear" }) {
				const QQuickItem *button = phone.item(name);
				const QRectF r = sceneRect(button);
				const bool whole = button && button->property("implicitContentWidth").toDouble()
						+ button->property("leftPadding").toDouble() + button->property("rightPadding").toDouble()
						<= button->width() + 0.5;
				bool over = false;
				for (const QRectF &other : taken) over = over || other.intersected(r).width() > 0.5;
				taken << r;
				widths << QString::number(r.width(), 'f', 0);
				if (!button || !button->isVisible() || r.width() < 47.5 || r.height() < 47.5 || r.left() < -0.5
						|| r.right() > size.width() + 0.5 || r.top() < -0.5 || !whole || over)
					bad << QStringLiteral("%1: %2 %3 x %4 at %5 (text whole %6, over another %7)").arg(where, QLatin1String(name))
							.arg(r.width()).arg(r.height()).arg(r.left()).arg(whole).arg(over);
			}
			const QRectF legend = sceneRect(phone.item("legendView"));
			for (const QRectF &r : taken)
				if (r.intersected(legend).width() > 0.5) bad << QStringLiteral("%1: a button over the legend").arg(where);
			const QQuickItem *top = phone.item("chartTop");
			const bool beside = top && top->property("columns").toInt() == 2;
			const QQuickItem *firstValue = nullptr;
			forEachItem(topItem(phone.window), [&](QQuickItem *item) {
				if (!firstValue && item->objectName() == QLatin1String("chipValue") && item->isVisible()) firstValue = item;
			});
			const QRectF value = sceneRect(firstValue);
			if (!beside || legend.width() < 160 || !firstValue || !legend.adjusted(-0.5, -0.5, 0.5, 0.5).contains(value))
				bad << QStringLiteral("%1: the legend %2 wide (beside %3), its first value at %4..%5").arg(where)
						.arg(legend.width()).arg(beside).arg(value.left()).arg(value.right());
			int texts = 0;
			const QStringList outside = textsOutside(phone.window, texts);
			if (!outside.isEmpty()) bad << where + QStringLiteral(": ") + outside.join(QStringLiteral(", "));
			const QQuickItem *hint = phone.item("chartHint");
			if (hint && hint->isVisible()) bad << QStringLiteral("%1: the gesture line shows").arg(where);
			seen << QStringLiteral("%1: plot %2 of %3 (%4 %), buttons %5, legend %6 wide, rows above %7").arg(where)
							.arg(area ? area->height() : 0).arg(size.height()).arg(share * 100, 0, 'f', 0)
							.arg(widths.join(QLatin1Char('+'))).arg(legend.width(), 0, 'f', 0).arg(top ? top->height() : 0);
		}
		Phone upright(PORTRAIT);
		chartWithFedLines(upright, 2);
		const QQuickItem *hint = upright.item("chartHint");
		const QQuickItem *header = upright.item("header"), *tabs = upright.item("tabBar"), *bars = upright.item("barsButton");
		if (!hint || !hint->isVisible() || !header || !header->isVisible() || !tabs || !tabs->isVisible() || !bars
				|| bars->isVisible())
			bad << code + QStringLiteral(" portrait: the gesture line, the header or the tab bar hidden, or the bars button shown");
	}
	language::apply(*qApp, QStringLiteral("en"));
	check(bad.isEmpty(), QStringLiteral("phone, chart across: the whole window, no header nor tab bar, the plot at least 75 % "
			"of its height, every button of the controls shown whole and 48 dp to the touch, none over another or the "
			"legend, the legend beside them with its first value, no text cut, the gesture line upright only (%1) %2")
			.arg(seen.join(QStringLiteral("; ")), bad.join(QStringLiteral(" | "))));
}

/* the bars back, discoverably: across, the bars button at the controls' start (four corners in, a tooltip, the link's
 * colour as a dot: grey with no link); a tap shows the rail (the plot narrower), its icon four corners
 * out; a tap again hides them. Android's back in the whole window brings them back too (taken: the app stays); another
 * tab chosen and the Chart again: the whole window again */
void checkChartFullScreen() {
	QStringList bad;
	Phone phone(QSize(760, 384));
	chartWithFedLines(phone, 2);
	const QQuickItem *header = phone.item("header"), *tabs = phone.item("tabBar"), *area = phone.item("chartArea");
	const QQuickItem *rail = phone.item("navRail");
	QQuickItem *bars = phone.item("barsButton");
	QObject *icon = phone.object("barsIcon");
	const QQuickItem *dot = phone.item("barsLinkDot");
	const auto full = [&] { return header && tabs && rail && !header->isVisible() && !tabs->isVisible() && !rail->isVisible(); };
	const double fullWidth = area ? area->width() : 0;
	const QString tip = bars ? bars->property("tip").toString() : QString();
	if (!full() || !bars || !bars->isVisible() || !icon || icon->property("kind").toString() != QLatin1String("shrink") || !dot
			|| !dot->isVisible() || !near(dot->property("color").value<QColor>(), themeColor(&ThemeColors::muted), 2))
		bad << QStringLiteral("at the start: not the whole window, or the bars button, its icon or its dot wrong");
	phone.tap(bars);
	const bool shown = rail && rail->isVisible() && area && area->width() < fullWidth - 40
			&& icon && icon->property("kind").toString() == QLatin1String("expand") && dot && !dot->isVisible();
	if (!shown) bad << QStringLiteral("a tap did not bring the rail back (%1 wide, was %2)").arg(area ? area->width() : 0).arg(fullWidth);
	phone.tap(bars);
	if (!full()) bad << QStringLiteral("a second tap did not hide them");
	const bool taken = pressBack(phone);
	if (!taken || full()) bad << QStringLiteral("a back did not bring them back (taken %1)").arg(taken);
	QQuickItem *registers = phone.item("tabRegisters");
	const QRectF at = sceneRect(registers);
	phone.tap(registers);
	const int afterRegisters = phone.object("tabBar")->property("currentIndex").toInt();
	phone.tap(phone.item("tabChart"));
	if (afterRegisters != 0)
		bad << QStringLiteral("Registers' tab at %1,%2 %3x%4 visible %5 did not show the Registers").arg(at.left()).arg(at.top())
				.arg(at.width()).arg(at.height()).arg(registers && registers->isVisible());
	if (!full())
		bad << QStringLiteral("the Chart chosen again is not the whole window (tab %1, bars %2, rail %3)")
				.arg(phone.object("tabBar")->property("currentIndex").toInt()).arg(phone.window->property("chartBars").toBool())
				.arg(rail && rail->isVisible());
	check(bad.isEmpty() && tip == QLatin1String("Show the tabs and the link"),
			QStringLiteral("phone, chart across: the bars button (\"%1\") brings the rail back and hides "
					"them again, a back brings them back, the Chart chosen again is the whole window %2")
					.arg(tip, bad.join(QStringLiteral(" | "))));
}

/* the owner's phone: with many lines the last chip was cut at the screen's edge ("SETPOIN") and nothing said the row
 * scrolls. Six lines, across and upright, English and Arabic: the row's content is wider than its view, a fade at its
 * end (none at its start); a finger scrolls it to the end: the last chip whole inside the view, the end's fade gone,
 * the start's shown, no line hidden by the drags */
void checkChartLegendScroll() {
	QStringList seen;
	bool ok = true;
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		for (QSize size : { LANDSCAPE, PORTRAIT }) {
			Phone phone(size);
			ChartItem *chart = chartWithFedLines(phone, 6);
			QQuickItem *legend = phone.item("legendView");
			QQuickItem *endFade = phone.item("legendFadeEnd"), *startFade = phone.item("legendFadeStart");
			const double contentWidth = legend ? legend->property("contentWidth").toDouble() : 0;
			const double width = legend ? legend->width() : 0;
			const QRectF view = sceneRect(legend);
			const bool endShown = endFade && endFade->isVisible() && sceneRect(endFade).right() >= view.right() - 0.5;
			const bool startHidden = startFade && !startFade->isVisible();
			for (int i = 0; i < 10 && legend && !legend->property("atXEnd").toBool(); ++i)
				dragItem(phone, legend, -width * 0.6);
			phone.settle(300);
			const QRectF last = sceneRect(chip(phone, QStringLiteral("SIXTH")));
			const bool lastWhole = !last.isEmpty() && last.left() >= view.left() - 0.5 && last.right() <= view.right() + 0.5;
			const bool endGone = endFade && !endFade->isVisible();
			const bool startShown = startFade && startFade->isVisible() && sceneRect(startFade).left() <= view.left() + 0.5;
			int hidden = 0;
			forEachItem(legend, [&](QQuickItem *item) {
				if (item->objectName() == QLatin1String("legendChip")) hidden += !item->property("shown").toBool();
			});
			const bool good = chart && contentWidth > width + 0.5 && endShown && startHidden && lastWhole && endGone
					&& startShown && hidden == 0;
			seen << QStringLiteral("%1 %2: content %3 in %4, fade at the end %5 (start %6); scrolled: the last chip %7 .. %8 "
								   "in %9 .. %10, end's fade gone %11, start's shown %12, hidden %13")
							.arg(code, size == LANDSCAPE ? QStringLiteral("landscape") : QStringLiteral("portrait"))
							.arg(contentWidth, 0, 'f', 0).arg(width, 0, 'f', 0).arg(endShown).arg(!startHidden)
							.arg(last.left(), 0, 'f', 0).arg(last.right(), 0, 'f', 0).arg(view.left(), 0, 'f', 0)
							.arg(view.right(), 0, 'f', 0).arg(endGone).arg(startShown).arg(hidden);
			ok = ok && good;
		}
	}
	language::apply(*qApp, QStringLiteral("en"));
	check(ok, QStringLiteral("phone, legend: six lines' chips scroll sideways and show it: a fade at the end with more, at "
			"the start once scrolled, the last chip reached whole (%1)").arg(seen.join(QStringLiteral("; "))));
}

/* the owner's phone: a card's "..." menu touched the screen's right edge, and its dark face had no edge on the dark
 * page. English and Arabic, dark and light: a card's menu (upright) and a legend chip's (the second of two, across)
 * lie inside the window less 8 dp, under their button; a card's grows toward the middle (its end at the button's end
 * in English, its start at the button's start in Arabic); the face has an edge in the control colour, 3:1 or more to
 * the page, seen in the picture */
void checkMenuInside() {
	QStringList seen, bad;
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		const bool rtl = code == QLatin1String("ar");
		for (bool dark : { true, false }) {
			QSettings().setValue(QStringLiteral("ui/dark"), dark);
			const ThemeColors colors = Theme::colorsFor(dark);
			const QString where = QStringLiteral("%1 %2").arg(code, dark ? "dark" : "light");
			auto inspect = [&](const Phone &phone, const char *menuName, const QQuickItem *button, const QString &what,
								  bool alignEnds) {
				QObject *menu = phone.object(menuName);
				const bool opened = menu && QTest::qWaitFor([&] { return menu->property("opened").toBool(); }, 3000);
				phone.settle(200);
				const QQuickItem *face = menu ? menu->property("background").value<QQuickItem *>() : nullptr;
				const QObject *border = face ? face->property("border").value<QObject *>() : nullptr;
				const QRectF r = sceneRect(face), b = sceneRect(button);
				const QRectF room(8, 8, phone.window->width() - 16, phone.window->height() - 16);
				const bool inside = !r.isEmpty() && room.adjusted(-0.5, -0.5, 0.5, 0.5).contains(r);
				const bool under = r.top() >= b.bottom() - 0.5 || r.bottom() <= b.top() + 0.5;
				const bool side = !alignEnds
						|| (rtl ? std::fabs(r.left() - b.left()) <= 1 : std::fabs(r.right() - b.right()) <= 1);
				const QColor edge = border ? border->property("color").value<QColor>() : QColor();
				const double edgeWidth = border ? border->property("width").toDouble() : 0;
				const QColor drawn = Phone::pixel(phone.grab(), QPointF(r.left() + 0.5, r.center().y()));
				const bool edged = edgeWidth >= 1 && edge == colors.control && contrast(edge, colors.bg) >= 3.0
						&& near(drawn, colors.control, 12);
				seen << QStringLiteral("%1 %2: %3,%4 %5 x %6 (button %7 .. %8), edge %9 drawn %10")
								.arg(where, what).arg(r.left()).arg(r.top()).arg(r.width()).arg(r.height()).arg(b.left())
								.arg(b.right()).arg(edge.name(), drawn.name());
				if (!opened || !inside || !under || !side || !edged)
					bad << QStringLiteral("%1 %2: opened %3, inside %4, under %5, toward the middle %6, edge %7")
								   .arg(where, what).arg(opened).arg(inside).arg(under).arg(side).arg(edged);
				if (menu) QMetaObject::invokeMethod(menu, "close");
				(void) QTest::qWaitFor([&] { return menu && !menu->property("visible").toBool(); }, 3000);
			};
			{
				Phone phone(LANDSCAPE);
				chartWithFedLines(phone, 2);
				/* the last chip's, at the legend's end (beside the controls it may start out of view) */
				if (QObject *legend = phone.object("legendView")) QMetaObject::invokeMethod(legend, "positionViewAtEnd");
				phone.settle(200);
				QQuickItem *more = within(chip(phone, QStringLiteral("DOUBLE")), QStringLiteral("chipMore"));
				phone.tap(more);
				inspect(phone, "chipMenu", more, QStringLiteral("chip"), false);
			}
		}
	}
	QSettings().remove(QStringLiteral("ui/dark"));
	language::apply(*qApp, QStringLiteral("en"));
	check(bad.isEmpty(), QStringLiteral("phone, menus: a chip's \"...\" menu inside the window less 8 dp, under the button, "
			"an edge in the control colour drawn on the page, dark and light (%1) %2").arg(seen.join(QStringLiteral("; ")), bad.join(QStringLiteral(" | "))));
}

/* ---- the design pass (part A): the Registers screen as the desktop's table, a register's sheet, the pill */

/* the cards fully in view at 412 x 915, connected, before the rows (phone_shots_b6: DEVICE_ID to MSG_BUFFER) */
constexpr int CARDS_IN_VIEW_BEFORE = 5;

/* rows as the desktop's table: about twice as many fully in view as the cards were, each at least 48 high, the second
 * line only with something to say, the values lined up at their right edge in tabular digits, a thin line under each */
void checkRowDensity() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	phone.settle(600);
	QQuickItem *list = phone.item("registerList");
	const QRectF view = sceneRect(list);
	int whole = 0, separated = 0;
	double lowest = 1e9;
	forEachItem(list, [&](QQuickItem *item) {
		if (!item->objectName().startsWith(QLatin1String("row_")) || !item->isVisible()) return;
		const QRectF r = sceneRect(item);
		if (r.top() < view.top() - 0.5 || r.bottom() > view.bottom() + 0.5) return;
		whole++;
		lowest = std::min(lowest, r.height());
		const QQuickItem *line = within(item, QStringLiteral("rowSeparator"));
		separated += line && line->isVisible() && line->height() <= 1.01;
	});
	auto second = [&](const char *reg) {
		const QQuickItem *line = within(regRow(phone, QLatin1String(reg)), QStringLiteral("rowSecond"));
		return line && line->isVisible();
	};
	const bool secondLines = !second("DEVICE_ID") && !second("UPTIME") && second("STATUS") && second("CONFIG");
	QStringList edges;
	QVector<double> rights;
	bool tabular = true;
	for (const char *reg : { "DEVICE_ID", "STATUS", "CONFIG", "MSG_CNT", "UPTIME" }) {
		const QQuickItem *value = within(regRow(phone, QLatin1String(reg)), QStringLiteral("regValue"));
		if (!value) continue;
		const double right = value->mapToScene(QPointF(value->width(), 0)).x();
		rights << right;
		edges << QStringLiteral("%1 %2").arg(QLatin1String(reg)).arg(right);
		tabular = tabular && value->property("font").value<QFont>().featureValue(QFont::Tag("tnum")) == 1
				&& value->property("effectiveHorizontalAlignment").toInt() == Qt::AlignRight;
	}
	const bool lined = rights.size() == 5 && *std::max_element(rights.begin(), rights.end())
			- *std::min_element(rights.begin(), rights.end()) <= 0.5;
	check(whole >= CARDS_IN_VIEW_BEFORE * 9 / 5 && lowest >= 48 && separated == whole && secondLines && lined && tabular,
			QStringLiteral("phone, Registers rows: at 412 x 915 connected %1 rows fully in view (the cards: %2), each at least "
					"48 high (lowest %3) with a thin line under it (%4); a second line only with decoded text (STATUS, CONFIG; "
					"not DEVICE_ID, UPTIME); the values right-aligned in tabular digits, their right edges lined up (%5)")
					.arg(whole).arg(CARDS_IN_VIEW_BEFORE).arg(lowest).arg(separated).arg(edges.join(QStringLiteral(", "))));
	if (session) session->disconnectLink();
}

/* a tap on a row opens its sheet: the value large with its unit, the address and group, the decoded text, its trend,
 * Plot and Copy; the write part only for a writable register, the number pad for a number typed with digits */
void checkRowSheet() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	if (session) session->setWritesEnabled(true);
	RegisterList *list = session ? session->registerList() : nullptr;
	const int supply = modelRow(session, QStringLiteral("SUPPLY_V"));
	/* a few values read: the trend has a line to draw */
	(void) QTest::qWaitFor([&] { return list && list->trendSize(supply) >= 5; }, 4000);
	openSheet(phone, QStringLiteral("SUPPLY_V"));
	const bool opened = sheetOpen(phone);
	const QString name = phone.text("sheetName"), addr = phone.text("sheetAddr"), unit = phone.text("sheetValueUnit");
	bool number = false;
	phone.text("sheetValue").toDouble(&number);
	const QQuickItem *trend = phone.item("sheetTrend");
	bool lowOk = false, highOk = false;
	const double low = phone.text("sheetTrendMin").toDouble(&lowOk), high = phone.text("sheetTrendMax").toDouble(&highOk);
	const bool trendShown = trend && trend->isVisible() && lowOk && highOk && low <= high
			&& phone.text("sheetTrendSpan").startsWith(QLatin1String("last "));
	const QString lowText = phone.text("sheetTrendMin"), highText = phone.text("sheetTrendMax"),
			span = phone.text("sheetTrendSpan");
	const bool readOnly = phone.item("sheetWriteRow") && !phone.item("sheetWriteRow")->isVisible()
			&& phone.item("sheetPlot")->isVisible() && phone.item("sheetCopy")->isVisible();
	closeSheet(phone);

	openSheet(phone, QStringLiteral("FAN_SPEED"));
	const QQuickItem *field = phone.item("sheetField");
	const int fanHints = field ? field->property("inputMethodHints").toInt() : 0;
	const bool fanPad = field && field->isVisible() && (fanHints & Qt::ImhFormattedNumbersOnly)
			&& phone.item("sheetApply")->isVisible() && phone.text("sheetApply") == QLatin1String("Write");
	closeSheet(phone);
	openSheet(phone, QStringLiteral("CONFIG"));
	const int configHints = field ? field->property("inputMethodHints").toInt() : 0;
	const QString decoded = phone.text("sheetDecoded");
	const bool config = field && field->isVisible() && !(configHints & Qt::ImhFormattedNumbersOnly)
			&& phone.item("sheetDecoded")->isVisible() && decoded == rowText(phone, QStringLiteral("CONFIG"), "regDecoded");
	closeSheet(phone);
	check(opened && name == QLatin1String("SUPPLY_V") && addr == QStringLiteral("0xD004  ·  Power & supply") && number
					&& unit == QLatin1String("V") && trendShown && readOnly && fanPad && config,
			QStringLiteral("phone, sheet: a tap on SUPPLY_V's row opens its sheet (\"%1\", \"%2\", its value with \"%3\"), its "
					"trend from %4 to %5 (%6), Plot and Copy, no write part (read-only); FAN_SPEED's field brings up the "
					"number pad with Write beside it; CONFIG (hex) the letters' keyboard, its fields \"%7\"")
					.arg(name, addr, unit, lowText, highText, span, decoded)
					+ QStringLiteral(" [%1 %2 %3 %4 %5]").arg(trendShown).arg(readOnly).arg(fanPad).arg(config).arg(number));
	if (session) session->disconnectLink();
}

/* the desktop's quick write: the field's Enter writes, the answer, then what the device holds read back */
void checkWriteByEnter() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	if (session) session->setWritesEnabled(true);
	openSheet(phone, QStringLiteral("FAN_SPEED"));
	const QString before = modelValue(session, QStringLiteral("FAN_SPEED"));
	const QString wanted = before == QLatin1String("44") ? QStringLiteral("45") : QStringLiteral("44");
	phone.type("sheetField", wanted);
	focusField(phone, "sheetField");
	QTest::keyClick(phone.window, Qt::Key_Return);
	const bool written = QTest::qWaitFor([&] {
		return modelValue(session, QStringLiteral("FAN_SPEED")) == wanted && phone.text("sheetAnswer") == QLatin1String("FAN_SPEED written")
				&& phone.item("sheetReadBack")->isVisible() && phone.text("sheetReadBack") == QStringLiteral("Read back: %1 %").arg(wanted);
	}, 3000);
	const QString readBack = phone.text("sheetReadBack");
	check(written && readBack == QStringLiteral("Read back: %1 %").arg(wanted)
					&& session->log()->contains(QStringLiteral("written: FAN_SPEED = %1").arg(wanted)),
			QStringLiteral("phone, quick write: %1 typed in FAN_SPEED's field and Enter writes it as Write does: \"%2\", \"%3\"")
					.arg(wanted, phone.text("sheetAnswer"), readBack));
	if (session) session->disconnectLink();
}

/* the trend's ring: at most TREND_POINTS values a register, only a plottable one, started again when the clock goes
 * back (another link) */
void checkTrendBounded() {
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	RegisterList *list = session ? session->registerList() : nullptr;
	const int supply = modelRow(session, QStringLiteral("SUPPLY_V")), bytes = modelRow(session, QStringLiteral("MSG_BUFFER"));
	if (!list || supply < 0 || bytes < 0) {
		check(false, "phone, trend: the session's registers");
		return;
	}
	for (int i = 0; i < 300; ++i) list->sample(supply, 1000 + i * 10, i);
	const int kept = list->trendSize(supply);
	const QVariantMap trend = list->trend(supply);
	const int points = int(trend.value(QStringLiteral("points")).toList().size());
	const double low = trend.value(QStringLiteral("min")).toDouble(), high = trend.value(QStringLiteral("max")).toDouble();
	const double seconds = trend.value(QStringLiteral("seconds")).toDouble();
	list->sample(supply, 500, 7);
	const int after = list->trendSize(supply);
	list->sample(bytes, 1000, 1);
	const int none = list->trendSize(bytes);
	check(kept == RegisterList::TREND_POINTS && points == kept && low == 300 - kept && high == 299
					&& std::fabs(seconds - (kept - 1) * 0.01) < 1e-9 && after == 1 && none == 0 && list->trend(bytes).isEmpty(),
			QStringLiteral("phone, trend: 300 values of SUPPLY_V keep the last %1 (%2 points, %3 to %4 over %5 s); a value "
					"from an earlier time starts it again (%6); a bytes register keeps none (%7)")
					.arg(kept).arg(points).arg(low).arg(high).arg(seconds).arg(after).arg(none));
}

/* the pill: a chevron says it opens something, a tap opens the Connect drawer; while connected and polling it has the
 * poll rate, as the desktop's status bar */
void checkPill() {
	Phone phone(PORTRAIT);
	const QString off = phone.text("linkPillText");
	phone.tap(phone.item("linkPill"));
	const bool drawer = QTest::qWaitFor([&] { return phone.drawerOpen(); }, 3000);
	phone.closeDrawer();
	PhoneSession *session = connectFake(phone);
	static const QRegularExpression rate(QStringLiteral("^Connected · (\\d+(\\.\\d)?) Hz$"));
	QRegularExpressionMatch match;
	const bool rated = QTest::qWaitFor([&] {
		match = rate.match(phone.text("linkPillText"));
		return match.hasMatch() && session && std::fabs(match.captured(1).toDouble() - session->pollHz()) <= 0.51;
	}, 4000);
	const QString on = phone.text("linkPillText");
	if (session) session->setPolling(false);
	const bool plain = QTest::qWaitFor([&] { return phone.text("linkPillText") == QLatin1String("Connected"); }, 2000);
	const QQuickItem *chevron = phone.item("linkPillChevron");
	check(off == QLatin1String("Not connected") && drawer && rated && plain && chevron && chevron->isVisible(),
			QStringLiteral("phone, pill: \"%1\" with a chevron, a tap opens the Connect drawer; connected and polling \"%2\" "
					"(the measured %3 polls a second), polling off \"Connected\"")
					.arg(off, on).arg(session ? session->pollHz() : -1));
	if (session) {
		session->setPolling(true);
		session->disconnectLink();
	}
}

/* the header slimmer in portrait (48, was 56): the room goes to the screens; the title smaller, the targets 48 */
void checkHeaderSlim() {
	QStringList seen;
	bool ok = true;
	for (QSize size : { PORTRAIT, LANDSCAPE }) {
		Phone phone(size);
		const QQuickItem *header = phone.item("header"), *title = phone.item("screenTitle"), *menu = phone.item("menuButton");
		const QQuickItem *screens = phone.item("screens");
		const int pixels = title ? title->property("font").value<QFont>().pixelSize() : 0;
		const bool fine = size == PORTRAIT
				? header && header->isVisible() && header->height() == 48 && pixels == 18 && menu && menu->height() >= 47.5
						&& screens && std::fabs(sceneRect(screens).top() - 48) <= 0.5
				: header && !header->isVisible() && menu && menu->height() >= 47.5 && screens
						&& std::fabs(sceneRect(screens).top()) <= 0.5;
		seen << QStringLiteral("%1: header %2, title %3 px, the screens from %4").arg(sizeText(size)).arg(header ? header->height() : -1)
						.arg(pixels).arg(screens ? sceneRect(screens).top() : -1);
		ok = ok && fine;
	}
	check(ok, QStringLiteral("phone, header: 48 high upright, the title at 18 px, the menu button 48 dp, the screens right "
			"under it; across no header, the screens from the window's top (%1)").arg(seen.join(QStringLiteral("; "))));
}

/* an empty Log: its icon, what comes here, and a button to the Connect drawer */
void checkLogEmpty() {
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	if (session) session->log()->clear();
	phone.tap(phone.item("tabLog"));
	const QQuickItem *icon = phone.item("logEmptyIcon"), *button = phone.item("logEmptyButton");
	const bool shown = icon && icon->isVisible() && icon->property("kind").toString() == QLatin1String("log") && button
			&& button->isVisible() && button->height() >= 47.5;
	const QString title = phone.text("logEmptyTitle"), action = phone.text("logEmptyButton");
	phone.tap(phone.item("logEmptyButton"));
	const bool drawer = QTest::qWaitFor([&] { return phone.drawerOpen(); }, 3000);
	check(shown && title == QLatin1String("Nothing logged yet") && action == QLatin1String("Open the Connect drawer") && drawer,
			QStringLiteral("phone, empty Log: its icon, \"%1\", and \"%2\" opens the Connect drawer").arg(title, action));
}

/* a search that finds nothing: the screen says so with one button, which clears the search */
void checkSearchEmpty() {
	Phone phone(PORTRAIT);
	RegisterList *list = phone.session() ? phone.session()->registerList() : nullptr;
	search(phone, QStringLiteral("zzz"));
	const QString title = phone.text("registersEmptyTitle"), action = phone.text("registersEmptyButton");
	const QQuickItem *button = phone.item("registersEmptyButton");
	const bool shown = button && button->isVisible() && phone.item("registersEmptyIcon")->isVisible();
	phone.tap(phone.item("registersEmptyButton"));
	const bool all = list && list->count() == list->total() && phone.text("registerSearch").isEmpty()
			&& !phone.item("registersEmptyState")->isVisible();
	check(shown && title == QLatin1String("No match") && action == QLatin1String("Clear the search") && all,
			QStringLiteral("phone, search empty: \"%1\" with \"%2\", which shows every register again").arg(title, action));
}

/* the owner's phone: a group's header kept at the view's top lay over the row under it, its value cut. Scrolled down
 * the whole list a step at a time, no group header covers any part of a row in view */
void checkHeadersOverRows() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	phone.settle(600);
	QQuickItem *list = phone.item("registerList");
	RegisterList *rows = session ? session->registerList() : nullptr;
	QStringList covered;
	int steps = 0, pairs = 0;
	if (list && rows) {
		const QRectF view = sceneRect(list);
		const double most = std::max(0.0, list->property("contentHeight").toDouble() - list->height());
		const double first = list->property("originY").toDouble();
		for (double y = first; y <= first + most + 0.5; y += 23) {
			list->setProperty("contentY", y);
			phone.settle(120);
			steps++;
			QList<QRectF> headers;
			forEachItem(list, [&](QQuickItem *item) {
				if (item->objectName() != QLatin1String("regGroup") || !item->isVisible() || !item->parentItem()) return;
				const QRectF r = sceneRect(item->parentItem()).intersected(view);
				if (!r.isEmpty()) headers << r;
			});
			for (int i = 0; i < rows->count(); ++i) {
				QQuickItem *row = nullptr;
				QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, i));
				if (!row || !row->isVisible()) continue;
				const QRectF r = sceneRect(row).intersected(view);
				if (r.isEmpty()) continue;
				for (const QRectF &header : headers) {
					pairs++;
					const double overlap = std::min(r.bottom(), header.bottom()) - std::max(r.top(), header.top());
					if (overlap > 0.5)
						covered << QStringLiteral("at %1: %2 under a header by %3").arg(y).arg(row->objectName()).arg(overlap);
				}
			}
		}
	}
	covered.removeDuplicates();
	check(steps > 5 && pairs > 20 && covered.isEmpty(),
			QStringLiteral("phone, Registers headers: scrolled down the list in %1 steps, no group header lies over a row in view "
					"(%2 pairs looked at) %3").arg(steps).arg(pairs).arg(covered.mid(0, 6).join(QStringLiteral(" | "))));
	if (session) session->disconnectLink();
}

/* EVRE_PHONE_SHOT: both languages and themes. Portrait: the start (not connected) and its drawer, connected (the
 * drawer, the Log, the Registers screen, a search, the write sheet after a write, asking first, an enum's list, the
 * sheet and the drawer over a keyboard stood for, drawn grey); landscape: the Registers screen, the drawer, the sheet,
 * the Log and the Chart */
/* ---- part B of the design pass: the lost link on the chart */

/* A stand-in between the phone and the fake device that loses the link at drop() and, on every later link, hands the
 * device's answers over in bunches (all that came in the last BUNCH_MS at once), as a phone's TCP through a port forward
 * did after a reconnect; the requests go on at once. On a thread of its own. */
class BunchingProxy : public QObject {
public:
	static constexpr int BUNCH_MS = 450;
	std::atomic<int> connections{ 0 };
	std::atomic<quint16> port{ 0 };

	void start() {
		auto *server = new QTcpServer(this);
		QObject::connect(server, &QTcpServer::newConnection, this, [this, server] {
			while (QTcpSocket *client = server->nextPendingConnection()) {
				const bool bunched = ++connections > 1;
				clients_ << client;
				client->setSocketOption(QAbstractSocket::LowDelayOption, 1);
				auto *device = new QTcpSocket(client);
				device->connectToHost(QStringLiteral("127.0.0.1"), FAKE_DEVICE_PORT);
				device->setSocketOption(QAbstractSocket::LowDelayOption, 1);
				QObject::connect(client, &QTcpSocket::readyRead, device, [client, device] { device->write(client->readAll()); });
				if (bunched) {
					auto held = std::make_shared<QByteArray>();
					QObject::connect(device, &QTcpSocket::readyRead, client, [device, held] { *held += device->readAll(); });
					auto *flush = new QTimer(client);
					QObject::connect(flush, &QTimer::timeout, client, [client, held] {
						if (held->isEmpty()) return;
						client->write(*held);
						held->clear();
					});
					flush->start(BUNCH_MS);
				} else {
					QObject::connect(device, &QTcpSocket::readyRead, client, [client, device] { client->write(device->readAll()); });
				}
				QObject::connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
			}
		});
		if (server->listen(QHostAddress::LocalHost)) port = server->serverPort();
	}
	void drop() {
		for (const QPointer<QTcpSocket> &client : std::as_const(clients_))
			if (client) client->disconnectFromHost();
		clients_.clear();
	}

private:
	QList<QPointer<QTcpSocket>> clients_;
};

/* the example map with one register left (UPTIME, the fake device's milliseconds: a straight slope), so a poll is one
 * block and four polls can be under way on TCP (a bunched link then answers several at once); written to `dir` */
QString uptimeMap(const QTemporaryDir &dir) {
	QFile in(phone::mapsFolder() + QStringLiteral("/example_device.json"));
	QJsonObject map = in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object() : QJsonObject();
	QJsonArray kept;
	for (const QJsonValue &reg : map.value(QStringLiteral("registers")).toArray())
		if (reg.toObject().value(QStringLiteral("name")).toString() == QLatin1String("UPTIME")) kept << reg;
	map.insert(QStringLiteral("registers"), kept);
	const QString file = dir.filePath(QStringLiteral("uptime.json"));
	QFile out(file);
	if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(map).toJson());
	return file;
}

/* The owner's phone: the link dropped (the port forward went) and came back. The polled line bridged the drop with a
 * flat step and a jump, and the slope after the reconnect went up in stairs (the answers came in bunches, each bunch
 * one time). UPTIME plotted through a proxy that drops the link, then bunches the answers: the line has a break at the
 * first sample after the drop (a bin marked as a gap) and none of its colour lies in the picture between the samples
 * around the drop; after the reconnect each sample's value lies on the slope its times give (within 45 ms of it: the
 * fake device's values step every 20 ms), not on stairs */
void checkChartLinkGap() {
	OnThread<BunchingProxy> proxy;
	QTemporaryDir dir;
	const QString file = uptimeMap(dir);
	Phone phone(LANDSCAPE);
	PhoneSession *session = phone.session();
	const bool loaded = session && session->loadMap(file);
	if (session) {
		session->setAutoReconnect(true);
		session->setPolling(true);
		session->setIntervalMs(100);
		session->connectTcp(QStringLiteral("127.0.0.1"), proxy.stand->port, QString());
	}
	const int row = modelRow(session, QStringLiteral("UPTIME"));
	const bool up = waitForState(session, PhoneSession::Connected)
			&& QTest::qWaitFor([&] { return row >= 0 && session->registers()->rows()[row].valid; }, 4000);
	ChartItem *chart = showChart(phone);
	const bool plotted = up && session->setPlot(row, true);
	const int id = row >= 0 ? int(regKey(session->registers()->rows()[row].def)) : -1;
	if (chart) chart->setWindowSeconds(10);
	QTest::qWait(2000);
	proxy.run([](BunchingProxy *stand) { stand->drop(); });
	(void) QTest::qWaitFor([&] { return session->state() != PhoneSession::Connected; }, 3000);
	const bool back = QTest::qWaitFor([&] { return session->state() == PhoneSession::Connected; }, 5000);
	QTest::qWait(4000);
	if (chart) chart->setLive(false);
	chartFrame(phone, chart, 3);

	/* the drop: the widest step between two samples */
	const ChartData::Series *s = chart ? chart->series(id) : nullptr;
	qsizetype after = -1;
	double widest = 0;
	for (qsizetype i = 1; s && i < s->times.size(); ++i)
		if (s->times[i] - s->times[i - 1] > widest) {
			widest = s->times[i] - s->times[i - 1];
			after = i;
		}
	bool marked = false;
	for (const ChartData::Bin &bin : chart ? chart->bins(id) : QVector<ChartData::Bin>())
		marked = marked || (bin.gap && after >= 0 && bin.firstSample == s->dropped + after);
	int across = -1, drawn = 0;
	QString gapText;
	if (s && after > 0 && chart) {
		const QImage image = phone.grab();
		const qreal dpr = image.devicePixelRatio();
		const QRectF plot = plotInPicture(chart, image);
		const auto xOf = [&](double t) {
			return (chart->mapToScene(QPointF(chart->plotRect().left(), 0)).x()
						   + (t - chart->viewStart()) / (chart->viewEnd() - chart->viewStart()) * chart->plotRect().width()) * dpr;
		};
		const double x0 = xOf(s->times[after - 1]) + 4 * dpr, x1 = xOf(s->times[after]) - 4 * dpr;
		if (x1 > x0) across = colourIn(image, QRectF(x0, plot.top(), x1 - x0, plot.height()), chart->lineColour(id)).pixels;
		drawn = colourIn(image, plot, chart->lineColour(id)).pixels;
		gapText = QStringLiteral("%1 s, %2 px").arg(widest, 0, 'f', 2).arg((x1 - x0) / dpr, 0, 'f', 0);
	}
	check(loaded && plotted && back && widest > 0.4 && marked && across == 0 && drawn > 50,
			QStringLiteral("phone, chart: a lost link breaks the polled line: the first sample after it begins a new piece "
					"(its bin a gap: %1), none of the line's colour between the samples around the drop (%2 pixels; the "
					"drop %3; %4 pixels of it in the plot)").arg(marked).arg(across).arg(gapText).arg(drawn));

	/* after the reconnect: the samples' values against a straight line through them (least squares) */
	double worst = -1;
	int count = 0, risers = 0;
	if (s && after > 0) {
		double st = 0, sv = 0, stt = 0, stv = 0;
		const qsizetype first = after + 2; /* the link's first polls apart */
		const double t0 = first < s->times.size() ? s->times[first] : 0; /* times near 0: the sums keep their digits */
		for (qsizetype i = first; i < s->times.size(); ++i, ++count) {
			const double t = s->times[i] - t0;
			st += t;
			sv += s->values[i];
			stt += t * t;
			stv += t * s->values[i];
		}
		const double slope = count > 2 ? (count * stv - st * sv) / (count * stt - st * st) : 0;
		const double offset = count > 0 ? (sv - slope * st) / count : 0;
		for (qsizetype i = first; slope > 0 && i < s->times.size(); ++i) {
			worst = std::max(worst, std::fabs(s->values[i] - (offset + slope * (s->times[i] - t0))) / slope);
			/* a stair's riser: two samples a moment apart, their values far apart on the slope */
			if (i > first && s->times[i] - s->times[i - 1] < 0.005 && (s->values[i] - s->values[i - 1]) / slope > 0.03)
				++risers;
		}
	}
	check(count >= 15 && worst >= 0 && worst <= 0.045 && risers == 0,
			QStringLiteral("phone, chart: after a reconnect through a link that hands the answers over in bunches (every "
					"%1 ms), the polled samples keep their times: each within %2 ms of the slope (at most 45), no stair's "
					"riser (%3: two samples within 5 ms whose values lie over 30 ms apart; %4 samples)")
					.arg(BunchingProxy::BUNCH_MS).arg(worst * 1000, 0, 'f', 1).arg(risers).arg(count));
	if (session) {
		session->disconnectLink();
		session->loadMap(phone::mapsFolder() + QStringLiteral("/example_device.json"));
	}
}

/* ---- part B of the design pass: the Log's levels, the Monitor's kinds and sheet, motion and the buzz */

/* the visible items named `name` under `root`, in their order */
QList<QQuickItem *> visibleNamed(QQuickItem *root, const QString &name) {
	QList<QQuickItem *> found;
	forEachItem(root, [&](QQuickItem *item) {
		if (item->objectName() == name && item->isVisible()) found << item;
	});
	return found;
}

/* The Log as the desktop's: a line of each level, each with its level's icon (info, warn, error: three shapes) in its
 * colour (the accent, amber, red) and its text in the desktop's colour (the text colour, amber, red); the times in the
 * fixed-pitch family; a filter chip per level with its count. Info's chip off: its lines go (the list the warnings and
 * errors), the chip struck through, the choice kept (phone/logLevels) and back in a new window; Copy then puts the
 * lines shown with their levels (WARN, ERROR); every level off: "No line of the levels chosen" and Show all levels */
void checkLogLevels() {
	QSettings().remove(QStringLiteral("phone/logLevels"));
	QStringList bad;
	QString copied;
	{
		Phone phone(PORTRAIT);
		PhoneSession *session = phone.session();
		LogModel *log = session ? session->log() : nullptr;
		if (log) {
			log->clear();
			log->add(LogModel::Info, QStringLiteral("connected: 127.0.0.1:1210"));
			log->add(LogModel::Warning, QStringLiteral("the map is for device 0x1002, this is 0x1001"));
			log->add(LogModel::Error, QStringLiteral("connection lost: closed by 127.0.0.1:1210"));
			log->add(LogModel::Info, QStringLiteral("disconnected"));
		}
		phone.tap(phone.item("tabLog"));
		QQuickItem *list = phone.item("logList");
		/* in the list's order (top to bottom), not the delegates' */
		const auto byTop = [](QList<QQuickItem *> items) {
			std::sort(items.begin(), items.end(), [](QQuickItem *a, QQuickItem *b) { return sceneRect(a).top() < sceneRect(b).top(); });
			return items;
		};
		const QList<QQuickItem *> icons = byTop(visibleNamed(list, QStringLiteral("logIcon")));
		const QList<QQuickItem *> texts = byTop(visibleNamed(list, QStringLiteral("logText")));
		const QList<QQuickItem *> times = byTop(visibleNamed(list, QStringLiteral("logTime")));
		const char *kinds[] = { "info", "warn", "error", "info" };
		const QColor tones[] = { themeColor(&ThemeColors::accent), themeColor(&ThemeColors::warn), themeColor(&ThemeColors::bad),
			themeColor(&ThemeColors::accent) };
		const QColor textTones[] = { themeColor(&ThemeColors::text), themeColor(&ThemeColors::warn), themeColor(&ThemeColors::bad),
			themeColor(&ThemeColors::text) };
		if (icons.size() != 4 || texts.size() != 4 || times.size() != 4) bad << QStringLiteral("%1 icons, %2 texts, %3 times")
					.arg(icons.size()).arg(texts.size()).arg(times.size());
		for (int i = 0; i < 4 && i < icons.size() && i < texts.size(); ++i)
			if (icons[i]->property("kind").toString() != QLatin1String(kinds[i])
					|| !near(icons[i]->property("color").value<QColor>(), tones[i], 2)
					|| !near(texts[i]->property("color").value<QColor>(), textTones[i], 2))
				bad << QStringLiteral("line %1: icon %2 in %3, text in %4").arg(i).arg(icons[i]->property("kind").toString())
						.arg(icons[i]->property("color").value<QColor>().name(), texts[i]->property("color").value<QColor>().name());
		/* the time beside the text, not over it, and in the fixed-pitch family */
		if (!times.isEmpty() && !texts.isEmpty()) {
			const QFontInfo info(times.first()->property("font").value<QFont>());
			if (!info.fixedPitch()) bad << QStringLiteral("the time in %1, not fixed pitch").arg(info.family());
			if (sceneRect(times.first()).right() > sceneRect(texts.first()).left() + 0.5
					|| std::fabs(sceneRect(times.first()).center().y() - sceneRect(texts.first()).top()
							- sceneRect(texts.first()).height() / 2) > sceneRect(texts.first()).height())
				bad << QStringLiteral("the time not beside the text");
		}
		QQuickItem *info = phone.item("logLevel0"), *warn = phone.item("logLevel1"), *error = phone.item("logLevel2");
		const auto countOf = [](QQuickItem *chip) { return chip ? chip->property("count").toInt() : -1; };
		if (!info || !warn || !error || countOf(info) != 2 || countOf(warn) != 1 || countOf(error) != 1
				|| !info->property("checked").toBool() || sceneRect(info).height() < 47.5)
			bad << QStringLiteral("the chips: %1 %2 %3").arg(countOf(info)).arg(countOf(warn)).arg(countOf(error));
		phone.tap(info);
		const int shown = list ? list->property("count").toInt() : -1;
		const QQuickItem *name = within(info, QStringLiteral("filterChipName"));
		if (shown != 2 || info->property("checked").toBool() || !name || !name->property("font").value<QFont>().strikeOut())
			bad << QStringLiteral("Info off: %1 lines shown").arg(shown);
		if (QGuiApplication::clipboard()) QGuiApplication::clipboard()->clear();
		phone.tap(phone.item("logCopy"));
		copied = QGuiApplication::clipboard() ? QGuiApplication::clipboard()->text() : QString();
		if (copied.count(QLatin1Char('\n')) != 1 || !copied.contains(QLatin1String("  WARN   the map is for"))
				|| !copied.contains(QLatin1String("  ERROR  connection lost")))
			bad << QStringLiteral("Copy: \"%1\"").arg(copied);
		if (QSettings().value(QStringLiteral("phone/logLevels")).toString() != QLatin1String("1,2"))
			bad << QStringLiteral("not saved");
		phone.tap(warn);
		phone.tap(error);
		const QQuickItem *filtered = phone.item("logFilteredState");
		if (!filtered || !filtered->isVisible() || list->property("count").toInt() != 0)
			bad << QStringLiteral("every level off: no word of it");
		phone.tap(phone.item("logFilteredButton"));
		if (list->property("count").toInt() != 4) bad << QStringLiteral("Show all levels: %1 lines").arg(list->property("count").toInt());
		phone.tap(info);
	}
	{
		/* the choice back in a new window */
		Phone phone(PORTRAIT);
		phone.tap(phone.item("tabLog"));
		const QQuickItem *info = phone.item("logLevel0"), *warn = phone.item("logLevel1");
		if (!info || info->property("checked").toBool() || !warn || !warn->property("checked").toBool())
			bad << QStringLiteral("the choice not back in a new window");
	}
	QSettings().remove(QStringLiteral("phone/logLevels"));
	check(bad.isEmpty(), QStringLiteral("phone, Log: each line its level's icon and colour, the time in the fixed-pitch "
			"family beside the text; a chip per level with its count, Info off hides its lines and is kept, Copy the lines "
			"shown with their levels, every level off says so and Show all levels %1").arg(bad.join(QStringLiteral(" | "))));
}

/* The Monitor's frames labelled by their function, coloured by its kind (READ asked in the accent, READ_RESP in
 * green) on its soft tone; TX and RX filter chips with their counts: TX off leaves the received frames only, RX off too
 * says so; kept in phone/monitorSides */
void checkMonitorKinds() {
	QSettings().remove(QStringLiteral("phone/monitorSides"));
	QStringList bad;
	Phone phone(LANDSCAPE);
	PhoneSession *session = connectFake(phone);
	phone.tap(phone.item("tabMonitor"));
	FrameLog *frames = session ? session->frames() : nullptr;
	(void) QTest::qWaitFor([&] { return frames && frames->sentCount() >= 6 && frames->receivedCount() >= 6; }, 4000);
	phone.tap(phone.item("monitorPause"));
	phone.settle(300);
	QQuickItem *list = phone.item("monitorList");
	const QList<QQuickItem *> types = visibleNamed(list, QStringLiteral("frameType"));
	QSet<QString> named;
	for (QQuickItem *type : types) {
		const QString text = type->property("text").toString();
		named << text;
		const QColor colour = type->property("color").value<QColor>();
		const QColor want = text == QLatin1String("READ") ? themeColor(&ThemeColors::accent)
				: text == QLatin1String("READ_RESP") ? themeColor(&ThemeColors::good) : QColor();
		if (!near(colour, want, 2)) bad << QStringLiteral("%1 in %2").arg(text, colour.name());
	}
	if (!named.contains(QStringLiteral("READ")) || !named.contains(QStringLiteral("READ_RESP")))
		bad << QStringLiteral("labels: %1").arg(QStringList(named.begin(), named.end()).join(QLatin1Char(',')));
	QQuickItem *tx = phone.item("monitorTx"), *rx = phone.item("monitorRx");
	const int sent = frames ? frames->sentCount() : 0, received = frames ? frames->receivedCount() : 0;
	if (!tx || !rx || tx->property("count").toInt() != sent || rx->property("count").toInt() != received || sent == 0)
		bad << QStringLiteral("the chips' counts (%1, %2)").arg(sent).arg(received);
	phone.tap(tx);
	phone.settle(200);
	bool onlyReceived = list && list->property("count").toInt() == received;
	for (QQuickItem *direction : visibleNamed(list, QStringLiteral("frameDirection")))
		onlyReceived = onlyReceived && direction->property("text").toString() == QLatin1String("RX");
	if (!onlyReceived) bad << QStringLiteral("TX off: %1 rows (want %2)").arg(list ? list->property("count").toInt() : -1).arg(received);
	if (QSettings().value(QStringLiteral("phone/monitorSides")).toString() != QLatin1String("1,2")) bad << QStringLiteral("TX off not saved");
	phone.tap(rx);
	const QString empty = phone.text("monitorEmpty");
	if (empty != QLatin1String("TX and RX both off: turn one on to see its frames."))
		bad << QStringLiteral("both off: \"%1\"").arg(empty);
	phone.tap(tx);
	phone.tap(rx);
	if (session) session->disconnectLink();
	QSettings().remove(QStringLiteral("phone/monitorSides"));
	check(bad.isEmpty(), QStringLiteral("phone, Monitor: each frame's function a label in its kind's colour (%1), TX and "
			"RX chips with their counts filter the frames and are kept, both off it says so %2")
			.arg(QStringList(named.begin(), named.end()).join(QStringLiteral(", ")), bad.join(QStringLiteral(" | "))));
}

/* A frame tapped: its sheet slides up with the frame field by field as the desktop's Dump spells it: the eight fields
 * in the wire's order, FUNCTION READ_RESP, OFFSET naming the map's register, CRC OK, the Dump's words under each; back
 * closes it. FrameLog.decode against built frames: an ERROR_RESP's code named, a request's CRC computed, a frame with a
 * wrong CRC not spelled out */
void checkFrameSheet() {
	QStringList bad;
	Phone phone(LANDSCAPE);
	PhoneSession *session = connectFake(phone);
	phone.tap(phone.item("tabMonitor"));
	FrameLog *frames = session ? session->frames() : nullptr;
	(void) QTest::qWaitFor([&] { return frames && frames->receivedCount() >= 4; }, 4000);
	phone.tap(phone.item("monitorPause"));
	phone.settle(300);
	QQuickItem *answer = nullptr;
	for (QQuickItem *row : visibleNamed(phone.item("monitorList"), QStringLiteral("frameRow"))) {
		const QQuickItem *type = within(row, QStringLiteral("frameType"));
		if (type && type->property("text").toString() == QLatin1String("READ_RESP") && sceneRect(row).bottom() < phone.window->height())
			answer = row;
	}
	phone.tap(answer);
	(void) QTest::qWaitFor([&] { return phone.object("frameSheet") && phone.object("frameSheet")->property("opened").toBool(); }, 3000);
	QQuickItem *sheet = phone.item("frameSheetFlick");
	QStringList names, meanings;
	int words = 0;
	for (QQuickItem *field : visibleNamed(sheet, QStringLiteral("frameField"))) {
		const QQuickItem *name = within(field, QStringLiteral("frameFieldName"));
		const QQuickItem *meaning = within(field, QStringLiteral("frameFieldMeaning"));
		const QQuickItem *said = within(field, QStringLiteral("frameFieldWords"));
		names << (name ? name->property("text").toString() : QString());
		meanings << (meaning ? meaning->property("text").toString() : QString());
		words += said && !said->property("text").toString().isEmpty();
	}
	const QString want = QStringLiteral("START,SLAVE,FUNCTION,OFFSET,COUNT,DATA,CRC,END");
	if (names.join(QLatin1Char(',')) != want || words != 8 || meanings.value(2) != QLatin1String("READ_RESP")
			|| !meanings.value(3).contains(QStringLiteral(" · ")) || meanings.value(6) != QLatin1String("OK")
			|| phone.text("frameSheetType") != QLatin1String("READ_RESP"))
		bad << QStringLiteral("fields %1, meanings %2, %3 words").arg(names.join(QLatin1Char(',')), meanings.join(QLatin1Char('|'))).arg(words);
	const bool closed = pressBack(phone) && QTest::qWaitFor([&] { return !phone.object("frameSheet")->property("visible").toBool(); }, 3000);
	if (!closed) bad << QStringLiteral("back did not close it");
	/* the decoder alone */
	const auto rowsOf = [&](const QByteArray &raw, const char *direction) {
		return frames ? frames->decode(evre::hex(raw), QLatin1String(direction)) : QVariantList();
	};
	const QVariantList error = rowsOf(evre::build(1, evre::ERROR_RESP, 0xD0F0, 1, QByteArray(1, char(4))), "RX");
	const QString errorText = error.size() == 8 ? error[5].toMap().value(QStringLiteral("meaning")).toString() : QString();
	if (errorText != QLatin1String("ERROR 4 offset out of range")) bad << QStringLiteral("ERROR_RESP: \"%1\"").arg(errorText);
	const QVariantList request = rowsOf(evre::build(1, evre::READ, 0xD000, 4), "TX");
	const QString crcWords = request.size() == 8 ? request[6].toMap().value(QStringLiteral("words")).toString() : QString();
	const QString offset = request.size() == 8 ? request[3].toMap().value(QStringLiteral("meaning")).toString() : QString();
	if (!crcWords.endsWith(QLatin1String("the Studio computed it")) || offset != QStringLiteral("0xD000 · UPTIME"))
		bad << QStringLiteral("READ: \"%1\", \"%2\"").arg(offset, crcWords);
	QByteArray broken = evre::build(1, evre::READ, 0xD000, 4);
	broken[broken.size() - 2] = char(broken.at(broken.size() - 2) ^ 0x55);
	if (!rowsOf(broken, "TX").isEmpty()) bad << QStringLiteral("a wrong CRC spelled out");
	if (session) session->disconnectLink();
	check(bad.isEmpty(), QStringLiteral("phone, Monitor: a tap on a frame opens its sheet, the frame field by field as the "
			"Dump spells it (%1), back closes it; an error answer's code, a request's CRC and its register named, a wrong "
			"CRC not spelled out %2").arg(names.join(QLatin1Char(' ')), bad.join(QStringLiteral(" | "))));
}

/* Motion: a tab's screen comes in with a short fade and slide (under way a moment after the tap, done after); the
 * register sheet slides up. Android's animations removed (the animator scale 0): the session's motion off, the screen
 * there at once, the sheet open at once */
void checkMotion() {
	QStringList seen;
	bool ok = true;
	FakeSystem system;
	for (double scale : { 1.0, 0.0 }) {
		system.scale = scale;
		PhoneSession::setSystemBridgeForTests(&system);
		{
			Phone phone(PORTRAIT);
			PhoneSession *session = phone.session();
			QObject *tabs = phone.object("tabBar");
			QObject *motion = phone.object("tabMotion");
			const QQuickItem *log = phone.item("screenLog");
			if (tabs) tabs->setProperty("currentIndex", 3);
			QTest::qWait(40);
			const bool running = motion && motion->property("running").toBool();
			const double opacity = log ? log->opacity() : -1;
			phone.settle(500);
			const bool done = log && log->opacity() == 1 && motion && !motion->property("running").toBool()
					&& phone.object("screens")->property("shift").toDouble() == 0;
			if (tabs) tabs->setProperty("currentIndex", 0);
			phone.settle(500);
			/* the register sheet: under way, or open at once */
			QObject *sheet = phone.object("registerSheet");
			if (sheet) QMetaObject::invokeMethod(sheet, "openFor", Q_ARG(QVariant, 0));
			QTest::qWait(40);
			const double position = sheet ? sheet->property("position").toDouble() : -1;
			phone.settle(600);
			if (sheet) QMetaObject::invokeMethod(sheet, "close");
			phone.settle(600);
			const bool on = scale > 0;
			const bool right = session && session->motion() == on && done
					&& (on ? running && opacity < 0.99 && position < 0.99 : !running && opacity == 1 && position > 0.99);
			ok = ok && right;
			seen << QStringLiteral("scale %1: motion %2, the tab's motion %3 (opacity %4 at 40 ms), the sheet at %5 at 40 ms")
							.arg(scale).arg(session && session->motion()).arg(running).arg(opacity, 0, 'f', 2).arg(position, 0, 'f', 2);
		}
		PhoneSession::setSystemBridgeForTests(nullptr);
	}
	check(ok, QStringLiteral("phone, motion: a tab's screen fades and slides in, the sheet slides up; with Android's "
			"animations removed both are there at once (%1)").arg(seen.join(QStringLiteral("; "))));
}

/* the buzz (Android's haptic feedback): once when the link comes up, once when the device takes a write; none for a
 * write the app did not send (writes off) */
void checkHaptics() {
	FakeSystem system;
	PhoneSession::setSystemBridgeForTests(&system);
	int afterConnect = -1, afterWrite = -1, afterRefused = -1;
	{
		Phone phone(PORTRAIT);
		PhoneSession *session = connectFake(phone);
		afterConnect = system.buzzes;
		const int row = modelRow(session, QStringLiteral("FAN_SPEED"));
		bool answered = false;
		QMetaObject::Connection watch;
		if (session) {
			watch = QObject::connect(session, &PhoneSession::writeAnswered, session, [&](int, bool good, const QString &) {
				answered = answered || good;
			});
			session->setWritesEnabled(true);
			session->write(row, QStringLiteral("37"));
		}
		(void) QTest::qWaitFor([&] { return answered; }, 3000);
		afterWrite = system.buzzes;
		if (session) {
			session->setWritesEnabled(false);
			session->write(row, QStringLiteral("38"));
		}
		phone.settle(300);
		afterRefused = system.buzzes;
		QObject::disconnect(watch);
		if (session) session->disconnectLink();
	}
	PhoneSession::setSystemBridgeForTests(nullptr);
	check(afterConnect == 1 && afterWrite == 2 && afterRefused == 2,
			QStringLiteral("phone, haptics: a short buzz when the link comes up (%1) and when the device takes a write (%2), "
					"none for a write not sent (%3)").arg(afterConnect).arg(afterWrite).arg(afterRefused));
}

/* part B's pictures for the eye (not checks): <prefix>_<en|ar>_<dark|light>_<name>.png. At the owner's phone across
 * (760 x 384): registers760 (the rail, one-line rows), sheet760 (FAN_SPEED's sheet in two columns), chart760_two (two
 * registers, live, the whole window), chart760_bars (the bars back), chart760_window (the
 * window's menu), chart760_ysheet, chart760_six (six lines), chart760_cursors, chart760_gap (a lost link: the break, then
 * the slope after the reconnect), monitor760, monitor760_rx (TX off), frame760 (a READ_RESP's sheet); upright:
 * chart_portrait, log_levels, log_filtered (Info off), frame_portrait (a READ's sheet). EVRE_PHONE_ONLY=partb saves
 * only these */
void savePartBPictures(const QString &prefix) {
	const QSize phoneAcross(760, 384);
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		for (bool dark : { true, false }) {
			QSettings().setValue(QStringLiteral("ui/dark"), dark);
			const QString name = QStringLiteral("%1_%2_%3_").arg(prefix, code, dark ? "dark" : "light");
			{
				Phone phone(phoneAcross);
				PhoneSession *session = connectFake(phone);
				if (session) session->setWritesEnabled(true);
				QTest::qWait(1500);
				phone.grab().save(name + QStringLiteral("registers760.png"));
				openSheet(phone, QStringLiteral("FAN_SPEED"));
				phone.settle(500);
				phone.grab().save(name + QStringLiteral("sheet760.png"));
				closeSheet(phone);
				ChartItem *chart = showChart(phone);
				for (const char *reg : { "SUPPLY_V", "TEMPERATURE" }) {
					const int row = modelRow(session, QLatin1String(reg));
					if (session && row >= 0) session->setPlot(row, true);
				}
				if (chart) chart->setWindowSeconds(10);
				QTest::qWait(6000);
				phone.grab().save(name + QStringLiteral("chart760_two.png"));
				phone.tap(phone.item("barsButton"));
				phone.settle(400);
				phone.grab().save(name + QStringLiteral("chart760_bars.png"));
				phone.tap(phone.item("barsButton"));
				phone.tap(phone.item("windowButton"));
				phone.settle(400);
				phone.grab().save(name + QStringLiteral("chart760_window.png"));
				if (QObject *menu = phone.object("windowMenu")) QMetaObject::invokeMethod(menu, "close");
				phone.settle(400);
				phone.tap(phone.item("yButton"));
				phone.settle(500);
				phone.grab().save(name + QStringLiteral("chart760_ysheet.png"));
				if (QObject *sheet = phone.object("ySheet")) QMetaObject::invokeMethod(sheet, "close");
				phone.settle(400);
				for (const char *reg : { "SUPPLY_I", "PRESSURE", "SETPOINT", "MOTOR_SPEED" }) {
					const int more = modelRow(session, QLatin1String(reg));
					if (session && more >= 0) session->setPlot(more, true);
				}
				QTest::qWait(4000);
				phone.grab().save(name + QStringLiteral("chart760_six.png"));
				phone.tap(phone.item("cursorsButton"));
				phone.settle(800);
				phone.grab().save(name + QStringLiteral("chart760_cursors.png"));
				phone.tap(phone.item("cursorsButton"));
				phone.tap(phone.item("tabMonitor"));
				QTest::qWait(1500);
				phone.tap(phone.item("monitorPause"));
				phone.settle(400);
				phone.grab().save(name + QStringLiteral("monitor760.png"));
				phone.tap(phone.item("monitorTx"));
				phone.settle(400);
				phone.grab().save(name + QStringLiteral("monitor760_rx.png"));
				QQuickItem *answer = nullptr;
				for (QQuickItem *row : visibleNamed(phone.item("monitorList"), QStringLiteral("frameRow")))
					if (!answer && sceneRect(row).top() > 60) answer = row;
				phone.tap(answer);
				phone.settle(700);
				phone.grab().save(name + QStringLiteral("frame760.png"));
				pressBack(phone);
				phone.tap(phone.item("monitorTx"));
				if (session) session->disconnectLink();
			}
			{
				/* the lost link and the reconnect through the bunching proxy */
				OnThread<BunchingProxy> proxy;
				QTemporaryDir dir;
				Phone phone(phoneAcross);
				PhoneSession *session = phone.session();
				if (session) {
					session->loadMap(uptimeMap(dir));
					session->setAutoReconnect(true);
					session->setPolling(true);
					session->setIntervalMs(100);
					session->connectTcp(QStringLiteral("127.0.0.1"), proxy.stand->port, QString());
				}
				const int row = modelRow(session, QStringLiteral("UPTIME"));
				(void) QTest::qWaitFor([&] { return session && row >= 0 && session->registers()->rows()[row].valid; }, 4000);
				ChartItem *chart = showChart(phone);
				if (session && row >= 0) session->setPlot(row, true);
				if (chart) chart->setWindowSeconds(10);
				QTest::qWait(3000);
				proxy.run([](BunchingProxy *stand) { stand->drop(); });
				(void) QTest::qWaitFor([&] { return session->state() != PhoneSession::Connected; }, 3000);
				(void) QTest::qWaitFor([&] { return session->state() == PhoneSession::Connected; }, 5000);
				QTest::qWait(4000);
				phone.grab().save(name + QStringLiteral("chart760_gap.png"));
				if (session) {
					session->disconnectLink();
					session->loadMap(phone::mapsFolder() + QStringLiteral("/example_device.json"));
				}
			}
			{
				Phone phone(PORTRAIT);
				PhoneSession *session = connectFake(phone);
				ChartItem *chart = showChart(phone);
				for (const char *reg : { "SUPPLY_V", "TEMPERATURE" }) {
					const int row = modelRow(session, QLatin1String(reg));
					if (session && row >= 0) session->setPlot(row, true);
				}
				if (chart) chart->setWindowSeconds(10);
				QTest::qWait(5000);
				phone.grab().save(name + QStringLiteral("chart_portrait.png"));
				phone.tap(phone.item("tabMonitor"));
				QTest::qWait(1200);
				phone.tap(phone.item("monitorPause"));
				phone.settle(400);
				QQuickItem *request = nullptr;
				for (QQuickItem *row : visibleNamed(phone.item("monitorList"), QStringLiteral("frameRow"))) {
					const QQuickItem *type = within(row, QStringLiteral("frameType"));
					if (!request && type && type->property("text").toString() == QLatin1String("READ")) request = row;
				}
				phone.tap(request);
				phone.settle(700);
				phone.grab().save(name + QStringLiteral("frame_portrait.png"));
				pressBack(phone);
				if (LogModel *log = session ? session->log() : nullptr) {
					log->add(LogModel::Warning, QStringLiteral("the map is for device 0x1002, this is 0x1001"));
					log->add(LogModel::Error, QStringLiteral("write refused: NOWHERE = 1 (01 at 0xD0F0): offset out of range"));
					log->add(LogModel::Info, QStringLiteral("written: FAN_SPEED = 42"));
				}
				phone.tap(phone.item("tabLog"));
				phone.settle(500);
				phone.grab().save(name + QStringLiteral("log_levels.png"));
				phone.tap(phone.item("logLevel0"));
				phone.settle(400);
				phone.grab().save(name + QStringLiteral("log_filtered.png"));
				phone.tap(phone.item("logLevel0"));
				if (session) session->disconnectLink();
			}
		}
	}
	QSettings().remove(QStringLiteral("ui/dark"));
	QSettings().remove(QStringLiteral("phone/logLevels"));
	QSettings().remove(QStringLiteral("phone/monitorSides"));
	language::apply(*qApp, QStringLiteral("en"));
}

/* ---- the coordinator's review of part A's pictures: landscape's height, the sheet across, numbers with units */

/* Across, every tab: the bottom tab bar and the header give way to a rail at the start edge (the right in Arabic): the
 * menu button, the four tabs (each 48 dp or more, the chosen one marked), the link's state; the screens beside it from
 * the window's top. At 760 x 384, connected, the Registers screen shows 7 rows or more whole (4 before), each one line
 * and at least 42 high; a tap on the rail's Log shows the Log, its menu button opens the drawer */
void checkLandscapeRail() {
	QStringList seen, bad;
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		Phone phone(QSize(760, 384));
		PhoneSession *session = connectFake(phone);
		phone.settle(600);
		const bool rtl = code == QLatin1String("ar");
		const QQuickItem *rail = phone.item("navRail"), *header = phone.item("header"), *tabs = phone.item("tabBar");
		const QQuickItem *screens = phone.item("screens");
		const QRectF railAt = sceneRect(rail), screensAt = sceneRect(screens);
		if (!rail || !rail->isVisible() || (header && header->isVisible()) || (tabs && tabs->isVisible()))
			bad << code + QStringLiteral(": the rail hidden, or the header or the tab bar shown");
		if (rtl ? std::fabs(railAt.right() - 760) > 0.5 : std::fabs(railAt.left()) > 0.5)
			bad << code + QStringLiteral(": the rail not at the start edge (%1..%2)").arg(railAt.left()).arg(railAt.right());
		if (screensAt.intersected(railAt).width() > 0.5 || std::fabs(screensAt.top()) > 0.5 || screensAt.height() < 383.5)
			bad << code + QStringLiteral(": the screens %1,%2 %3x%4").arg(screensAt.left()).arg(screensAt.top())
					.arg(screensAt.width()).arg(screensAt.height());
		for (const char *name : { "menuButton", "tabRegisters", "tabChart", "tabMonitor", "tabLog", "linkPill" }) {
			const QQuickItem *item = phone.item(name);
			const QRectF r = sceneRect(item);
			if (!item || !item->isVisible() || r.width() < 47.5 || r.height() < 47.5 || !railAt.adjusted(-0.5, -0.5, 0.5, 0.5).contains(r))
				bad << QStringLiteral("%1: %2 %3x%4 at %5,%6").arg(code, QLatin1String(name)).arg(r.width()).arg(r.height())
						.arg(r.left()).arg(r.top());
		}
		/* the Registers' rows whole in view */
		QQuickItem *list = phone.item("registerList");
		const QRectF view = sceneRect(list);
		int whole = 0;
		double lowest = 1e9;
		forEachItem(list, [&](QQuickItem *item) {
			if (!item->objectName().startsWith(QLatin1String("row_")) || !item->isVisible()) return;
			const QRectF r = sceneRect(item);
			if (r.top() < view.top() - 0.5 || r.bottom() > view.bottom() + 0.5) return;
			++whole;
			lowest = std::min(lowest, r.height());
			const QQuickItem *second = within(item, QStringLiteral("rowSecond"));
			if (second && second->isVisible()) bad << code + QStringLiteral(": a second line across");
		});
		if (whole < 7 || lowest < 41.5) bad << QStringLiteral("%1: %2 rows whole, the lowest %3").arg(code).arg(whole).arg(lowest);
		/* STATUS's decoded text on its one line */
		const QQuickItem *inline_ = within(regRow(phone, QStringLiteral("STATUS")), QStringLiteral("regDecodedInline"));
		if (!inline_ || !inline_->isVisible() || inline_->property("text").toString().isEmpty())
			bad << code + QStringLiteral(": STATUS's decoded text not on its line");
		const QString linkText = phone.text("railLinkText");
		phone.tap(phone.item("tabLog"));
		QObject *bar = phone.object("tabBar");
		if (!bar || bar->property("currentIndex").toInt() != 3) bad << code + QStringLiteral(": the rail's Log did not show the Log");
		phone.openDrawer();
		if (!phone.drawerOpen()) bad << code + QStringLiteral(": the rail's menu did not open the drawer");
		phone.closeDrawer();
		seen << QStringLiteral("%1: the rail %2..%3, %4 rows whole (lowest %5), the link \"%6\"").arg(code).arg(railAt.left())
						.arg(railAt.right()).arg(whole).arg(lowest).arg(linkText);
		if (session) session->disconnectLink();
	}
	language::apply(*qApp, QStringLiteral("en"));
	check(bad.isEmpty(), QStringLiteral("phone, across: a rail at the start edge (the menu, the four tabs, the link's state, "
			"each 48 dp or more), no header nor tab bar, the screens beside it; at 760 x 384 the Registers show 7 rows or "
			"more, each one line (%1) %2").arg(seen.join(QStringLiteral("; ")), bad.join(QStringLiteral(" | "))));
}

/* The register sheet across (the owner's picture: Write and its field cut at the foot): at 760 x 384, English and
 * Arabic, FAN_SPEED's sheet in two columns (the register, its value and trend; Plot, Copy and the write), all of it
 * inside the window with nothing to scroll: the name, the value, the trend, Plot, Copy, the limits, the field and Write
 * whole in view, no text cut */
void checkSheetAcross() {
	QStringList seen, bad;
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		Phone phone(QSize(760, 384));
		PhoneSession *session = connectFake(phone);
		if (session) session->setWritesEnabled(true);
		QTest::qWait(1500); /* a few values for the trend */
		openSheet(phone, QStringLiteral("FAN_SPEED"));
		phone.settle(400);
		const QQuickItem *flick = phone.item("sheetFlick");
		const QRectF viewport = sceneRect(flick).intersected(QRectF(0, 0, 760, 384));
		const double contentHeight = flick ? flick->property("contentHeight").toDouble() : 1e9;
		if (!flick || contentHeight > flick->height() + 0.5)
			bad << QStringLiteral("%1: %2 of content in %3").arg(code).arg(contentHeight).arg(flick ? flick->height() : 0);
		const QRectF about = sceneRect(phone.item("sheetAbout")), actions = sceneRect(phone.item("sheetActions"));
		if (about.intersected(actions).width() > 0.5 || std::fabs(about.top() - actions.top()) > 0.5)
			bad << code + QStringLiteral(": not two columns side by side");
		for (const char *name : { "sheetName", "sheetValue", "sheetTrend", "sheetPlot", "sheetCopy", "sheetLimits",
					 "sheetField", "sheetApply", "sheetClose" }) {
			const QQuickItem *item = phone.item(name);
			const QRectF r = sceneRect(item);
			if (!item || !item->isVisible() || !viewport.adjusted(-0.5, -0.5, 0.5, 0.5).contains(r))
				bad << QStringLiteral("%1: %2 at %3,%4 %5x%6 (the view %7..%8)").arg(code, QLatin1String(name)).arg(r.left())
						.arg(r.top()).arg(r.width()).arg(r.height()).arg(viewport.top()).arg(viewport.bottom());
		}
		int texts = 0;
		const QStringList outside = textsOutside(phone.window, texts);
		if (!outside.isEmpty()) bad << code + QStringLiteral(": ") + outside.join(QStringLiteral(", "));
		seen << QStringLiteral("%1: content %2 in %3").arg(code).arg(contentHeight).arg(flick ? flick->height() : 0);
		closeSheet(phone);
		if (session) session->disconnectLink();
	}
	language::apply(*qApp, QStringLiteral("en"));
	check(bad.isEmpty(), QStringLiteral("phone, the register sheet across: at 760 x 384 in two columns, every part in view "
			"with nothing to scroll, no text cut (%1) %2").arg(seen.join(QStringLiteral("; ")), bad.join(QStringLiteral(" | "))));
}

/* Arabic: a number and its unit one left-to-right run (between U+2066 and U+2069), not "% 100": the sheet's limits
 * ("100 %"), its trend's span ("3 s"), the pill's rate upright ("10 Hz") and the rail's across */
void checkArabicRuns() {
	language::apply(*qApp, QStringLiteral("ar"));
	const QChar lri(0x2066), pdi(0x2069);
	QStringList bad, seen;
	{
		Phone phone(PORTRAIT);
		PhoneSession *session = connectFake(phone);
		if (session) session->setWritesEnabled(true);
		QTest::qWait(2500);
		const QString pill = phone.text("linkPillText");
		static const QRegularExpression rate(QStringLiteral("\\x{2066}[0-9.]+ Hz\\x{2069}"));
		if (!rate.match(pill).hasMatch()) bad << QStringLiteral("the pill \"%1\"").arg(pill);
		openSheet(phone, QStringLiteral("FAN_SPEED"));
		phone.settle(400);
		const QString limits = phone.text("sheetLimits"), span = phone.text("sheetTrendSpan");
		if (!limits.contains(lri + QStringLiteral("100 %") + pdi)) bad << QStringLiteral("the limits \"%1\"").arg(limits);
		static const QRegularExpression seconds(QStringLiteral("\\x{2066}[0-9]+ s\\x{2069}"));
		if (!seconds.match(span).hasMatch()) bad << QStringLiteral("the trend's span \"%1\"").arg(span);
		seen << pill << limits << span;
		closeSheet(phone);
		if (session) session->disconnectLink();
	}
	{
		Phone phone(QSize(760, 384));
		PhoneSession *session = connectFake(phone);
		QTest::qWait(2500);
		const QString rail = phone.text("railLinkText");
		static const QRegularExpression rate(QStringLiteral("^\\x{2066}[0-9.]+ Hz\\x{2069}$"));
		if (!rate.match(rail).hasMatch()) bad << QStringLiteral("the rail \"%1\"").arg(rail);
		seen << rail;
		if (session) session->disconnectLink();
	}
	language::apply(*qApp, QStringLiteral("en"));
	QString shown = seen.join(QStringLiteral(" | "));
	shown.replace(lri, QStringLiteral("[")).replace(pdi, QStringLiteral("]"));
	check(bad.isEmpty(), QStringLiteral("phone, Arabic: a number and its unit one left-to-right run (isolated): the sheet's "
			"limits keep \"100 %\", the trend's span, the pill's and the rail's rate (%1) %2").arg(shown, bad.join(QStringLiteral(" | "))));
}

void savePictures(const QString &prefix) {
	/* the window as drawn, the keyboard stood for drawn over its part */
	auto save = [](const Phone &phone, const QString &file, double keyboard = 0) {
		QImage image = phone.grab();
		if (keyboard > 0) {
			/* the painter takes the picture's dpr itself: logical pixels */
			QPainter painter(&image);
			painter.fillRect(QRectF(0, phone.window->height() - keyboard, phone.window->width(), keyboard), QColor(0x80, 0x84, 0x8C));
		}
		image.save(file);
	};
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		for (bool dark : { true, false }) {
			QSettings().setValue(QStringLiteral("ui/dark"), dark);
			const QString name = QStringLiteral("%1_%2_%3_").arg(prefix, code, dark ? "dark" : "light");
			{
				Phone phone(PORTRAIT);
				save(phone, name + QStringLiteral("portrait.png"));
				phone.openDrawer();
				save(phone, name + QStringLiteral("drawer.png"));
				phone.closeDrawer();
				/* the Help: its topics, then the phone's own page, at its top, a screen further and at its end */
				openHelp(phone);
				save(phone, name + QStringLiteral("help.png"));
				if (QObject *help = phone.object("helpScreen")) QMetaObject::invokeMethod(help, "show", Q_ARG(QVariant, 0));
				phone.settle(200);
				save(phone, name + QStringLiteral("helppage.png"));
				if (QQuickItem *page = phone.item("helpPage")) {
					const double end = std::max(0.0, page->property("contentHeight").toDouble() - page->height());
					page->setProperty("contentY", std::min(end, page->height() - 40));
					phone.settle(150);
					save(phone, name + QStringLiteral("helppage2.png"));
					page->setProperty("contentY", end);
					phone.settle(150);
					save(phone, name + QStringLiteral("helppage_end.png"));
				}
			}
			{
				/* connected to the fake device: the drawer, then the Log */
				Phone phone(PORTRAIT);
				if (PhoneSession *session = phone.session()) {
					session->setTcp(true);
					session->connectTcp(QStringLiteral("127.0.0.1"), FAKE_DEVICE_PORT, QString());
					(void) QTest::qWaitFor([&] { return session->deviceId() >= 0; }, 3000);
				}
				phone.openDrawer();
				save(phone, name + QStringLiteral("connected.png"));
				focusField(phone, "tokenField");
				fakeKeyboard(phone, 330);
				save(phone, name + QStringLiteral("drawerkb.png"), 330);
				fakeKeyboard(phone, 0);
				phone.closeDrawer();
				phone.tap(phone.item("tabLog"));
				save(phone, name + QStringLiteral("log.png"));
			}
			{
				/* the Registers screen live, a search, a write's sheet answered, then asking first, an enum, the keyboard */
				Phone phone(PORTRAIT);
				PhoneSession *session = connectFake(phone);
				if (session) session->setWritesEnabled(true);
				phone.settle(800);
				save(phone, name + QStringLiteral("registers.png"));
				search(phone, QStringLiteral("0xd08"));
				phone.settle(300);
				save(phone, name + QStringLiteral("search.png"));
				openSheet(phone, QStringLiteral("FAN_SPEED"));
				phone.type("sheetField", QStringLiteral("60"));
				phone.tap(phone.item("sheetApply"));
				(void) QTest::qWaitFor([&] { return !phone.object("registerSheet")->property("waiting").toBool(); }, 3000);
				phone.settle(600);
				save(phone, name + QStringLiteral("sheet.png"));
				phone.type("sheetField", QStringLiteral("150"));
				phone.tap(phone.item("sheetApply"));
				save(phone, name + QStringLiteral("question.png"));
				closeSheet(phone);
				openSheet(phone, QStringLiteral("FAN_SPEED"));
				focusField(phone, "sheetField");
				fakeKeyboard(phone, 330);
				save(phone, name + QStringLiteral("keyboard.png"), 330);
				fakeKeyboard(phone, 0);
				closeSheet(phone);
				openSheet(phone, QStringLiteral("LED_MODE"));
				save(phone, name + QStringLiteral("enum.png"));
				closeSheet(phone);
				if (session) session->disconnectLink();
			}
			{
				Phone phone(LANDSCAPE);
				PhoneSession *session = connectFake(phone);
				if (session) session->setWritesEnabled(true);
				phone.settle(800);
				save(phone, name + QStringLiteral("land_registers.png"));
				openSheet(phone, QStringLiteral("FAN_SPEED"));
				save(phone, name + QStringLiteral("land_sheet.png"));
				closeSheet(phone);
				search(phone, QString());
				phone.openDrawer();
				save(phone, name + QStringLiteral("land_drawer.png"));
				phone.closeDrawer();
				phone.tap(phone.item("tabLog"));
				save(phone, name + QStringLiteral("land_log.png"));
				phone.tap(phone.item("tabChart"));
				save(phone, name + QStringLiteral("landscape.png"));
				if (session) session->disconnectLink();
			}
			/* the chart's test feed, 12 s in, at both sizes */
			for (QSize size : { LANDSCAPE, PORTRAIT }) {
				Phone phone(size, false, false, true);
				QTest::qWait(12000);
				phone.grab().save(name + (size == LANDSCAPE ? QStringLiteral("chartdemo.png") : QStringLiteral("chartdemo_portrait.png")));
			}
		}
	}
	QSettings().remove(QStringLiteral("ui/dark"));
	language::apply(*qApp, QStringLiteral("en"));
}

void checkThreadWarnings() {
	const int seen = threadWarnings.load();
	check(seen == 0, QStringLiteral("phone: no Qt warnings about objects used across threads (%1 seen)").arg(seen));
}

} // namespace

int main(int argc, char **argv) {
	previousHandler = qInstallMessageHandler(countThreadWarnings);
	QGuiApplication app(argc, argv);
	QGuiApplication::setOrganizationName(QStringLiteral("teknile"));
	QGuiApplication::setApplicationName(QStringLiteral("EVReStudioPhoneTest")); /* not the user's settings */
	/* the settings and the recordings in a folder of this run's own: two runs at once (two agents, two builds) shared
	 * the registry's key before, and one's theme switch turned the other's pictures light */
	QTemporaryDir own;
	phone::useOwnFolder(own.path());
	QSettings().clear();
	language::apply(app, QStringLiteral("en"));
	phone::setStyle();
	/* the pictures alone, without the checks (EVRE_PHONE_SHOT with EVRE_PHONE_ONLY=shots): for the eye, quickly */
	if (qEnvironmentVariable("EVRE_PHONE_ONLY") == QLatin1String("shots")) {
		savePictures(qEnvironmentVariable("EVRE_PHONE_SHOT"));
		saveChartPictures(qEnvironmentVariable("EVRE_PHONE_SHOT"));
		saveToolsPictures(qEnvironmentVariable("EVRE_PHONE_SHOT"));
		savePartBPictures(qEnvironmentVariable("EVRE_PHONE_SHOT"));
		QSettings().clear();
		return 0;
	}

	/* part B's pictures alone */
	if (qEnvironmentVariable("EVRE_PHONE_ONLY") == QLatin1String("partb")) {
		savePartBPictures(qEnvironmentVariable("EVRE_PHONE_SHOT"));
		QSettings().clear();
		return 0;
	}
	/* EVRE_PHONE_ONLY=<text>: only the checks whose function's name holds it (a run of a few, by hand) */
	const QString only = qEnvironmentVariable("EVRE_PHONE_ONLY");
#define RUN(f) if (only.isEmpty() || QStringLiteral(#f).contains(only)) f()
	RUN(checkLoads);
	RUN(checkTabs);
	RUN(checkDrawer);
	RUN(checkTheme);
	RUN(checkScrim);
	RUN(checkLanguageChoice);
	RUN(checkArabic);
	RUN(checkTexts);
	RUN(checkP9);
	RUN(checkChartBins);
	RUN(checkChartDrawn);
	RUN(checkChartPinch);
	RUN(checkChartDragAndLive);
	RUN(checkChartTheme);
	RUN(checkChartArabic);
	RUN(checkChartDemo);
	RUN(checkChartPlotted);
	RUN(checkChartHide);
	RUN(checkChartUnplot);
	RUN(checkChartClear);
	RUN(checkChartYRange);
	RUN(checkChartFast);
	RUN(checkChartEmpty);
	RUN(checkChartLegendThemes);
	RUN(checkCursorButtons);
	RUN(checkCursorDrag);
	RUN(checkCursorReadouts);
	RUN(checkCursorLateData);
	RUN(checkCursorPanelScroll);
	RUN(checkCursorPairs);
	RUN(checkMonitorFrames);
	RUN(checkMonitorEmpty);
	RUN(checkMonitorMono);
	RUN(checkToolsTexts);
	RUN(checkChartInfinite);
	RUN(checkFastPlotLimit);
	RUN(checkChartButtonsStay);
	RUN(checkChartLandscapeRoom);
	RUN(checkChartFullScreen);
	RUN(checkChartLegendScroll);
	RUN(checkMenuInside);
	RUN(checkLogScreen);
	RUN(checkTcpLink);
	RUN(checkRefusedLink);
	RUN(checkReconnect);
	RUN(checkChartLinkGap);
	RUN(checkTokenStops);
	RUN(checkUsbPorts);
	RUN(checkUsbPermission);
	RUN(checkUsbGone);
	RUN(checkDeviceIdRetry);
	RUN(checkRegisterRows);
	RUN(checkRegisterValues);
	RUN(checkRegisterWrite);
	RUN(checkRegisterLimit);
	RUN(checkRegisterRefused);
	RUN(checkWritesOff);
	RUN(checkRegisterGlow);
	RUN(checkRegisterStale);
	RUN(checkRegisterSearch);
	RUN(checkRegisterPlot);
	RUN(checkDrawnSymbols);
	RUN(checkRegisterTexts);
	RUN(checkRegisterThemes);
	RUN(checkPerfLine);
	RUN(checkKeyboard);
	RUN(checkSearchClear);
	RUN(checkOfflineButton);
	RUN(checkTabIcons);
	RUN(checkTouchTargets);
	RUN(checkEnumChoices);
	RUN(checkArabicStart);
	RUN(checkMutedContrast);
	RUN(checkSegmentContrast);
	RUN(checkOwnSettings);
	RUN(checkService);
	RUN(checkOrientation);
	RUN(checkHelpTopics);
	RUN(checkHelpPhoneList);
	RUN(checkBackOrder);
	RUN(checkBackWhileOpening);
	RUN(checkBackToBackground);
	RUN(checkBackKeyboard);
	RUN(checkMenuFocus);
	RUN(checkUsbBack);
	RUN(checkDeviceLineAfterDisconnect);
	RUN(checkTypedNumbers);
	RUN(checkRowDensity);
	RUN(checkRowSheet);
	RUN(checkWriteByEnter);
	RUN(checkTrendBounded);
	RUN(checkPill);
	RUN(checkHeaderSlim);
	RUN(checkLogEmpty);
	RUN(checkSearchEmpty);
	RUN(checkHeadersOverRows);
	RUN(checkLogLevels);
	RUN(checkMonitorKinds);
	RUN(checkFrameSheet);
	RUN(checkMotion);
	RUN(checkHaptics);
	RUN(checkLandscapeRail);
	RUN(checkSheetAcross);
	RUN(checkArabicRuns);
#undef RUN
	if (!qEnvironmentVariableIsEmpty("EVRE_PHONE_SHOT")) {
		if (only.isEmpty()) savePictures(qEnvironmentVariable("EVRE_PHONE_SHOT"));
		if (only.isEmpty() || only.contains(QLatin1String("Chart"))) saveChartPictures(qEnvironmentVariable("EVRE_PHONE_SHOT"));
		if (only.isEmpty() || only.contains(QLatin1String("Cursor")) || only.contains(QLatin1String("Monitor")))
			saveToolsPictures(qEnvironmentVariable("EVRE_PHONE_SHOT"));
		if (only.isEmpty()) savePartBPictures(qEnvironmentVariable("EVRE_PHONE_SHOT"));
	}
	QCoreApplication::processEvents();
	checkThreadWarnings();

	QSettings().clear();
	std::printf("\n%d passed, %d failed\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
