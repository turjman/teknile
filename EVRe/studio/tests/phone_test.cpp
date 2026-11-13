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
 * The Registers screen's checks (B.2) run against the fake device too: its values, a write read back, the limits asked
 * first, a refused write (a map with a register the device does not have), the glow in the picture; a stand-in that
 * leaves the first DEVICE_ID request unanswered checks that the ID is read again by itself.
 *
 * EVRE_PHONE_SHOT=<prefix> also saves the window's pictures for the eye: <prefix>_<en|ar>_<dark|light>_<name>.png,
 * the names portrait, drawer, connected, drawerkb, log, registers, search, sheet, question, keyboard, enum and, at
 * 915 x 412, land_registers, land_sheet, land_drawer, land_log, landscape (not checks; all but the first two with the
 * fake device; drawerkb and keyboard with a keyboard stood for, drawn grey). EVRE_PHONE_ONLY=shots saves them without
 * running the checks.
 *
 * Exit code: 0 all passed, 1 a check failed. */
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
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
#include "phone/p9_proof.h"
#include "phone/phone_app.h"
#include "phone/phone_session.h"
#include "phone/register_list.h"
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

	explicit Phone(QSize size, bool p9 = false, bool perf = false) : engine(std::make_unique<QQmlApplicationEngine>()) {
		QObject::connect(engine.get(), &QQmlEngine::warnings, [this](const QList<QQmlError> &list) {
			for (const QQmlError &error : list) warnings << error.toString();
		});
		window = phone::loadMain(*engine, p9, size, perf);
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
	/* the language list (each language in its own words), and the links' names */
	static const QStringList kept{ QStringLiteral("English"), QStringLiteral("USB"), QStringLiteral("TCP") };
	/* the data shown as it is: the map's device name, a USB device's name and IDs; a register's name, group, value,
	 * unit, decoded text, address and the device's error */
	static const QStringList data{ QStringLiteral("mapText"), QStringLiteral("portText"), QStringLiteral("regName"),
		QStringLiteral("regGroup"), QStringLiteral("regValue"), QStringLiteral("regUnit"), QStringLiteral("regDecoded"),
		QStringLiteral("regAddr"), QStringLiteral("regError"), QStringLiteral("sheetName"), QStringLiteral("sheetAddr"),
		QStringLiteral("sheetNow"), QStringLiteral("sheetUnit") };
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
	/* a point of the screen beside the cards, at the list's foot: the window's background; one in the header: its panel */
	const QQuickItem *list = phone.item("registerList");
	const QPointF inScreen = list ? list->mapToScene(QPointF(4, list->height() - 4)) : QPointF(4, 600);
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

/* ---- the link (B.1): over TCP against the fake device and stand-ins, over USB through a fake bridge */

constexpr quint16 FAKE_DEVICE_PORT = 1210;     /* tests/fake_device.py --port 1210 --map maps/example_device.json */
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

/* ---- the Registers screen (B.2): the cards over TCP against the fake device */

/* an item under `root` by its objectName (a card's own: every card has a "regValue") */
QQuickItem *within(QQuickItem *root, const QString &name) {
	QQuickItem *found = nullptr;
	forEachItem(root, [&](QQuickItem *item) {
		if (!found && item->objectName() == name) found = item;
	});
	return found;
}

/* a register's card, by its name: created only while it is in the list's view (search for it first) */
QQuickItem *card(const Phone &phone, const QString &reg) {
	QQuickItem *list = phone.item("registerList");
	return list ? within(list, QStringLiteral("card_") + reg) : nullptr;
}

QString cardText(const Phone &phone, const QString &reg, const char *part) {
	QQuickItem *found = within(card(phone, reg), QLatin1String(part));
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
	const QObject *sheet = phone.object("writeSheet");
	return sheet && sheet->property("opened").toBool();
}

/* the write sheet of a register, by its card's Write button */
void openSheet(const Phone &phone, const QString &reg) {
	search(phone, reg);
	phone.tap(within(card(phone, reg), QStringLiteral("writeButton")));
	(void) QTest::qWaitFor([&] { return sheetOpen(phone); }, 3000);
	phone.settle();
}

void closeSheet(const Phone &phone) {
	if (QObject *sheet = phone.object("writeSheet")) QMetaObject::invokeMethod(sheet, "close");
	(void) QTest::qWaitFor([&] { return !phone.object("writeSheet")->property("visible").toBool(); }, 3000);
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
	/* the first section's header and card drawn: the map's first group and register */
	QQuickItem *view = phone.item("registerList");
	const QString header = view ? within(view, QStringLiteral("regGroup")) ? within(view, QStringLiteral("regGroup"))->property("text").toString() : QString() : QString();
	const QString firstAddr = cardText(phone, map.regs.value(0).name, "regAddr");
	const bool offline = phone.item("registersOffline") && phone.item("registersOffline")->isVisible();
	check(list && list->total() == map.regs.size() && list->count() == map.regs.size() && sortedNames == sortedMap && together
					&& groups.size() == 5 && header == map.regs.value(0).group && firstAddr == QLatin1String("0xA000")
					&& offline && phone.text("registerCount") == QStringLiteral("registers: %1").arg(map.regs.size()),
			QStringLiteral("phone, Registers: a card per register of the map (%1 of %2), each group's together in sections "
					"(%3), the first header \"%4\" and card's address %5; not connected, a line says so with Connect")
					.arg(names.size()).arg(map.regs.size()).arg(groups.join(QStringLiteral(", ")), header, firstAddr));
}

void checkRegisterValues() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	search(phone, QStringLiteral("SUPPLY_V"));
	(void) QTest::qWaitFor([&] { return !cardText(phone, QStringLiteral("SUPPLY_V"), "regValue").startsWith(QStringLiteral("—")); }, 3000);
	const QString value = cardText(phone, QStringLiteral("SUPPLY_V"), "regValue");
	const QString unit = cardText(phone, QStringLiteral("SUPPLY_V"), "regUnit");
	bool number = false;
	value.toDouble(&number);
	/* the fake device's sine moves it: a second later another value, from the table */
	QTest::qWait(1000);
	const QString later = cardText(phone, QStringLiteral("SUPPLY_V"), "regValue");
	const QString inModel = modelValue(session, QStringLiteral("SUPPLY_V"));
	const bool offline = phone.item("registersOffline") && phone.item("registersOffline")->isVisible();
	check(session && session->connected() && number && unit == QLatin1String("V") && later != value
					&& (later == inModel || QTest::qWaitFor([&] {
						   return cardText(phone, QStringLiteral("SUPPLY_V"), "regValue") == modelValue(session, QStringLiteral("SUPPLY_V"));
					   }, 1000))
					&& !offline,
			QStringLiteral("phone, Registers: connected to the fake device, SUPPLY_V's card shows its value with its unit "
					"(\"%1\" \"%2\", then \"%3\"), the table's (RegisterModel's text); the not-connected line gone")
					.arg(value, unit, later));
	if (session) session->disconnectLink();
}

