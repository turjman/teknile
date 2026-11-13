/* SPDX-License-Identifier: Apache-2.0 */
/* evre_phone_test: the phone's window (src/phone, Qt Quick) driven by QtTest, as the phone program loads it
 * (phone_app.cpp): Main.qml at a phone's portrait (412 x 915) and landscape (915 x 412), touched by QTest::touchEvent,
 * its pixels read from QQuickWindow::grabWindow() (an area scaled by the picture's devicePixelRatio: the owner's
 * Windows runs at 225 %). STUDIO.md 26.11.
 *
 *   evre_phone_test
 *
 * It needs no device: the maps beside it (the build copies maps/) give P-9 its rows. Settings: its own application
 * (EVReStudioPhoneTest), cleared at the start. On Linux under xvfb it draws with Mesa through OpenGL
 * (QSG_RHI_BACKEND=opengl); the software adaptation would not draw the chart's nodes to come.
 *
 * EVRE_PHONE_SHOT=<prefix> also saves the window's pictures for the eye: <prefix>_<en|ar>_<dark|light>_<portrait|
 * drawer|landscape>.png (not checks).
 *
 * Exit code: 0 all passed, 1 a check failed. */
#include <QElapsedTimer>
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
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>
#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>

#include "model/device_map.h"
#include "phone/p9_proof.h"
#include "phone/phone_app.h"
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

	explicit Phone(QSize size, bool p9 = false) : engine(std::make_unique<QQmlApplicationEngine>()) {
		QObject::connect(engine.get(), &QQmlEngine::warnings, [this](const QList<QQmlError> &list) {
			for (const QQmlError &error : list) warnings << error.toString();
		});
		window = phone::loadMain(*engine, p9, size);
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
		if (on) tap(on->mapToScene(QPointF(on->width() / 2, on->height() / 2)));
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
	static const QStringList kept{ QStringLiteral("English") }; /* the language list: each language in its own words */
	QStringList found;
	forEachItem(topItem(window), [&](QQuickItem *item) {
		if (!item->inherits("QQuickText") || !item->isVisible()) return;
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

/* EVRE_PHONE_SHOT: both languages and themes, portrait, the drawer open, landscape */
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
			Phone phone(LANDSCAPE);
			phone.tap(phone.item("tabChart"));
			phone.grab().save(name + QStringLiteral("landscape.png"));
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
	if (!qEnvironmentVariableIsEmpty("EVRE_PHONE_SHOT")) savePictures(qEnvironmentVariable("EVRE_PHONE_SHOT"));
	QCoreApplication::processEvents();
	checkThreadWarnings();

	QSettings().clear();
	std::printf("\n%d passed, %d failed\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
