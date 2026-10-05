/* SPDX-License-Identifier: Apache-2.0 */
/* EVRe Studio: a register tool for any EVRe device, over TCP or a serial port.
 * Device maps are JSON (maps/). Live values, charts, CSV recording.
 *
 *   EVReStudio [--tcp host:port | --serial COMx[:baud]] [--map file.json | --bus bus.json]
 *              [--plot NAME,NAME] [--tab registers|chart|monitor|map] [--connect]
 *              [--interval ms] [--inflight n] [--record file.csv] [--api] [--api-writes] [--api-writes-danger]
 *   The token, if the server needs one, from the environment: EVRE_TOKEN.
 *
 * This file reads the command line, applies the saved theme and opens the window. It also holds the
 * EVRE_SHOT test aid, which pictures the window and quits.
 */
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDialog>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStringList>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <optional>

#include "ui/language.h"
#include "ui/main_window.h"
#include "ui/theme.h"

namespace {

/* The command line, as what the window applies once it is shown. --help, --version and a bad
 * option end the program here (QCommandLineParser::process). */
MainWindow::Startup parseCommandLine(const QApplication &app) {
	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("EVRe register tool"));
	parser.addHelpOption();
	parser.addVersionOption();
	const QCommandLineOption tcp(QStringLiteral("tcp"), QStringLiteral("Connect over TCP."),
			QStringLiteral("host:port"));
	const QCommandLineOption serial(QStringLiteral("serial"), QStringLiteral("Connect over a serial port."),
			QStringLiteral("COMx[:baud]"));
	const QCommandLineOption map(QStringLiteral("map"), QStringLiteral("Register map to load."),
			QStringLiteral("file.json"));
	const QCommandLineOption bus(QStringLiteral("bus"),
			QStringLiteral("Several devices on the link: a bus file to load, in place of a map."), QStringLiteral("bus.json"));
	const QCommandLineOption plot(QStringLiteral("plot"), QStringLiteral("Registers to chart."),
			QStringLiteral("NAME,NAME"));
	const QCommandLineOption tab(QStringLiteral("tab"), QStringLiteral("Tab to show."),
			QStringLiteral("registers|chart|monitor|map"));
	const QCommandLineOption interval(QStringLiteral("interval"),
			QStringLiteral("Poll interval, ms (0.25 = 4000/s, 0 = as fast as possible)."), QStringLiteral("ms"));
	const QCommandLineOption inFlight(QStringLiteral("inflight"), QStringLiteral("Requests in flight (pipelining)."),
			QStringLiteral("n"));
	const QCommandLineOption record(QStringLiteral("record"), QStringLiteral("Record every poll to this CSV file."),
			QStringLiteral("file.csv"));
	const QCommandLineOption connectAtStart(QStringLiteral("connect"), QStringLiteral("Connect at start."));
	const QCommandLineOption api(QStringLiteral("api"), QStringLiteral("Serve the API (EVRe :1219, JSON :1220)."));
	const QCommandLineOption apiWrites(QStringLiteral("api-writes"),
			QStringLiteral("Allow API clients to write (not the danger registers)."));
	const QCommandLineOption apiWritesDanger(QStringLiteral("api-writes-danger"),
			QStringLiteral("Allow API clients to write the danger registers too."));
	parser.addOptions({ tcp, serial, map, bus, plot, tab, interval, inFlight, record, connectAtStart, api, apiWrites,
			apiWritesDanger });
	parser.process(app);

	MainWindow::Startup startup;
	startup.tcp = parser.value(tcp);
	startup.serial = parser.value(serial);
	startup.map = parser.value(map);
	startup.bus = parser.value(bus);
	startup.tab = parser.value(tab);
	startup.plot = parser.value(plot).split(QLatin1Char(','), Qt::SkipEmptyParts);
	startup.connect = parser.isSet(connectAtStart);
	startup.interval = parser.isSet(interval) ? parser.value(interval).toDouble() : -1; /* 0.25 = 4000/s, 0 = max */
	startup.record = parser.value(record);
	startup.inFlight = parser.value(inFlight).toInt();
	/* each API switch implies the ones before it: danger writes -> writes -> serve */
	startup.apiDanger = parser.isSet(apiWritesDanger);
	startup.apiWrites = parser.isSet(apiWrites) || startup.apiDanger;
	startup.api = parser.isSet(api) || startup.apiWrites;
	return startup;
}