/* a write through the sheet: Apply, the answer, the card shows what the device holds (read back) */
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
		return modelValue(session, QStringLiteral("FAN_SPEED")) == wanted && cardText(phone, QStringLiteral("FAN_SPEED"), "regValue") == wanted
				&& phone.text("sheetNow") == wanted + QStringLiteral(" %");
	}, 3000);
	check(opened && startsWithNow && limits == QStringLiteral("The map's limits: from 0 to 100 %")
					&& answered && green && readBack
					&& session->log()->contains(QStringLiteral("written: FAN_SPEED = %1").arg(wanted)),
			QStringLiteral("phone, write: FAN_SPEED's Write opens the sheet (the value now \"%1\", \"%2\"); %3 typed, Apply: "
					"\"%4\" in green, the card and the sheet show %3 read back from the device, the Log \"written: ...\"")
					.arg(before, limits, wanted, phone.text("sheetAnswer")));
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

/* a register the device does not have: its card in red, a write to it refused with the device's text */
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
		const QQuickItem *value = within(card(phone, QStringLiteral("NOWHERE")), QStringLiteral("regValue"));
		const QQuickItem *error = within(card(phone, QStringLiteral("NOWHERE")), QStringLiteral("regError"));
		return value && error && error->isVisible() && near(value->property("color").value<QColor>(), themeColor(&ThemeColors::bad), 2);
	}, 4000);
	const QString shown = cardText(phone, QStringLiteral("NOWHERE"), "regValue");
	const QString why = cardText(phone, QStringLiteral("NOWHERE"), "regError");
	openSheet(phone, QStringLiteral("NOWHERE"));
	phone.type("sheetField", QStringLiteral("1"));
	phone.tap(phone.item("sheetApply"));
	const bool refused = QTest::qWaitFor([&] { return phone.text("sheetAnswer").startsWith(QLatin1String("write refused: NOWHERE = 1")); }, 3000);
	const QString answer = phone.text("sheetAnswer");
	const QQuickItem *answerItem = phone.item("sheetAnswer");
	const bool answerRed = answerItem && near(answerItem->property("color").value<QColor>(), themeColor(&ThemeColors::bad), 2);
	check(loaded && red && shown == QLatin1String("not available") && !why.isEmpty() && refused && answerRed
					&& answer.contains(QLatin1String("offset out of range"))
					&& session->log()->contains(QStringLiteral("write refused: NOWHERE")),
			QStringLiteral("phone, refused write: a register the device does not have shows \"%1\" in red and why (\"%2\"); "
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
			QStringLiteral("phone, write sheet: with no link it says \"%1\", connected with Allow writes off \"%2\", no field and "
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
	const bool changed = QTest::qWaitFor([&] { return cardText(phone, QStringLiteral("FAN_SPEED"), "regValue") == wanted; }, 3000);
	QQuickItem *panel = within(card(phone, QStringLiteral("FAN_SPEED")), QStringLiteral("cardPanel"));
	/* the bar at the card's start edge, and a spot of the card with nothing on it (its end, half way down) */
	const QPointF barAt = at(panel, 5, panel ? panel->height() / 2 : 0);
	const QPointF tintAt = at(panel, panel ? panel->width() - 6 : 0, panel ? panel->height() - 6 : 0);
	const QImage lit = phone.grab();
	const QColor surface = themeColor(&ThemeColors::surface), accent = themeColor(&ThemeColors::accent);
	const QColor barLit = Phone::pixel(lit, barAt), tintLit = Phone::pixel(lit, tintAt);
	QTest::qWait(int(RegisterModel::GLOW_MS) + 400);
	const QImage plain = phone.grab();
	const QColor barPlain = Phone::pixel(plain, barAt), tintPlain = Phone::pixel(plain, tintAt);
	const QQuickItem *bar = within(card(phone, QStringLiteral("FAN_SPEED")), QStringLiteral("cardGlowBar"));
	check(changed && distance(barLit, surface) > 60 && distance(barLit, accent) < distance(barLit, surface)
					&& tintLit.blue() > surface.blue() + 4 && near(barPlain, surface, 8) && near(tintPlain, surface, 4)
					&& bar && bar->opacity() == 0,
			QStringLiteral("phone, glow: FAN_SPEED written after a rest, its card glows in the picture (dpr %1: the bar %2, the "
					"card %3, on %4) and fades within %5 ms (the bar %6, the card %7)")
					.arg(lit.devicePixelRatio()).arg(barLit.name(), tintLit.name(), surface.name()).arg(RegisterModel::GLOW_MS)
					.arg(barPlain.name(), tintPlain.name()));
	if (session) session->disconnectLink();
}

/* polling off: the values are not refreshed, the cards say so and grey them */
void checkRegisterStale() {
	Phone phone(PORTRAIT);
	PhoneSession *session = connectFake(phone);
	search(phone, QStringLiteral("SUPPLY_V"));
	const QQuickItem *stale = within(card(phone, QStringLiteral("SUPPLY_V")), QStringLiteral("regStale"));
	const bool freshAtFirst = stale && !stale->isVisible();
	if (session) session->setPolling(false);
	const bool greyed = QTest::qWaitFor([&] {
		const QQuickItem *value = within(card(phone, QStringLiteral("SUPPLY_V")), QStringLiteral("regValue"));
		return stale && stale->isVisible() && value && near(value->property("color").value<QColor>(), themeColor(&ThemeColors::muted), 2);
	}, 3000);
	const QString said = cardText(phone, QStringLiteral("SUPPLY_V"), "regStale");
	if (session) session->setPolling(true);
	const bool freshAgain = QTest::qWaitFor([&] { return stale && !stale->isVisible(); }, 3000);
	check(freshAtFirst && greyed && said == QLatin1String("not refreshed") && freshAgain,
			QStringLiteral("phone, stale: Polling off, SUPPLY_V's value greys and its card says \"%1\"; polling again, it is "
					"fresh again").arg(said));
	if (session) session->disconnectLink();
}

void checkRegisterSearch() {
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	RegisterList *list = session ? session->registerList() : nullptr;
	search(phone, QStringLiteral("fan"));
	const int byName = list ? list->count() : -1;
	const bool fanCard = card(phone, QStringLiteral("FAN_SPEED")) && !card(phone, QStringLiteral("SUPPLY_V"));
	const QString count = phone.text("registerCount");
	search(phone, QStringLiteral("0xd08"));
	const int byAddr = list ? list->count() : -1;
	search(phone, QStringLiteral("zzz"));
	const bool none = list && list->count() == 0 && phone.item("registersEmpty") && phone.item("registersEmpty")->isVisible();
	const QString empty = phone.text("registersEmpty");
	phone.tap(phone.item("searchClear"));
	const bool all = list && list->count() == list->total() && phone.text("registerSearch").isEmpty();
	check(byName == 1 && fanCard && count == QStringLiteral("registers: 1 of %1").arg(list ? list->total() : 0) && byAddr == 4
					&& none && empty == QLatin1String("No register matches \"zzz\".") && all,
			QStringLiteral("phone, search: \"fan\" keeps FAN_SPEED alone (\"%1\"), \"0xd08\" the 4 at 0xD08x, \"zzz\" none "
					"(\"%2\"); Clear shows them all again").arg(count, empty));
}

/* the "⋯" button: Plot marks it plotted, the chart's limit says why not, Copy */
void checkRegisterPlot() {
	Phone phone(PORTRAIT);
	PhoneSession *session = phone.session();
	RegisterModel *model = session ? session->registers() : nullptr;
	search(phone, QStringLiteral("SUPPLY_V"));
	phone.tap(within(card(phone, QStringLiteral("SUPPLY_V")), QStringLiteral("moreButton")));
	QObject *menu = phone.object("moreMenu");
	const bool menuOpen = menu && menu->property("opened").toBool();
	const QString plotText = phone.text("menuPlot");
	phone.tap(phone.item("menuPlot"));
	const int row = modelRow(session, QStringLiteral("SUPPLY_V"));
	const bool plotted = model && row >= 0 && model->rows()[row].plot;
	const QQuickItem *mark = within(card(phone, QStringLiteral("SUPPLY_V")), QStringLiteral("regPlotted"));
	const bool marked = mark && mark->isVisible() && mark->property("text").toString() == QLatin1String("on the chart");
	const QString plotNotice = phone.text("registerNoticeText");

	/* one line at most: the next is refused with the desktop's words */
	if (model) model->setPlotLimit(1);
	search(phone, QStringLiteral("SUPPLY_I"));
	phone.tap(within(card(phone, QStringLiteral("SUPPLY_I")), QStringLiteral("moreButton")));
	phone.tap(phone.item("menuPlot"));
	const int other = modelRow(session, QStringLiteral("SUPPLY_I"));
	const bool refused = model && other >= 0 && !model->rows()[other].plot;
	const QString limitNotice = phone.text("registerNoticeText");
	const QQuickItem *noticeBox = phone.item("registerNotice");
	const bool noticeShown = noticeBox && noticeBox->isVisible();

	/* Copy: the name, the value and the unit on the clipboard */
	phone.tap(within(card(phone, QStringLiteral("SUPPLY_I")), QStringLiteral("moreButton")));
	phone.tap(phone.item("menuCopy"));
	const QString copied = QGuiApplication::clipboard() ? QGuiApplication::clipboard()->text() : QString();
	const QString copyNotice = phone.text("registerNoticeText");
	check(menuOpen && plotText == QLatin1String("Plot") && plotted && marked && plotNotice == QLatin1String("SUPPLY_V on the chart")
					&& refused && noticeShown
					&& limitNotice == QLatin1String("At most 1 registers on the chart at 10 samples a second: take one off first"),
			QStringLiteral("phone, Plot: the \"⋯\" button's menu, Plot marks SUPPLY_V plotted (\"on the chart\" on its card, "
					"\"%1\"); with a limit of one line the next is refused: \"%2\"").arg(plotNotice, limitNotice));
	check(copied == QStringLiteral("SUPPLY_I = %1 A").arg(modelValue(session, QStringLiteral("SUPPLY_I")))
					&& copyNotice == QStringLiteral("Copied: ") + copied,
			QStringLiteral("phone, Copy: the \"⋯\" button's Copy puts \"%1\" on the clipboard and says so").arg(copied));
}

/* no symbol characters a phone's font may not have: the menu's bars and the "⋯" dots are drawn */
void checkDrawnSymbols() {
	Phone phone(PORTRAIT);
	static const QString symbols = QStringLiteral("☰⋯▾▸▴▲▼◀▶✎✕");
	QStringList found;
	forEachItem(topItem(phone.window), [&](QQuickItem *item) {
		if (!item->inherits("QQuickText")) return;
		const QString text = item->property("text").toString();
		for (const QChar c : symbols)
			if (text.contains(c)) found << text;
	});
	int bars = 0, dots = 0;
	forEachItem(phone.item("menuButton"), [&](QQuickItem *item) { bars += item->objectName() == QLatin1String("menuBar"); });
	search(phone, QStringLiteral("SUPPLY_V"));
	forEachItem(within(card(phone, QStringLiteral("SUPPLY_V")), QStringLiteral("moreButton")),
			[&](QQuickItem *item) { dots += item->objectName() == QLatin1String("moreDot"); });
	const QString label = phone.item("menuButton") ? phone.item("menuButton")->property("text").toString() : QStringLiteral("?");
	check(found.isEmpty() && bars == 3 && dots == 3 && label.isEmpty(),
			QStringLiteral("phone, symbols: the menu button's three bars and the \"⋯\" button's three dots are drawn (%1 bars, "
					"%2 dots), no Text holds a symbol a phone's font may lack (\"☰\" showed as a box on a Samsung phone) %3")
					.arg(bars).arg(dots).arg(found.join(QStringLiteral(" | "))));
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
			/* a card's parts: the name at the start edge, the value then its unit left to right */
			search(phone, QStringLiteral("SETPOINT"));
			QQuickItem *setpoint = card(phone, QStringLiteral("SETPOINT"));
			auto x = [&](const char *part) {
				const QQuickItem *item = within(setpoint, QLatin1String(part));
				return item ? item->mapToScene(QPointF(item->width() / 2, 0)).x() : -1.0;
			};
			const double width = phone.window ? phone.window->width() : 0;
			const double name = x("regName"), value = x("regValue"), unit = x("regUnit"), more = x("moreButton");
			const bool rtl = code == QLatin1String("ar");
			/* the map's names (Latin) at the start edge too: their text, not only their item */
			auto align = [&](QQuickItem *in, const char *part) {
				const QQuickItem *item = within(in, QLatin1String(part));
				return item ? item->property("effectiveHorizontalAlignment").toInt() : -1;
			};
			const int start = rtl ? Qt::AlignRight : Qt::AlignLeft;
			const int nameAlign = align(setpoint, "regName"), groupAlign = align(phone.item("registerList"), "regGroup");
			const bool ok = value < unit && (rtl ? (value > width / 2 && more < width / 2) : (value < width / 2 && more > width / 2))
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
			"cards and the write sheet asking first: every Text inside its parent and not cut (%1 seen) %2")
			.arg(seen).arg(bad.join(QStringLiteral(" | "))));
	check(latin.isEmpty() && mirror.isEmpty(), QStringLiteral("phone, Registers in Arabic: every text translated but the "
			"map's names and the values; the card mirrored (its buttons at the left), the name, the group and the value at the "
			"right, the value and its unit left to right %1 %2").arg(latin.join(QStringLiteral(" | ")), mirror.join(QStringLiteral(" | "))));
}

