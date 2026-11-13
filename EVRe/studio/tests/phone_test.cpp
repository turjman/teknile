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
 * The chart's checks (B.3a) feed its lines from the test (a sine at 1 kHz), compare its bins with the desktop chart's
 * binning (chart_data.h), find the line's colour in the picture, and pinch, drag and double tap it.
 *
 * EVRE_PHONE_SHOT=<prefix> also saves the window's pictures for the eye: <prefix>_<en|ar>_<dark|light>_<portrait|
 * drawer|connected|log|landscape|chartdemo|chartdemo_portrait>.png (not checks; connected and log with the fake
 * device, chartdemo with the chart's test feed). EVRE_FAKE_PORT: the fake device on another port than 1210 (a second
 * run on the same PC).
 *
 * Exit code: 0 all passed, 1 a check failed. */
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPointer>
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
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>

#include "evre/frame.h"
#include "evre/usb_bridge.h"
#include "model/device_map.h"
#include "model/register_model.h"
#include "phone/chart_demo.h"
#include "phone/chart_item.h"
#include "phone/p9_proof.h"
#include "phone/phone_app.h"
#include "phone/phone_session.h"
#include "ui/chart_data.h"
#include "ui/language.h"
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

	explicit Phone(QSize size, bool p9 = false, bool chartDemo = false) : engine(std::make_unique<QQmlApplicationEngine>()) {
		QObject::connect(engine.get(), &QQmlEngine::warnings, [this](const QList<QQmlError> &list) {
			for (const QQmlError &error : list) warnings << error.toString();
		});
		window = phone::loadMain(*engine, p9, size, chartDemo);
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

	bool drawerOpen() const {
		const QObject *drawer = object("connectDrawer");
		return drawer && drawer->property("visible").toBool() && drawer->property("position").toReal() > 0.99;
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
	return found;
}

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
		QStringLiteral("10 s"), QStringLiteral("1 min"), QStringLiteral("5 min") };
	/* the data shown as it is: the map's device name, a USB device's name and IDs */
	static const QStringList data{ QStringLiteral("mapText"), QStringLiteral("portText") };
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