/* A test aid, EVRE_SHOT="file.png;ms;WxH;extras": the window opens at W x H, and after ms (3000 when
 * left out) it saves a picture of itself to file.png and quits. Only this window is pictured, never
 * another running instance, and the run keeps settings of its own (EVReStudio-test).
 * The optional extras, comma separated, add pictures named file_<extra>.png:
 *   side    the whole sidebar, the part scrolled away included
 *   log     the Log tab
 *   help    the Help window
 *   map     the Map editor tab
 *   fields  the same, on the first register with bit fields, its Bit fields page
 *   mapset  the Map settings dialog
 *   busdev  the dialog of a new device on a bus (+ Device; a bus must be open)
 *   monitor the Monitor tab
 *   devices the Registers tab's device list, open (a bus must be open) */
struct ScreenshotRequest {
	bool testRun = false;              /* EVRE_SHOT is set */
	QString file;                      /* empty: no picture, and the run does not quit by itself */
	int delayMs = 0;
	std::optional<QSize> windowSize;
	QStringList extras;
};

ScreenshotRequest screenshotRequestFromEnvironment() {
	const QString text = qEnvironmentVariable("EVRE_SHOT");
	const QStringList parts = text.split(QLatin1Char(';'));
	ScreenshotRequest request;
	request.testRun = !text.isEmpty();
	request.file = parts.value(0);
	request.delayMs = parts.value(1, QStringLiteral("3000")).toInt();
	if (parts.size() >= 3) {
		const QStringList size = parts[2].split(QLatin1Char('x'));
		if (size.size() == 2) request.windowSize = QSize(size[0].toInt(), size[1].toInt());
	}
	request.extras = parts.value(3).split(QLatin1Char(','), Qt::SkipEmptyParts);
	return request;
}

/* file.png -> file_<extra>.png */
QString extraPicturePath(const QString &file, const QString &extra) {
	QString path = file;
	path.insert(path.lastIndexOf(QLatin1Char('.')), QStringLiteral("_") + extra);
	return path;
}

QPushButton *buttonWithText(const QWidget &window, const QString &text) {
	for (QPushButton *button : window.findChildren<QPushButton *>())
		if (button->text() == text) return button;
	return nullptr;
}

void saveSidebarPicture(const QWidget &window, const QString &path) {
	const auto *scroll = window.findChild<QScrollArea *>(QStringLiteral("sideScroll"));
	if (scroll && scroll->widget()) scroll->widget()->grab().save(path);
}

void saveLogPicture(QWidget &window, const QString &path) {
	if (auto *tabs = window.findChild<QTabWidget *>()) tabs->setCurrentIndex(MainWindow::TabLog);
	QApplication::processEvents();
	window.grab().save(path);
}

void saveHelpPicture(const QWidget &window, const QString &path) {
	if (QPushButton *help = buttonWithText(window, QStringLiteral("Help"))) help->click();
	QApplication::processEvents();
	for (QWidget *topLevel : QApplication::topLevelWidgets())
		if (topLevel != &window && topLevel->isVisible() && topLevel->windowTitle().contains(QLatin1String("Help")))
			topLevel->grab().save(path);
}

/* the Map editor tab, then the tab that was shown again. withFields: the first register whose "More"
 * column names fields, on its Bit fields page; otherwise the first register, on General */