/* the Registers screen in both themes: the window's background, a card's panel and its name in the picture */
void checkRegisterThemes() {
	QStringList seen;
	bool ok = true;
	for (bool dark : { true, false }) {
		QSettings().setValue(QStringLiteral("ui/dark"), dark);
		const ThemeColors colors = Theme::colorsFor(dark);
		Phone phone(PORTRAIT);
		QQuickItem *panel = within(card(phone, QStringLiteral("DEVICE_ID")), QStringLiteral("cardPanel"));
		const QImage image = phone.grab();
		const QColor cardColor = Phone::pixel(image, at(panel, panel ? panel->width() - 6 : 0, panel ? panel->height() - 6 : 0));
		const QQuickItem *list = phone.item("registerList");
		const QColor bg = Phone::pixel(image, at(list, 4, list ? list->height() - 4 : 0));
		const QQuickItem *name = within(card(phone, QStringLiteral("DEVICE_ID")), QStringLiteral("regName"));
		const bool nameColor = name && near(name->property("color").value<QColor>(), colors.text, 2);
		const bool fine = panel && near(cardColor, colors.surface, 4) && near(bg, colors.bg, 4) && nameColor;
		seen << QStringLiteral("%1: card %2, background %3").arg(dark ? "dark" : "light", cardColor.name(), bg.name());
		ok = ok && fine;
	}
	QSettings().remove(QStringLiteral("ui/dark"));
	check(ok, QStringLiteral("phone, Registers themes: the cards on the theme's surface, the list on its background, the "
			"names in its text colour, dark and light (%1)").arg(seen.join(QStringLiteral("; "))));
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

/* no link: the card at the top of the Registers screen says how to connect, its button opens the drawer */
void checkOfflineButton() {
	Phone phone(PORTRAIT);
	QQuickItem *card = phone.item("registersOffline");
	const bool shown = card && card->isVisible() && !phone.text("offlineText").isEmpty();
	QQuickItem *button = phone.item("registersConnect");
	const bool target = button && button->height() >= 48;
	phone.tap(button);
	(void) QTest::qWaitFor([&] { return phone.drawerOpen(); }, 3000);
	const bool opened = phone.drawerOpen();
	check(shown && target && opened, QStringLiteral("phone, not connected: the Registers screen says how to connect (\"%1\") and "
			"its Connect button (48 dp) opens the Connect drawer").arg(phone.text("offlineText")));
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
			const bool placed = icon && label && (size == PORTRAIT ? bottomOf(icon) <= topOf(label) + 1
					: icon->mapToScene(QPointF(icon->width(), 0)).x() <= label->mapToScene(QPointF(0, 0)).x() + 1);
			if (!icon || !icon->isVisible() || icon->width() < 20 || strokes < 3 || glyph || inked < 15 || !coloured || !placed)
				bad << QStringLiteral("%1 %2: %3 strokes, %4 inked px%5%6%7").arg(sizeText(size), QLatin1String(name)).arg(strokes)
								.arg(inked).arg(glyph ? ", a glyph" : "").arg(coloured ? "" : ", colour").arg(placed ? "" : ", placed");
		}
	}
	check(bad.isEmpty(), QStringLiteral("phone, tab icons: each tab's icon drawn (rectangles, no glyph) over its name, beside it "
			"in landscape; the chosen tab's in the accent, the others muted %1").arg(bad.join(QStringLiteral(" | "))));
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
	measure(within(card(phone, QStringLiteral("SETPOINT")), QStringLiteral("writeButton")), QStringLiteral("Write"));
	measure(within(card(phone, QStringLiteral("SETPOINT")), QStringLiteral("moreButton")), QStringLiteral("more"));
	measure(phone.item("searchClear"), QStringLiteral("clear"));
	openSheet(phone, QStringLiteral("FAN_SPEED"));
	measure(phone.item("sheetApply"), QStringLiteral("Apply"));
	measure(phone.item("sheetClose"), QStringLiteral("Close"));
	phone.type("sheetField", QStringLiteral("150"));
	phone.tap(phone.item("sheetApply"));
	measure(phone.item("sheetWriteAnyway"), QStringLiteral("Write anyway"));
	measure(phone.item("sheetCancel"), QStringLiteral("Cancel"));
	closeSheet(phone);
	openSheet(phone, QStringLiteral("LED_MODE"));
	measure(phone.item("sheetChoice_2"), QStringLiteral("a choice"));
	closeSheet(phone);
	for (const char *tab : { "tabRegisters", "tabChart", "tabMonitor", "tabLog" }) measure(phone.item(tab), QLatin1String(tab));
	check(small.isEmpty(), QStringLiteral("phone, touch targets: the menu, the pill, a card's Write and \"⋯\", search's clear, "
			"the sheet's Apply, Close, Write anyway, Cancel and an enum's choice, the tabs: each at least 48 x 48 dp %1")
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
	const bool picked = phone.object("writeSheet") && phone.object("writeSheet")->property("chosen").toString() == wanted;
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
void mutedContrast(const Phone &phone, const QString &where, double &lowest, QStringList &bad, int &seen) {
	const QImage image = phone.grab();
	const QColor muted = themeColor(&ThemeColors::muted);
	const QRectF view(0, 0, phone.window->width(), phone.window->height());
	forEachItem(topItem(phone.window), [&](QQuickItem *item) {
		if (!item->inherits("QQuickText") || !item->isVisible() || item->opacity() < 0.99) return;
		if (item->property("text").toString().isEmpty() || !near(item->property("color").value<QColor>(), muted, 2)) return;
		const QRectF box = item->mapRectToScene(QRectF(0, 0, std::min(item->width(), item->property("contentWidth").toDouble()),
				std::min(item->height(), item->property("contentHeight").toDouble())));
		if (!view.contains(box) || box.width() < 4) return;
		/* inside a popup that is still sliding, or hidden under one: only what is plainly in the picture */
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
			mutedContrast(phone, QStringLiteral("sheet"), lowest, bad, seen);
			closeSheet(phone);
			phone.openDrawer();
			mutedContrast(phone, QStringLiteral("drawer"), lowest, bad, seen);
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

/* EVRE_PHONE_SHOT: both languages and themes. Portrait: the start (not connected) and its drawer, connected (the
 * drawer, the Log, the Registers screen, a search, the write sheet after a write, asking first, an enum's list, the
 * sheet and the drawer over a keyboard stood for, drawn grey); landscape: the Registers screen, the drawer, the sheet,
 * the Log and the Chart */
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
				(void) QTest::qWaitFor([&] { return !phone.object("writeSheet")->property("waiting").toBool(); }, 3000);
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
	/* the pictures alone, without the checks (EVRE_PHONE_SHOT with EVRE_PHONE_ONLY=shots): for the eye, quickly */
	if (qEnvironmentVariable("EVRE_PHONE_ONLY") == QLatin1String("shots")) {
		savePictures(qEnvironmentVariable("EVRE_PHONE_SHOT"));
		QSettings().clear();
		return 0;
	}

	checkLoads();
	checkTabs();
	checkDrawer();
	checkTheme();
	checkLanguageChoice();
	checkArabic();
	checkTexts();
	checkP9();
	checkLogScreen();
	checkTcpLink();
	checkRefusedLink();
	checkReconnect();
	checkTokenStops();
	checkUsbPorts();
	checkUsbPermission();
	checkUsbGone();
	checkDeviceIdRetry();
	checkRegisterRows();
	checkRegisterValues();
	checkRegisterWrite();
	checkRegisterLimit();
	checkRegisterRefused();
	checkWritesOff();
	checkRegisterGlow();
	checkRegisterStale();
	checkRegisterSearch();
	checkRegisterPlot();
	checkDrawnSymbols();
	checkRegisterTexts();
	checkRegisterThemes();
	checkPerfLine();
	checkKeyboard();
	checkSearchClear();
	checkOfflineButton();
	checkTabIcons();
	checkTouchTargets();
	checkEnumChoices();
	checkArabicStart();
	checkMutedContrast();
	if (!qEnvironmentVariableIsEmpty("EVRE_PHONE_SHOT")) savePictures(qEnvironmentVariable("EVRE_PHONE_SHOT"));
	QCoreApplication::processEvents();
	checkThreadWarnings();

	QSettings().clear();
	std::printf("\n%d passed, %d failed\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