void checkTheme() {
	Phone phone(PORTRAIT);
	const ThemeColors dark = Theme::colorsFor(true), light = Theme::colorsFor(false);
	/* a point of the screen away from its note: the window's background; and one in the header: its panel */
	const QPointF inScreen(12, phone.item("header") ? phone.item("header")->height() + 24 : 80);
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
	ChartItem *chart = showChart(phone);
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
	ChartItem *chart = showChart(phone);
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
	int inside = 0, outside = 0, columnsHit = 0;
	QVector<bool> column(image.width(), false);
	for (int y = 0; y < image.height(); ++y)
		for (int x = 0; x < image.width(); ++x) {
			if (!near(image.pixelColor(x, y), SINE_COLOUR, 8)) continue;
			if (device.contains(QPointF(x + 0.5, y + 0.5))) {
				++inside;
				column[x] = true;
			} else if (!room.contains(QPointF(x + 0.5, y + 0.5))) {
				++outside;
			}
		}
	for (bool hit : std::as_const(column)) columnsHit += hit;
	/* the line runs across the plot: nearly every column of it holds some of the line */
	check(chart && inside > 0 && outside == 0 && columnsHit >= int(device.width() * 0.95),
			QStringLiteral("phone, chart: a fed sine is drawn in its colour inside the plot and nowhere outside it (dpr %1: "
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

void checkChartPinch() {
	Phone phone(LANDSCAPE);
	ChartItem *chart = showChart(phone);
	QQuickItem *area = phone.item("chartArea");
	const double before = chart ? chart->windowSeconds() : 0;
	QQuickItem *oneMinute = phone.item("window60");
	const bool chosenBefore = oneMinute && oneMinute->property("checked").toBool();
	/* the handler takes the fingers once they have moved past the drag threshold: from 160 px apart (taken by then)
	 * to 320, twice the spread, the window is halved */
	double middle = 0;
	if (area) pinch(phone, area, 100, 160, 320, [&] { middle = chart ? chart->windowSeconds() : 0; });
	const double after = chart ? chart->windowSeconds() : 0;
	const bool chosenAfter = oneMinute && oneMinute->property("checked").toBool();
	const double ratio = after > 0 ? middle / after : 0;
	check(chart && before == 60 && middle < 60 && ratio > 1.95 && ratio < 2.05 && chart->live() && chosenBefore
			&& !chosenAfter,
			QStringLiteral("phone, chart: a pinch to twice the fingers' spread halves the window (%1 s, %2 s at 160 px "
					"apart, %3 s at 320: %4 times shorter), the view stays live and the 1 min choice is no longer marked")
					.arg(before).arg(middle, 0, 'f', 2).arg(after, 0, 'f', 2).arg(ratio, 0, 'f', 3));
}

void checkChartDragAndLive() {
	Phone phone(LANDSCAPE);
	ChartItem *chart = showChart(phone);
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

/* in Arabic the screen is mirrored but the plot is not: value labels left of it, time labels in rising order left to
 * right, each under its grid line */
void checkChartArabic() {
	language::apply(*qApp, QStringLiteral("ar"));
	{
		Phone phone(LANDSCAPE);
		ChartItem *chart = showChart(phone);
		QVector<double> times, values;
		sine(5, 20, times, values);
		if (chart) {
			chart->setWindowSeconds(10);
			chart->addLine(1, SINE_COLOUR);
			chart->appendSamples(1, times, values);
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
		const bool screenMirrored = phone.item("liveButton")
				&& phone.item("liveButton")->mapToScene(QPointF(0, 0)).x() > rightEdge / 2;
		check(chart && valueLabels >= 2 && timeLabels >= 2 && valuesLeft && timesUnder && rising && screenMirrored
				&& plot.left() < rightEdge / 4,
				QStringLiteral("phone, Arabic: the controls mirrored (Live at the right), the plot not: its value labels "
						"left of it (%1), time rising left to right, each label under its grid line (%2), the plot from "
						"x %3").arg(valueLabels).arg(timeLabels).arg(plot.left()));
	}
	language::apply(*qApp, QStringLiteral("en"));
}

/* the test feed (chartdemo): 8 lines at 1 kHz live in a 1 min window, a line a second for logcat under EVRE_PERF */
void checkChartDemo() {
	Phone phone(LANDSCAPE, false, true);
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
	void open(const QString &, qint32, Sink *to) override {
		sink = to;
		opens++;
	}
	void send(const QByteArray &) override {}
	void close() override {
		closes++;
		sink = nullptr;
	}
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
	check(session && up && session->tcp() && link == QStringLiteral("Connected: ") + wanted && pill == QLatin1String("Connected")
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
	const QString count = phone.text("logCount");
	const bool mapLine = session && session->log()->contains(QStringLiteral("map loaded: "));
	phone.tap(phone.item("logClear"));
	const bool cleared = session && session->log()->count() == 0 && list && list->property("count").toInt() == 0
			&& phone.item("logEmpty") && phone.item("logEmpty")->isVisible();
	check(lines >= 2 && mapLine && red && count == QStringLiteral("lines: %1").arg(lines) && cleared,
			QStringLiteral("phone, Log screen: the session's lines (%1: the map loaded, a map that did not), an error in "
					"red, \"%2\"; Clear empties it and says nothing is there yet").arg(lines).arg(count));
}

/* EVRE_PHONE_SHOT: both languages and themes, portrait, the drawer open, connected (the drawer, the Log), landscape */
void savePictures(const QString &prefix) {
	for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
		language::apply(*qApp, code);
		for (bool dark : { true, false }) {
			QSettings().setValue(QStringLiteral("ui/dark"), dark);
			const QString name = QStringLiteral("%1_%2_%3_").arg(prefix, code, dark ? "dark" : "light");
			{
				Phone phone(PORTRAIT);
				phone.grab().save(name + QStringLiteral("portrait.png"));
				phone.openDrawer();
				phone.grab().save(name + QStringLiteral("drawer.png"));
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
				phone.grab().save(name + QStringLiteral("connected.png"));
				phone.closeDrawer();
				phone.tap(phone.item("tabLog"));
				phone.grab().save(name + QStringLiteral("log.png"));
			}
			{
				Phone phone(LANDSCAPE);
				phone.tap(phone.item("tabChart"));
				phone.grab().save(name + QStringLiteral("landscape.png"));
			}
			/* the chart's test feed, 12 s in, at both sizes */
			for (QSize size : { LANDSCAPE, PORTRAIT }) {
				Phone phone(size, false, true);
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
	QSettings().clear();
	language::apply(app, QStringLiteral("en"));
	phone::setStyle();

	checkLoads();
	checkTabs();
	checkDrawer();
	checkTheme();
	checkLanguageChoice();
	checkArabic();
	checkTexts();
	checkP9();
	checkChartBins();
	checkChartDrawn();
	checkChartPinch();
	checkChartDragAndLive();
	checkChartTheme();
	checkChartArabic();
	checkChartDemo();
	checkLogScreen();
	checkTcpLink();
	checkRefusedLink();
	checkReconnect();
	checkTokenStops();
	checkUsbPorts();
	checkUsbPermission();
	checkUsbGone();
	if (!qEnvironmentVariableIsEmpty("EVRE_PHONE_SHOT")) savePictures(qEnvironmentVariable("EVRE_PHONE_SHOT"));
	QCoreApplication::processEvents();
	checkThreadWarnings();

	QSettings().clear();
	std::printf("\n%d passed, %d failed\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