void saveMapEditorPicture(QWidget &window, const QString &path, bool withFields) {
	auto *tabs = window.findChild<QTabWidget *>();
	auto *table = window.findChild<QTableView *>(QStringLiteral("mapTable"));
	auto *pages = window.findChild<QTabWidget *>(QStringLiteral("editorPages"));
	if (!tabs || !table || !pages) return;
	const int shown = tabs->currentIndex();
	tabs->setCurrentIndex(MainWindow::TabMap);
	int row = 0;
	const int more = table->model()->columnCount() - 1;
	for (int r = 0; withFields && r < table->model()->rowCount(); r++) {
		if (table->model()->index(r, more).data().toString().contains(QLatin1String("field"))) {
			row = r;
			break;
		}
	}
	if (table->model()->rowCount() > 0) table->selectRow(row);
	pages->setCurrentIndex(withFields ? 2 : 0);
	QApplication::processEvents();
	window.grab().save(path);
	tabs->setCurrentIndex(shown);
}

/* The dialog a button opens. It is modal: the click returns only once the timer below has pictured and closed it. */
void saveDialogPicture(const QWidget &window, const QString &path, const QString &button) {
	QTimer::singleShot(400, [path] {
		if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
			dialog->grab().save(path);
			dialog->reject();
		}
	});
	if (QPushButton *opener = buttonWithText(window, button)) opener->click();
}

/* the Monitor tab, then the tab that was shown again */
void saveMonitorPicture(QWidget &window, const QString &path) {
	auto *tabs = window.findChild<QTabWidget *>();
	const int shown = tabs->currentIndex();
	tabs->setCurrentIndex(MainWindow::TabMonitor);
	window.grab().save(path);
	tabs->setCurrentIndex(shown);
}

/* the Registers tab's device list, open: the rows as the user sees them */
void saveDevicePickerPicture(QWidget &window, const QString &path) {
	auto *box = window.findChild<QComboBox *>(QStringLiteral("registersDevice"));
	if (!box || box->isHidden()) return;
	box->showPopup();
	QApplication::processEvents();
	box->view()->window()->grab().save(path);
	box->hidePopup();
}

void takeScreenshotsAndQuit(QWidget &window, const ScreenshotRequest &request) {
	window.grab().save(request.file);
	for (const QString &extra : request.extras) {
		const QString path = extraPicturePath(request.file, extra);
		if (extra == QLatin1String("side")) saveSidebarPicture(window, path);
		else if (extra == QLatin1String("log")) saveLogPicture(window, path);
		else if (extra == QLatin1String("help")) saveHelpPicture(window, path);
		else if (extra == QLatin1String("map")) saveMapEditorPicture(window, path, false);
		else if (extra == QLatin1String("fields")) saveMapEditorPicture(window, path, true);
		else if (extra == QLatin1String("mapset")) saveDialogPicture(window, path, QStringLiteral("Map settings…"));
		else if (extra == QLatin1String("busdev")) saveDialogPicture(window, path, QStringLiteral("+ Device"));
		else if (extra == QLatin1String("monitor")) saveMonitorPicture(window, path);
		else if (extra == QLatin1String("devices")) saveDevicePickerPicture(window, path);
	}
	QApplication::quit();
}

} // namespace

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	QApplication::setOrganizationName(QStringLiteral("teknile"));
	QApplication::setApplicationName(QStringLiteral("EVReStudio"));
	QApplication::setApplicationVersion(QStringLiteral(EVRE_STUDIO_VERSION));

	const MainWindow::Startup startup = parseCommandLine(app);
	const ScreenshotRequest screenshot = screenshotRequestFromEnvironment();
	if (screenshot.testRun) /* settings of its own, before anything reads them */
		QApplication::setApplicationName(QStringLiteral("EVReStudio-test"));

	language::apply(app, language::resolve(language::saved()));
	Theme::apply(app, QSettings().value(QStringLiteral("ui/dark"), true).toBool());
	int result = 0;
	{ /* the window gone (its ports and its link closed) before the program starts again in another language */
		MainWindow window(startup.map, startup.bus);
		if (screenshot.windowSize) window.resize(*screenshot.windowSize);
		window.show();
		window.applyStartup(startup);
		if (!screenshot.file.isEmpty()) {
			QTimer::singleShot(screenshot.delayMs, &window,
					[&window, screenshot] { takeScreenshotsAndQuit(window, screenshot); });
		}
		result = app.exec();
	}
	if (MainWindow::restartAsked())
		QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
	return result;
}
