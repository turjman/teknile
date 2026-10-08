/* SPDX-License-Identifier: Apache-2.0 */
/* evre_gui_test: drives the real MainWindow against tests/fake_device.py
 * (127.0.0.1:1210) - NEVER a real device: it writes, including a register
 * marked danger. A second TCP client plays "someone else" writing the same
 * registers. Settings go under a separate name, the user's are not touched.
 *
 *   python tests/fake_device.py --port 1210 [--map maps/X.json] &
 *   evre_gui_test [X.json]          (a map in maps/, default example_device.json)
 *
 * The registers it uses are found in the map, so any map that has them will
 * do: the first writable u8 and the first writable 16-bit danger register of
 * the device bank (0xD000 and up), the first writable u8 there with named
 * values (its check fails without one), and the first read-only f32 in V and
 * in A, which a math line multiplies. The protocol's own CONFIG register
 * (0xA004) serves as a danger register of flags. The group check needs a
 * group with "&" in its name, and the login checks a "login" in the map.
 *
 * The login: the window starts with EVRE_TOKEN set to the fake device's
 * token (fake_device.py --token, "example-token" by default). The fake
 * device keeps the last token written to its login register and lets it be
 * read back, so the other client sees what arrived.
 *
 * The steps run in order, each one leaving the window and the device as the
 * next expects. Qt's warnings about objects used across threads are counted
 * too, until the window and its I/O thread are gone: any of them fails the
 * last check (a timer started from the wrong thread never fires, and Qt only
 * warns).
 *
 * Exit code: 0 all passed, 1 a check failed, 2 the map file or the fake
 * device is missing.
 */
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QDropEvent>
#include <QInputDialog>
#include <QMimeData>
#include <QProgressDialog>
#include <QSignalSpy>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QCompleter>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QFrame>
#include <QHeaderView>
#include <QHelpEvent>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGlyphRun>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QTabBar>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTcpSocket>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextLayout>
#include <QTemporaryDir>
#include <QTextDocumentFragment>
#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QToolTip>
#include <QUndoStack>
#include <QWheelEvent>
#include <QWidgetAction>
#include <QXmlStreamReader>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>

#include "evre/frame.h"
#include "io/engine.h"
#include "io/reg_table.h"
#include "model/bus_file.h"
#include "model/device_map.h"
#include "model/map_document.h"
#include "ui/map_settings_dialog.h"
#include "model/recording_file.h"
#include "model/register_model.h"
#include "ui/bit_view.h"
#include "ui/bus_panel.h"
#include "ui/event_log.h"
#include "model/analysis.h"
#include "ui/analysis_window.h"
#include "ui/chart_tab.h"
#include "ui/recording_window.h"
#include "ui/frame_clock.h"
#include "ui/language.h"
#include "ui/elided_label.h"
#include "ui/field_editor.h"
#include "ui/formula_completer.h"
#include "ui/help_dialog.h"
#include "ui/math_line_dialog.h"
#include "ui/chart_widget.h"
#include "ui/main_window.h"
#include "ui/map_editor_tab.h"
#include "ui/limit_spin_box.h"
#include "ui/map_table_model.h"
#include "ui/monitor_tab.h"
#include "ui/registers_tab.h"
#include "ui/sidebar.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"
#include "ui/value_pace.h"

namespace {

constexpr quint16 FAKE_DEVICE_PORT = 1210;
constexpr quint16 FAKE_BUS_PORT = 1226; /* evre_fake_fast as several devices on one link, started by the bus step */
constexpr quint16 FAKE_AUTO_SEND_PORT = 1236; /* evre_fake_fast as one device that can AUTO_SEND, the auto send step */
constexpr quint16 FAKE_FAST_PORT = 1240;      /* evre_fake_fast with maps/example_fast.json: the fast streams step */
constexpr uint16_t PROTOCOL_CONFIG = 0xA004;  /* the EVRe protocol's CONFIG register */
constexpr int MSG_ENABLE_MASK = 0x4;           /* its MSG_ENABLE flag, bit 2 */
constexpr int DIALOG_WAIT_MS = 10000;         /* how long a step waits for the dialog it brings */
constexpr int UNANSWERED_DIALOG_MS = 15000;    /* a dialog open this long was answered by no step */

/* the token the fake device accepts at its login register, and one it refuses */
const QByteArray fakeDeviceToken("example-token");
const QByteArray wrongToken("not-the-token");
/* what the other client leaves in the login register before the window connects with a map without login */
const QByteArray noLoginMarker("no-login-marker");

/* what the window logs about the login */
const QLatin1String tokenRefusedText("token refused: permission denied");
const QLatin1String noLoginRegisterText("the map declares no login register: the token was not sent");

/* the titles of the window's two questions before a write */
const QLatin1String valueChangedTitle("Value changed while editing");
const QLatin1String confirmWriteTitle("Confirm write");

/* the math lines the test sets up: one over the map's registers, one naming
 * no register of the map (shown as an error, never drawn), and one saved
 * without its on flag, as an older entry is (shown) */
const QString powerLine = QStringLiteral("ƒ P");
const QString unresolvedLine = QStringLiteral("ƒ UNKNOWN");
const QString withoutOnFlagLine = QStringLiteral("ƒ OLD");

/* ---- results */

int passed = 0, failed = 0;

void check(bool ok, const char *what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	std::fflush(stdout);
	(ok ? passed : failed)++;
}

/* ---- Qt's warnings about objects used across threads: counted (they come
 * from any thread), then passed on to the handler before, so they still show */

std::atomic<int> threadWarnings{ 0 };
QtMessageHandler previousHandler = nullptr;

void countThreadWarnings(QtMsgType type, const QMessageLogContext &context, const QString &message) {
	/* parts of Qt's messages: "... from another thread" (timers, socket notifiers),
	 * "... in a different thread" (children, a new parent, stopping a timer),
	 * "... is not the object's thread" (moveToThread), and the rest by name */
	static const char *const phrases[] = {
		"another thread",
		"different thread",
		"object's thread",
		"Cannot move objects with a parent",
		"only be used with threads started with QThread",
		"QThread: Destroyed while thread is still running",
	};
	for (const char *phrase : phrases) {
		if (message.contains(QLatin1String(phrase))) {
			threadWarnings++;
			break;
		}
	}
	if (previousHandler) previousHandler(type, context, message);
}

/* ---- the other client: plain blocking EVRe over its own socket */

class OtherClient {
public:
	/* port: the fake device's; slave: the device on that link the requests go to */
	bool open(quint16 port = FAKE_DEVICE_PORT, uint8_t slave = 1) {
		slave_ = slave;
		socket_.connectToHost(QStringLiteral("127.0.0.1"), port);
		return socket_.waitForConnected(2000);
	}
	QByteArray read(uint16_t addr, int count) { /* empty: no answer */
		QByteArray data;
		return transact(evre::READ, addr, {}, uint16_t(count), &data) ? data : QByteArray();
	}
	bool write(uint16_t addr, const QByteArray &data) {
		return transact(evre::WRITE_ACK, addr, data, uint16_t(data.size()), nullptr);
	}
	int readU8(uint16_t addr) { /* -1: no answer */
		QByteArray data;
		return transact(evre::READ, addr, {}, 1, &data) && data.size() == 1 ? uint8_t(data[0]) : -1;
	}
	int readI16(uint16_t addr) { /* -99999: no answer */
		QByteArray data;
		if (!transact(evre::READ, addr, {}, 2, &data) || data.size() != 2) return -99999;
		return int16_t(uint8_t(data[0]) | (uint8_t(data[1]) << 8));
	}
	bool writeU8(uint16_t addr, uint8_t value) {
		return transact(evre::WRITE_ACK, addr, QByteArray(1, char(value)), 1, nullptr);
	}
	bool writeI16(uint16_t addr, int16_t value) {
		QByteArray data;
		data.append(char(value & 0xFF));
		data.append(char((value >> 8) & 0xFF));
		return transact(evre::WRITE_ACK, addr, data, 2, nullptr);
	}

private:
	/* one request and its answer (waited for up to 5 s): false on an error answer or none */
	bool transact(uint8_t fn, uint16_t addr, const QByteArray &data, uint16_t count, QByteArray *answer) {
		socket_.write(evre::build(slave_, fn, addr, count, data));
		socket_.flush();
		evre::Frame frame;
		for (int i = 0; i < 50; i++) {
			if (!socket_.waitForReadyRead(100)) continue;
			parser_.feed(socket_.readAll());
			while (parser_.next(frame)) {
				if (frame.addr != addr || frame.slave != slave_) continue;
				if (answer) *answer = frame.data;
				return frame.fn != evre::ERROR_RESP;
			}
		}
		return false;
	}

	QTcpSocket socket_;
	evre::Parser parser_;
	uint8_t slave_ = 1;
};

/* ---- the registers the test uses, found in the map */

struct TestRegisters {
	RegDef u8;      /* the first writable u8 of the device bank, not danger */
	RegDef enumU8;  /* the same with named values, for the quick write's list */
	RegDef danger;  /* the first writable 16-bit danger register of the device bank */
	RegDef volts;   /* the first read-only f32 in V and in A (the fake device moves them) */
	RegDef amps;

	/* a register not found keeps an empty name */
	static TestRegisters find(const DeviceMap &map) {
		TestRegisters found;
		const auto keepFirst = [](RegDef &slot, const RegDef &def, bool matches) {
			if (matches && slot.name.isEmpty()) slot = def;
		};
		for (const RegDef &def : map.regs) {
			const bool writableDevice = def.addr >= 0xD000 && def.rw;
			const bool sixteenBits = def.type == RegType::I16 || def.type == RegType::U16;
			const bool readOnlyFloat = !def.rw && def.type == RegType::F32;
			const bool plainU8 = writableDevice && !def.danger && def.type == RegType::U8;
			keepFirst(found.u8, def, plainU8);
			keepFirst(found.enumU8, def, plainU8 && !def.enumValues.isEmpty());
			keepFirst(found.danger, def, writableDevice && def.danger && sixteenBits);
			keepFirst(found.volts, def, readOnlyFloat && def.unit == QLatin1String("V"));
			keepFirst(found.amps, def, readOnlyFloat && def.unit == QLatin1String("A"));
		}
		return found;
	}
};

/* The test's own settings: the math lines (P = V x I over the map's
 * registers, the unresolved one, and OLD = 2 V in three fields) and a known
 * chart memory and view. */
void prepareSettings(const TestRegisters &regs) {
	QSettings settings;
	settings.setValue(QStringLiteral("chart/math"),
			QStringList{ QStringLiteral("P\tW\t%1 * %2\t1").arg(regs.volts.name, regs.amps.name),
					QStringLiteral("UNKNOWN\tW\tNO_SUCH_REGISTER * 2\t1"),
					QStringLiteral("OLD\tV\t%1 * 2").arg(regs.volts.name) });
	settings.setValue(QStringLiteral("chart/memory"), 60.0);
	settings.setValue(QStringLiteral("chart/window"), 30.0);
}

/* a token as the login register receives it: cut or zero-padded to its size (STUDIO.md section 3.6) */
QByteArray loginBytes(const QByteArray &token, int size) {
	QByteArray bytes = token.left(size);
	bytes.append(QByteArray(size - int(bytes.size()), '\0'));
	return bytes;
}

/* ---- helpers for the window */

/* the value cell of a register, by name */
QModelIndex valueCell(QTableView *table, const QString &name) {
	QAbstractItemModel *model = table->model();
	for (int r = 0; r < model->rowCount(); r++)
		if (model->index(r, RegisterModel::ColName).data().toString() == name)
			return model->index(r, RegisterModel::ColValue);
	return {};
}

/* opens a cell's editor, as a double-click does: nullptr if none came */
QLineEdit *openEditor(QTableView *table, const QModelIndex &cell) {
	table->scrollTo(cell);
	table->setCurrentIndex(cell);
	table->edit(cell);
	QLineEdit *editor = nullptr;
	(void) QTest::qWaitFor([&] {
		for (QLineEdit *line : table->viewport()->findChildren<QLineEdit *>()) {
			if (line->isVisible()) {
				editor = line;
				return true;
			}
		}
		return false;
	}, 2000);
	return editor;
}

/* while a step waits for a dialog (answerDialog, fillDialog): the guard in main() says whether one was waiting */
struct AwaitingDialog {
	static inline int count = 0;
	AwaitingDialog() { count++; }
	~AwaitingDialog() { count--; }
	AwaitingDialog(const AwaitingDialog &) = delete;
	AwaitingDialog &operator=(const AwaitingDialog &) = delete;
};

/* runs trigger, then answers the modal message box it brings by clicking
 * buttonText (or rejects it, without that button): the box's title, or empty if none came.
 * The poll timer is local, so it stops when this returns: a poll left running
 * after a call that brought no box would answer the next call's box itself. */
QString answerDialog(const QString &buttonText, const std::function<void()> &trigger) {
	QString title;
	bool done = false;
	const QPointer<QWidget> wasActive = QApplication::activeWindow();
	const AwaitingDialog awaiting;
	QTimer poll;
	poll.setInterval(50);
	QObject::connect(&poll, &QTimer::timeout, &poll, [&] {
		auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
		if (!box) return;
		poll.stop();
		title = box->windowTitle();
		done = true;
		for (QAbstractButton *button : box->buttons()) {
			if (button->text().remove(QLatin1Char('&')) == buttonText) {
				button->click();
				return;
			}
		}
		box->reject();
	});
	poll.start();
	trigger();
	/* long enough for a busy machine (it returns as soon as the box is answered): a box that came after this
	 * stopped waiting stayed on the screen for a person */
	(void) QTest::qWaitFor([&] { return done; }, DIALOG_WAIT_MS);
	/* the window active again before the next step: on Windows it comes back a little after the box closes, and an
	 * editor opened meanwhile lost its focus then, was taken as typed (Qt commits an editor on focus out) and
	 * brought its question while no step waited for it (xvfb has no active window: nothing to wait for there) */
	if (done && wasActive) (void) QTest::qWaitFor([&] { return QApplication::activeWindow() == wasActive; }, 2000);
	return title;
}

/* runs trigger, and fill(dialog) on the modal dialog it brings (not a message box): fill accepts or rejects it.
 * True when a dialog came. */
bool fillDialog(const std::function<void(QDialog *)> &fill, const std::function<void()> &trigger) {
	bool done = false;
	const QPointer<QWidget> wasActive = QApplication::activeWindow();
	const AwaitingDialog awaiting;
	QTimer poll;
	poll.setInterval(50);
	QObject::connect(&poll, &QTimer::timeout, &poll, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!dialog || qobject_cast<QMessageBox *>(dialog)) return;
		poll.stop();
		done = true;
		fill(dialog);
		if (dialog->isVisible()) dialog->reject(); /* never left open */
	});
	poll.start();
	trigger();
	(void) QTest::qWaitFor([&] { return done; }, DIALOG_WAIT_MS);
	if (done && wasActive) (void) QTest::qWaitFor([&] { return QApplication::activeWindow() == wasActive; }, 2000);
	return done;
}

/* Enter in an editor, answering the message box it may bring with button */
QString enterAnswering(QLineEdit *editor, const QString &button) {
	return answerDialog(button, [editor] {
		if (editor) QTest::keyClick(editor, Qt::Key_Return);
	});
}

/* waits (up to ms) until a cell shows text */
bool cellShows(const QModelIndex &cell, const QString &text, int ms = 3000) {
	return QTest::qWaitFor([&] { return cell.data().toString() == text; }, ms);
}

/* a group check box's text is the group's name with each & doubled (a single & is a mnemonic) */
QString groupName(const QCheckBox *box) {
	return box->text().replace(QStringLiteral("&&"), QStringLiteral("&"));
}

/* and back: a name as a check box or a button shows it */
QString doubleAmpersands(QString text) {
	return text.replace(QStringLiteral("&"), QStringLiteral("&&"));
}

/* the chart's key of the line with this name, -1 if none is drawn */
int lineKey(const ChartView *view, const QString &name) {
	for (const ChartView::Info &line : view->lines())
		if (line.name == name) return line.key;
	return -1;
}

/* the P line's row of the measurements table has its area in J and in Wh */
bool powerAreaInJoulesAndWattHours(const QTableWidget *table) {
	bool found = false;
	for (int r = 0; r < table->rowCount(); r++) {
		const QTableWidgetItem *name = table->item(r, 0), *area = table->item(r, ChartTab::ColArea),
				*areaHours = table->item(r, ChartTab::ColAreaHours);
		if (name && name->text() == powerLine)
			found = area && areaHours && area->text().endsWith(QLatin1String(" J"))
					&& areaHours->text().endsWith(QLatin1String(" Wh"));
	}
	return found;
}

/* A link in memory, for the master's own checks: it keeps what is sent and plays back the answers given. */
class LoopLink : public evre::Link {
public:
	QList<evre::Frame> sent;
	void open() override { emit opened(); }
	void close() override {}
	bool isOpen() const override { return true; }
	void send(const QByteArray &bytes) override {
		evre::Parser parser;
		parser.feed(bytes);
		evre::Frame frame;
		while (parser.next(frame)) sent << frame;
	}
	QString describe() const override { return QStringLiteral("loop"); }
	void answer(uint8_t slave, uint8_t fn, uint16_t addr, uint16_t cnt, const QByteArray &data = {}) {
		emit received(evre::build(slave, fn, addr, cnt, data));
	}
};

/* ---- the steps */

/* the widgets of the quick write panel under the table */
struct QuickWriteWidgets {
	QFrame *frame = nullptr;
	QLineEdit *value = nullptr;
	QComboBox *enumList = nullptr;
	QCheckBox *bits = nullptr;

	bool complete() const { return frame && value && enumList && bits; }
	BitView *bitView() const { return frame->findChild<BitView *>(QStringLiteral("bitView")); }
	/* a label of the panel shows this text (its hint: "not connected", "tick Allow writes to write") */
	bool shows(const QString &text) const {
		const QList<QLabel *> labels = frame->findChildren<QLabel *>();
		return std::any_of(labels.begin(), labels.end(), [&](const QLabel *label) { return label->text() == text; });
	}
};

class GuiTest {
public:
	GuiTest(MainWindow &window, OtherClient &other, const DeviceMap &map, const QString &mapName)
		: window_(window), other_(other), map_(map), regs_(TestRegisters::find(map)), mapName_(mapName) {}

	/* false: the window or the map lacks what the steps need, nothing more was tried */
	bool run() {
		if (!findWidgets() || !findRegisters()) return false;
		/* before anything ticks it: Allow writes already as wide as in bold (it moved the bar at its first tick) */
		{
			const int unticked = allowWrites_->width();
			allowWrites_->setChecked(true);
			QApplication::processEvents();
			const int ticked = allowWrites_->width();
			allowWrites_->setChecked(false);
			QApplication::processEvents();
			check(unticked == ticked && allowWrites_->property("highlightWidth").isValid(),
					"Allow writes: its bold width from the start, so even the first tick moves nothing");
		}
		studioIcon();
		masterSlaves();
		connectedAndPolling();
		tokenSentAfterConnecting();
		writeSwitch();
		plainWrite();
		otherClientWrites();
		changeWhileEditing();
		unchangedMeanwhile();
		dangerRegister();
		badValueRefused();
		noticeCoversNothing();
		showInLogEndsNotice();
		noticeRightToLeft();
		eventLog();
		staleValues();
		decodedFields();
		quickWrite();
		groups();
		chart();
		chartManyLines();
		chartBinsAndGpu();
		chartFastLines();
		chartFastMeasure();
		chartFastMath();
		readoutSteady();
		displayMenu();
		helpPages();
		languages();
		measuresManyLines();
		chartFollowsFrames();
		cursorSpanBar();
		chartReadouts();
		chartTotals();
		chartLogScale();
		chartInfoLine();
		chartOneCap();
		chartRamCut();
		chartRamFree();
		chartFastTiers();
		memoryStripHandle();
		recordingFiles();
		chartMenuAndPictures();
		chartExport();
		chartNotes();
		chartLanes();
		chartLanesFit();
		chartLanesFoldButton();
		chartLanesSeparators();
		chartLaneBorders();
		chartLaneRanges();
		chartStateFits();
		heldViewReuse();
		measureTableRepaints();
		measureInBackground();
		perfLog();
		analysisMath();
		analysisWindows();
		chartTrigger();
		chartTriggerLines();
		chartTriggerModes();
		chartTriggerSteady();
		chartTriggerFast();
		chartTriggerAuto();
		chartTriggerRunStop();
		chartTriggerReview();
		chartTriggerLook();
		chartTriggerFlow();
		chartTriggerScopes();
		chartTriggerMarksOutside();
		chartTriggerSteadyState();
		chartShortLock();
		chartShortLockBusiest();
		chartTimeGrid();
		chartTimesFromT();
		frameBudget();
		plotShownWithoutQuestion();
		recordingWindows();
		recentMissing();
		mapEditor();
		mapStreamsPage();
		limitsAndFields();
		pollingSurvivesEdits();
		uiAudit();
		hoverEdges();
		monitorRequests();
		formulaCompletion();
		autoSend();   /* it ends with Auto send off, connected to the fake device as before */
		busDevices(); /* it ends with the window on one device again, connected to the fake device as before */
		fastStreams(); /* it ends with the window on the example map again, connected to the fake device as before */
		fastSpeed();   /* the same */
		other_.writeI16(regs_.danger.addr, 0);
		wrongTokenRefused();
		tokenWithoutLoginRegister(); /* the last step: the window keeps a map without the login */
		return true;
	}

private:
	bool findWidgets() {
		table_ = window_.findChild<QTableView *>(QStringLiteral("registers"));
		for (QCheckBox *box : window_.findChildren<QCheckBox *>()) {
			if (box->text() == QLatin1String("Allow writes")) allowWrites_ = box;
			if (box->text() == QLatin1String("Poll")) poll_ = box;
		}
		model_ = window_.findChild<RegisterModel *>();
		const bool found = table_ && allowWrites_ && poll_ && model_;
		check(found, "window built: table, Allow writes, Poll");
		/* the style sheet styles the combo boxes' drop-down: their arrow is an image the theme draws */
		const QRegularExpressionMatch arrow =
				QRegularExpression(QStringLiteral("QComboBox::down-arrow \\{ image: url\\(\"([^\"]+)\"\\)"))
						.match(qApp->styleSheet());
		check(arrow.hasMatch() && QImage(arrow.captured(1)).size() == QSize(40, 24),
				"combo boxes have a drop-down arrow: the theme's image of it exists");
		/* every button with a menu: the same chevron (not Fusion's triangle) */
		const bool menuArrow = arrow.hasMatch()
				&& qApp->styleSheet().contains(
						QStringLiteral("QPushButton::menu-indicator { image: url(\"%1\")").arg(arrow.captured(1)));
		QStringList plainMenuButtons;
		for (QPushButton *button : window_.findChildren<QPushButton *>())
			if (button->menu() && !button->property("menuButton").toBool()) plainMenuButtons << button->text();
		if (!plainMenuButtons.isEmpty())
			std::printf("menu buttons without the chevron: %s\n", qPrintable(plainMenuButtons.join(QStringLiteral(", "))));
		check(menuArrow && plainMenuButtons.isEmpty(), "menu buttons (Math, Export, groups) have the combo boxes' chevron");
		return found;
	}

	bool findRegisters() {
		std::printf("map %s: u8 register %s (0x%04X), with names %s, danger register %s (0x%04X), math line %s * %s,"
				" login at 0x%04X\n",
				qPrintable(mapName_), qPrintable(regs_.u8.name), regs_.u8.addr, qPrintable(regs_.enumU8.name),
				qPrintable(regs_.danger.name), regs_.danger.addr, qPrintable(regs_.volts.name),
				qPrintable(regs_.amps.name), map_.loginAddr);
		const bool found = !regs_.u8.name.isEmpty() && !regs_.danger.name.isEmpty();
		check(found, "the map has a writable u8 and a writable danger register");
		/* the registers were found in the file: the window must have loaded the same map */
		const bool loaded = model_->rows().size() == map_.regs.size() && inWindowsMap(regs_.u8.name)
				&& inWindowsMap(regs_.danger.name);
		check(loaded, "the window loaded the map: as many registers as the file, those above among them");
		return found && loaded;
	}

	bool inWindowsMap(const QString &name) const {
		const QVector<RegisterModel::Row> &rows = model_->rows();
		return std::any_of(rows.begin(), rows.end(), [&](const RegisterModel::Row &row) { return row.def.name == name; });
	}

	/* the Log tab's text, empty if there is no Log */
	QString logText() const {
		auto *log = window_.findChild<QPlainTextEdit *>(QStringLiteral("eventLog"));
		return log ? log->toPlainText() : QString();
	}

	/* the login register as the device holds it: the last token written to it (the fake device keeps it) */
	QByteArray deviceLogin() { return other_.read(map_.loginAddr, map_.loginSize); }

	/* the window started with EVRE_TOKEN set: the token went to the map's login register, and was accepted */
	void tokenSentAfterConnecting() {
		const bool declared = map_.loginAddr != 0;
		const QByteArray want = loginBytes(fakeDeviceToken, map_.loginSize);
		const bool arrived = declared && QTest::qWaitFor([&] { return deviceLogin() == want; }, 3000);
		const QString log = logText();
		check(arrived && !log.contains(QLatin1String("token refused")) && !log.contains(noLoginRegisterText),
				"a token and a map with a login: sent after connecting, zero-padded; the device holds it, accepted");
	}

	/* waits until the device holds value in the u8 / the danger register */
	bool u8Becomes(int value) {
		return QTest::qWaitFor([&] { return other_.readU8(regs_.u8.addr) == value; }, 3000);
	}
	bool dangerBecomes(int value) {
		return QTest::qWaitFor([&] { return other_.readI16(regs_.danger.addr) == value; }, 3000);
	}

	/* the window's button with this text, nullptr if there is none */
	QPushButton *buttonWithText(const QString &text) const { return buttonWithText(window_, text); }
	static QPushButton *buttonWithText(const QWidget &in, const QString &text) {
		for (QPushButton *button : in.findChildren<QPushButton *>())
			if (button->text() == text) return button;
		return nullptr;
	}

	/* opens a cell's editor and types text over what it holds: nullptr if no editor came */
	QLineEdit *typeInto(const QModelIndex &cell, const QString &text) {
		QLineEdit *editor = openEditor(table_, cell);
		if (editor) {
			editor->selectAll();
			QTest::keyClicks(editor, text);
		}
		return editor;
	}

	/* the device starts from 0; the window, connected at startup, shows it */
	void connectedAndPolling() {
		other_.writeU8(regs_.u8.addr, 0);
		other_.writeI16(regs_.danger.addr, 0);
		u8Cell_ = valueCell(table_, regs_.u8.name);
		check(cellShows(u8Cell_, QStringLiteral("0"), 5000), "connected and polling: the u8 register shows 0");
	}

	/* 1. writes are off until Allow writes is ticked */
	void writeSwitch() {
		check(!(u8Cell_.flags() & Qt::ItemIsEditable), "writes off: a RW value cannot be edited");
		allowWrites_->setChecked(true);
		check(bool(u8Cell_.flags() & Qt::ItemIsEditable), "Allow writes on: it can");
	}

	/* 2. a plain write, read back */
	void plainWrite() {
		QLineEdit *editor = typeInto(u8Cell_, QStringLiteral("3"));
		check(editor != nullptr, "double-click opens an editor");
		if (editor) QTest::keyClick(editor, Qt::Key_Return);
		check(u8Becomes(3), "u8 register: 3 written: the device holds 3");
		check(cellShows(u8Cell_, QStringLiteral("3")), "... and the table shows 3 (read back)");
	}

	/* 3. someone else writes: the table follows the device */
	void otherClientWrites() {
		other_.writeU8(regs_.u8.addr, 1);
		check(cellShows(u8Cell_, QStringLiteral("1")), "another client writes 1: the table shows 1 on the next poll");
	}

	/* 4. a change while editing: the typed text survives the refresh, the write asks first. The
	 * editor is held by a QPointer: should it close meanwhile (focus taken by another window),
	 * the checks fail instead of reading a deleted widget */
	void changeWhileEditing() {
		QPointer<QLineEdit> editor = typeInto(u8Cell_, QStringLiteral("2"));
		other_.writeU8(regs_.u8.addr, 4);
		QTest::qWait(600); /* several polls while the editor is open */
		check(editor && editor->text() == QLatin1String("2"), "polls while editing do not overwrite the typed text");
		QString title = enterAnswering(editor, QStringLiteral("Cancel"));
		check(title == valueChangedTitle, "Enter after the device changed: \"Value changed while editing\" asked");
		check(other_.readU8(regs_.u8.addr) == 4, "Cancel: nothing written, the device keeps the other client's 4");

		/* answered from the typing on: had the editor lost its focus meanwhile (another window), its question would
		 * come before Enter, and still be answered here, not left on the screen */
		title = answerDialog(QStringLiteral("Write anyway"), [&] {
			editor = typeInto(u8Cell_, QStringLiteral("2"));
			other_.writeU8(regs_.u8.addr, 1);
			QTest::qWait(400);
			if (editor) QTest::keyClick(editor, Qt::Key_Return);
		});
		check(title == valueChangedTitle && u8Becomes(2), "Write anyway: the device holds 2");
	}

	/* 5. no question when nothing changed meanwhile. The table first shows
	 * what the device holds (the I/O thread's values arrive within a frame). */
	void unchangedMeanwhile() {
		(void) cellShows(u8Cell_, QStringLiteral("2"));
		QLineEdit *editor = typeInto(u8Cell_, QStringLiteral("0"));
		const QString title = enterAnswering(editor, QStringLiteral("Cancel"));
		check(title.isEmpty() && u8Becomes(0), "unchanged meanwhile: written without a dialog");
	}

	/* 6. a register marked danger asks every time */
	void dangerRegister() {
		const QModelIndex cell = valueCell(table_, regs_.danger.name);
		QString title = enterAnswering(typeInto(cell, QStringLiteral("50")), QStringLiteral("Cancel"));
		check(title == confirmWriteTitle && other_.readI16(regs_.danger.addr) == 0,
				"the danger register: confirm asked, Cancel writes nothing");
		title = enterAnswering(typeInto(cell, QStringLiteral("50")), QStringLiteral("Write"));
		check(title == confirmWriteTitle && dangerBecomes(50), "... Write: the device holds 50");
	}

	/* 7. a bad value is refused before anything is sent */
	void badValueRefused() {
		QLineEdit *editor = typeInto(u8Cell_, QStringLiteral("300"));
		if (editor) QTest::keyClick(editor, Qt::Key_Return);
		check(QTest::qWaitFor([&] { return noticeShown("not written"); }, 2000) && other_.readU8(regs_.u8.addr) == 0,
				"300 into a u8: a pop-up \"not written\", nothing sent");
	}

	/* a pop-up notice is shown whose tooltip holds text */
	bool noticeShown(const char *text) const {
		for (QLabel *label : window_.findChildren<QLabel *>(QStringLiteral("notice")))
			if (label->isVisible() && label->toolTip().contains(QLatin1String(text))) return true;
		return false;
	}

	/* the pop-up covers nothing: not the tabs, not any tab's page, not the sidebar; on every tab and size */
	void noticeCoversNothing() {
		auto *tabs = window_.findChild<QTabWidget *>();
		auto *notice = window_.findChild<QLabel *>(QStringLiteral("notice"));
		bool clear = tabs && notice;
		const QSize was = window_.size();
		for (const QSize &size : { QSize(1200, 720), QSize(1600, 950), was }) {
			window_.resize(size);
			for (int tab = 0; clear && tab < tabs->count(); tab++) {
				tabs->setCurrentIndex(tab);
				QApplication::processEvents();
				if (!notice->isVisible() || !noticeOverlaps(tabs, notice)) continue;
				const QRect n = inWindow(notice);
				std::printf("  notice %d,%d %dx%d overlaps (tab %d, window %dx%d)\n", n.x(), n.y(), n.width(),
						n.height(), tab, size.width(), size.height());
				clear = false;
			}
		}
		if (tabs) tabs->setCurrentIndex(0);
		check(clear, "the pop-up covers nothing: tabs, pages, sidebar, at 1200x720 and 1600x950, every tab");
	}

	/* the notice over the current page, left of the last tab, or over the sidebar */
	bool noticeOverlaps(const QTabWidget *tabs, const QLabel *notice) const {
		const QRect noticeRect = inWindow(notice);
		const QTabBar *bar = tabs->tabBar();
		const QRect lastTab = bar->tabRect(tabs->count() - 1).translated(bar->mapTo(&window_, QPoint(0, 0)));
		const auto *sidebar = window_.findChild<QScrollArea *>(QStringLiteral("sideScroll"));
		return noticeRect.intersects(inWindow(tabs->currentWidget())) || noticeRect.left() <= lastTab.right()
				|| (sidebar && noticeRect.intersects(inWindow(sidebar)));
	}

	/* "Show in Log" opens the Log tab and ends the notice: a resize of the window does not bring it back */
	void showInLogEndsNotice() {
		auto *tabs = window_.findChild<QTabWidget *>();
		auto *notice = window_.findChild<QLabel *>(QStringLiteral("notice"));
		QLineEdit *editor = typeInto(u8Cell_, QStringLiteral("abc"));
		if (editor) QTest::keyClick(editor, Qt::Key_Return);
		const bool shown = QTest::qWaitFor([&] { return noticeShown("not a number"); }, 2000);
		if (notice) emit notice->linkActivated(QStringLiteral("log"));
		const bool logOpened = tabs && tabs->currentIndex() == MainWindow::TabLog;
		const QSize was = window_.size();
		for (const QSize &size : { was + QSize(60, 0), was }) {
			window_.resize(size);
			QTest::qWait(200);
		}
		check(shown && logOpened && notice && !notice->isVisible(),
				"Show in Log: the Log tab, and the notice stays gone after a resize");
		if (tabs) tabs->setCurrentIndex(0);
	}

	/* The notice in right-to-left (O-12): the tabs sit on the right there and the free room of their row is on their
	 * left; the notice goes there, on Arabic's direction change and at every size, and back right of the tabs in left to
	 * right. Its rectangle never meets the tab bar, the page or the sidebar, and stays in the window */
	void noticeRightToLeft() {
		auto *tabs = window_.findChild<QTabWidget *>();
		auto *notice = window_.findChild<Notice *>(QStringLiteral("notice"));
		const auto *sidebar = window_.findChild<QScrollArea *>(QStringLiteral("sideScroll"));
		if (!tabs || !notice) {
			check(false, "the notice in right-to-left: the tabs and the notice");
			return;
		}
		const QSize was = window_.size();
		const auto clear = [&](const char *where) {
			QApplication::processEvents();
			const QRect n = inWindow(notice);
			const QTabBar *bar = tabs->tabBar();
			const QRect barRect(bar->mapTo(&window_, QPoint(0, 0)), bar->size());
			const bool rtl = tabs->layoutDirection() == Qt::RightToLeft;
			const bool ok = notice->isVisible() && !n.intersects(barRect) && !n.intersects(inWindow(tabs->currentWidget()))
					&& !(sidebar && n.intersects(inWindow(sidebar))) && window_.rect().contains(n)
					&& (rtl ? n.right() < barRect.left() : n.left() > barRect.right());
			if (!ok)
				std::printf("     (%s: the notice %d,%d %dx%d shown %d, the tab bar %d,%d %dx%d, the window %dx%d)\n", where, n.x(),
						n.y(), n.width(), n.height(), int(notice->isVisible()), barRect.x(), barRect.y(), barRect.width(),
						barRect.height(), window_.width(), window_.height());
			return ok;
		};
		tabs->setCurrentIndex(0);
		notice->post(LogLevel::Error, QStringLiteral("a notice in right-to-left"));
		bool ok = clear("left to right");
		language::apply(*qApp, QStringLiteral("ar"));
		QTest::qWait(200);
		ok = clear("right to left") && ok;
		for (const QSize &size : { QSize(1200, 720), QSize(1600, 950), was }) {
			window_.resize(size);
			QTest::qWait(100);
			ok = clear("right to left, resized") && ok;
		}
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) /* for a look: the notice left of the tabs */
			window_.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_notice_ar.png"));
		language::apply(*qApp, QStringLiteral("en"));
		QTest::qWait(200);
		ok = clear("left to right again") && ok;
		emit notice->linkActivated(QStringLiteral("log")); /* gone, for the steps after */
		tabs->setCurrentIndex(0);
		check(ok, "the notice in right-to-left (Arabic): left of the tabs, where their row is free, at every size; right of "
				"them again in left to right; never over the tab bar, the page or the sidebar");
	}

	/* a widget's rectangle in the window's coordinates */
	QRect inWindow(const QWidget *widget) const {
		return QRect(widget->mapTo(&window_, QPoint(0, 0)), widget->size());
	}

	void eventLog() {
		const QString text = logText();
		check(text.contains(QLatin1String("not written")) && text.contains(QLatin1String("written: ")),
				"the Log tab: the writes, and the one not written");
	}

	/* 8. stale: polling paused, values go grey */
	void staleValues() {
		check(!u8Greyed(), "polling: not greyed");
		poll_->setChecked(false);
		check(QTest::qWaitFor([&] { return u8Greyed(); }, 3000), "Poll off: greyed within two intervals");
		poll_->setChecked(true);
		check(QTest::qWaitFor([&] { return !u8Greyed(); }, 3000), "Poll on again: not greyed");
	}

	/* WCAG 2.1: the contrast of two colours, 1 to 21 */
	static double contrast(const QColor &a, const QColor &b) {
		auto luminance = [](const QColor &c) {
			auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
			return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
		};
		const double la = luminance(a), lb = luminance(b);
		return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
	}

	/* AUTO_SEND (one device): evre_fake_fast on a port of its own sends its read-only block by itself once CONFIG asks.
	 * The window writes CONFIG (bit 3, the prescaler), takes the frames (table, rate, CSV rows), polls only the rest,
	 * reads CONFIG every 100 ms even with Poll off, clears it on Disconnect and switches it on again after a lost
	 * link; a device that clears it by itself is told of once. A serial link's rate is cut to what it can carry. */
	void autoSend() {
		/* the serial cap: 32-byte frames (22 data); 70% of the bytes a 10-bit UART byte allows */
		check(IoEngine::autoSendPrescalerFor(79, 115200, 22) == 79 && IoEngine::autoSendPrescalerFor(1, 115200, 22) == 31
						&& IoEngine::autoSendPrescalerFor(79, 9600, 22) == 199 && IoEngine::autoSendPrescalerFor(1, 0, 22) == 1,
				"auto send: a serial link keeps rate x frame <= 70% of baud/10 (4000 Hz at 115200 -> 250 Hz, 40 Hz the "
				"least), TCP as asked");

		auto *sidebar = window_.findChild<Sidebar *>();
		auto *box = window_.findChild<QCheckBox *>(QStringLiteral("autoSend"));
		QCheckBox *reconnectBox = nullptr;
		for (QCheckBox *b : window_.findChildren<QCheckBox *>())
			if (b->text() == QLatin1String("Reconnect by itself")) reconnectBox = b;
		auto *rate = window_.findChild<QComboBox *>(QStringLiteral("autoSendRate"));
		const bool found = sidebar && box && rate && reconnectBox && rate->count() == 16
				&& rate->itemData(0).toInt() == 4000 && rate->itemData(15).toInt() == 40 && rate->findData(100) >= 0;
		check(found, "auto send: the box and the 16 rates the device makes exactly (4000 Hz .. 40 Hz: the prescaler is the "
				"device timer's reload, never 0) in the polling card");
		if (!found) return;
		/* the sidebar's rate line: one line, as wide as it gets, and no slower-than-asked hint while the frames run */
		QLabel *rateLine = nullptr;
		for (QLabel *label : sidebar->findChildren<QLabel *>())
			if (label->text().contains(QLatin1String("polls/s"))) rateLine = label;
		IoEngine::Stats slow;
		slow.blocks = 1;
		slow.pollHz = 0.5;
		slow.master.avgLatencyMs = 20000; /* one answer in 20 s: a hint, polling everything */
		sidebar->showStats(slow, true);
		const bool hintPolling = !sidebar->slowPollHint().isEmpty();
		slow.autoSend = true;
		slow.autoSendHz = 3999.9;
		slow.pollHz = 3999.9;
		sidebar->showStats(slow, true);
		const bool oneLine = rateLine && rateLine->text().startsWith(QStringLiteral("Auto send 4000/s · 4000 polls/s"))
				&& rateLine->heightForWidth(rateLine->width()) < 2 * rateLine->fontMetrics().lineSpacing();
		if (rateLine && !oneLine)
			std::printf("  the rate line \"%s\": %d px of text in %d px\n", qPrintable(rateLine->text()),
					rateLine->fontMetrics().horizontalAdvance(rateLine->text()), rateLine->width());
		check(hintPolling && sidebar->slowPollHint().isEmpty() && oneLine,
				"auto send: the rate line says frames and polls a second on one line; no \"slower than asked\" hint meanwhile");
		sidebar->showStats(IoEngine::Stats(), true);

		/* a device that takes AUTO_SEND but whose frames never come (a gateway answering from its own copy of the device):
		 * off again after 2 s, said in the Log, the registers polled. The Python device takes it and sends nothing. */
		const bool sendOffered = box->isEnabled();
		if (sendOffered) box->setChecked(true);
		const bool backOff = sendOffered && QTest::qWaitFor([&] { return !box->isChecked(); }, 4000);
		check(backOff && logText().contains(QLatin1String("no frame came")) && box->isEnabled(),
				"auto send: no frame in 2 s (something between does not pass them on): unticked, said in the Log, polled");

		QProcess fake;
		auto startFake = [&] {
			fake.start(QCoreApplication::applicationDirPath() + QStringLiteral("/evre_fake_fast"),
					{ QString::number(FAKE_AUTO_SEND_PORT), map_.path, QString::fromLatin1(fakeDeviceToken) });
			return fake.waitForStarted(3000);
		};
		auto device = std::make_unique<OtherClient>();
		const bool started = startFake()
				&& QTest::qWaitFor([&] { return device->open(FAKE_AUTO_SEND_PORT, map_.slave); }, 5000);
		check(started, "auto send: the fake device (evre_fake_fast, one device) started");
		if (!started) return;
		auto config = [&] {
			const QByteArray bytes = device->read(PROTOCOL_CONFIG, 2);
			return bytes.size() == 2 ? int(uint8_t(bytes[0]) | (uint8_t(bytes[1]) << 8)) : -1;
		};
		auto configBecomes = [&](const std::function<bool(int)> &is, int ms = 3000) {
			return QTest::qWaitFor([&] {
				const int value = config();
				return value >= 0 && is(value);
			}, ms);
		};
		auto connectTo = [&](quint16 port) {
			MainWindow::Startup startup;
			startup.tcp = QStringLiteral("127.0.0.1:%1").arg(port);
			startup.connect = true;
			window_.applyStartup(startup);
		};
		reconnectBox->setChecked(true);
		connectTo(FAKE_AUTO_SEND_PORT);
		const bool offered = QTest::qWaitFor([&] { return box->isEnabled(); }, 5000);
		rate->setCurrentIndex(rate->findData(200));
		rate->setCurrentIndex(rate->findData(100)); /* a change: saved (from 100 already, nothing would be) */
		box->setChecked(true);
		check(offered && configBecomes([](int v) { return (v & 0x0008) && (v >> 8) == 79 && !(v & 0x0012); })
						&& QSettings().value(QStringLiteral("poll/autoSendHz")).toInt() == 100,
				"auto send on at 100 Hz: the device's CONFIG has AUTO_SEND (bit 3), prescaler 79, SYS_RESET and DFU 0");
		auto framesPerSecond = [&] {
			const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("^Auto send ([0-9.]+)/s"))
					.match(rateLine ? rateLine->text() : QString());
			return m.hasMatch() ? m.captured(1).toDouble() : -1.0;
		};
		const bool streaming = QTest::qWaitFor([&] {
			const double hz = framesPerSecond();
			return hz > 85 && hz < 115;
		}, 5000);
		if (!streaming) std::printf("  the rate line: %s\n", rateLine ? qPrintable(rateLine->text()) : "none");
		check(streaming, "auto send: the frames come at 100 per second (the sidebar's rate line, within 15%)");
		/* another rate while it is on: written to the device at once (200 Hz: prescaler 39), then back to 100 */
		rate->setCurrentIndex(rate->findData(200));
		const bool faster = configBecomes([](int v) { return (v & 0x0008) && (v >> 8) == 39; });
		rate->setCurrentIndex(rate->findData(100));
		check(faster && configBecomes([](int v) { return (v & 0x0008) && (v >> 8) == 79; }),
				"auto send: a rate chosen while it is on is written at once (200 Hz: prescaler 39), and back (100 Hz: 79)");

		/* Poll off: the frames still bring the read-only values, and CONFIG is still read 10 times a second */
		auto *traffic = [&]() -> QLabel * {
			for (QLabel *label : window_.statusBar()->findChildren<QLabel *>())
				if (label->text().startsWith(QLatin1String("TX "))) return label;
			return nullptr;
		}();
		auto sent = [&] {
			return traffic ? traffic->text().section(QLatin1Char(' '), 1, 1).toLongLong() : 0;
		};
		poll_->setChecked(false);
		QTest::qWait(600); /* the polls under way end; the status line follows */
		const QModelIndex volts = valueCell(table_, regs_.volts.name);
		const QString voltsBefore = volts.data().toString();
		const qint64 txBefore = sent();
		QTest::qWait(2000);
		const qint64 heartbeats = sent() - txBefore;
		const bool voltsMoved = volts.isValid() && volts.data().toString() != voltsBefore;
		std::printf("  Poll off for 2 s: %lld requests sent\n", heartbeats);
		check(traffic && heartbeats >= 16 && heartbeats <= 40 && voltsMoved,
				"auto send, Poll off: the read-only values still move (the frames); CONFIG read every 100 ms still");
		poll_->setChecked(true);

		/* the Monitor reads part of the block while it streams: its answer, not a frame of the stream */
		auto *monitor = window_.findChild<MonitorTab *>();
		auto *function = monitor ? monitor->findChild<QComboBox *>(QStringLiteral("monitorFunction")) : nullptr;
		QList<QLineEdit *> boxes = monitor ? monitor->findChildren<QLineEdit *>() : QList<QLineEdit *>();
		boxes.removeIf([](const QLineEdit *b) { return qobject_cast<QAbstractSpinBox *>(b->parent()) != nullptr; });
		QPushButton *send = monitor ? buttonWithText(*monitor, QStringLiteral("Send")) : nullptr;
		auto *frames = monitor ? monitor->findChild<QPlainTextEdit *>() : nullptr;
		bool fourBytes = false;
		if (function && boxes.size() >= 2 && send && frames) {
			function->setCurrentIndex(0);
			boxes[0]->setText(addrText(0xD000));
			boxes[1]->setText(QStringLiteral("4"));
			frames->clear();
			send->click();
			const QRegularExpression answer(QStringLiteral("== %1 OK: [0-9A-F]{2} [0-9A-F]{2} [0-9A-F]{2} [0-9A-F]{2}  \\(")
					.arg(addrText(0xD000)));
			fourBytes = QTest::qWaitFor([&] { return answer.match(frames->toPlainText()).hasMatch(); }, 3000);
			boxes[1]->setText(QStringLiteral("2"));
		}
		check(fourBytes, "auto send: a Monitor READ of 0xD000, 4 bytes, while it streams: answered with 4 bytes");

		/* one CSV row per frame, not per poll */
		QTemporaryDir folder;
		const QString csv = folder.filePath(QStringLiteral("stream.csv"));
		MainWindow::Startup record;
		record.record = csv;
		window_.applyStartup(record);
		QPushButton *stop = nullptr;
		(void) QTest::qWaitFor([&] { return (stop = buttonWithText(QStringLiteral("■  Stop recording"))) != nullptr; }, 3000);
		QElapsedTimer recorded;
		recorded.start();
		QTest::qWait(1500);
		if (stop) stop->click();
		const double seconds = double(recorded.elapsed()) / 1000.0;
		QFile file(csv);
		int rows = -1;
		if (QTest::qWaitFor([&] { return !buttonWithText(QStringLiteral("■  Stop recording")); }, 3000)
				&& file.open(QIODevice::ReadOnly))
			rows = int(file.readAll().count('\n')) - 1;
		std::printf("  %d CSV rows in %.1f s\n", rows, seconds);
		check(stop && rows > 60 * seconds && rows < 140 * seconds,
				"auto send: one CSV row per frame (about 100 a second, not the 10 polls a second)");

		/* off: cleared on the device; the polls read every register again */
		box->setChecked(false);
		const bool cleared = configBecomes([](int v) { return !(v & 0x0008); });
		check(cleared && QTest::qWaitFor([&] { return rateLine->text().contains(QLatin1String("registers in")); }, 3000),
				"auto send off: CONFIG's AUTO_SEND cleared on the device; the rate line shows the polls of every register");

		/* on again, then Disconnect: cleared before the link closes; the box unticked */
		box->setChecked(true);
		const bool onAgain = configBecomes([](int v) { return (v & 0x0008) != 0; });
		QPushButton *disconnectButton = buttonWithText(QStringLiteral("Disconnect"));
		if (disconnectButton) disconnectButton->click();
		check(onAgain && disconnectButton && configBecomes([](int v) { return !(v & 0x0008); }) && !box->isChecked()
						&& !box->isEnabled() && !box->toolTip().isEmpty() && !rate->isEnabled() && rate->currentIndex() < 0
						&& rate->placeholderText() == QLatin1String("not connected"),
				"auto send, then Disconnect: AUTO_SEND cleared on the device first; the box off, disabled, the rate list "
				"saying why (not connected)");
		/* each reason fits the greyed list whole: its text room is the width less the padding (2 x 8), the arrow (20) and
		 * the edges (2) */
		bool reasonsFit = rate != nullptr;
		for (const QString &reason : { QStringLiteral("not connected"), QStringLiteral("not offered"), QStringLiteral("not on a bus") })
			if (rate && rate->fontMetrics().horizontalAdvance(reason) > rate->width() - 38) reasonsFit = false;
		check(reasonsFit, "auto send: each reason the greyed rate list shows (not connected, not offered, not on a bus) "
				"fits it whole");

		/* the off before the close, again and again: closed with the device's frames unread, the TCP connection was reset
		 * and the device dropped the off it had not read yet (now and then a Disconnect left it sending) */
		int clearedEveryTime = 0;
		for (int round = 0; round < 6; round++) {
			connectTo(FAKE_AUTO_SEND_PORT);
			if (!QTest::qWaitFor([&] { return box->isEnabled(); }, 5000)) break;
			box->setChecked(true);
			if (!configBecomes([](int v) { return (v & 0x0008) != 0; })) break;
			QTest::qWait(50); /* frames under way */
			if (QPushButton *button = buttonWithText(QStringLiteral("Disconnect"))) button->click();
			if (!configBecomes([](int v) { return !(v & 0x0008); })) break;
			clearedEveryTime++;
		}
		if (clearedEveryTime < 6)
			std::printf("     (Disconnect with auto send on: cleared %d times in a row of 6)\n", clearedEveryTime);
		check(clearedEveryTime == 6, "auto send, then Disconnect, six times: AUTO_SEND cleared on the device every time "
				"(what the device still sends read before the close)");

		/* a lost link keeps it: the device comes back, auto send goes on again by itself */
		connectTo(FAKE_AUTO_SEND_PORT);
		(void) QTest::qWaitFor([&] { return box->isEnabled(); }, 5000);
		box->setChecked(true);
		const bool onBeforeLoss = configBecomes([](int v) { return (v & 0x0008) != 0; });
		fake.kill();
		fake.waitForFinished(3000);
		device = std::make_unique<OtherClient>();
		const bool back = startFake()
				&& QTest::qWaitFor([&] { return device->open(FAKE_AUTO_SEND_PORT, map_.slave); }, 5000);
		check(onBeforeLoss && back && configBecomes([](int v) { return (v & 0x0008) && (v >> 8) == 79; }, 10000)
						&& box->isChecked(),
				"auto send: after a lost link and a reconnect, switched on again by itself");

		/* the device clears it by itself (a reset): said once, the box unticked, not switched on again */
		(void) QTest::qWaitFor([&] { return box->isEnabled(); }, 3000);
		device->write(PROTOCOL_CONFIG, QByteArray(2, '\0'));
		const QLatin1String stoppedText("the device stopped auto send (reset?)");
		const bool told = QTest::qWaitFor([&] { return logText().contains(stoppedText); }, 3000);
		QTest::qWait(500);
		check(told && logText().count(stoppedText) == 1 && !box->isChecked()
						&& configBecomes([](int v) { return !(v & 0x0008); }),
				"auto send: the device cleared it by itself: the Log says so once, the box unticks, it stays off");

		/* the fake device of the other steps again */
		connectTo(FAKE_DEVICE_PORT);
		check(cellShows(valueCell(table_, regs_.u8.name), QString::number(other_.readU8(regs_.u8.addr)), 5000),
				"auto send: done, the window polls the fake device of the other steps again");
		fake.kill();
		fake.waitForFinished(3000);
	}

	/* Fast EVRe, part 5.1: the sidebar's Fast streams card for a map with streams (maps/example_fast.json), against
	 * evre_fake_fast on a port of its own. Start writes 1 to the stream's enable register, the blocks are counted (the
	 * rate as fitted, the samples lost), CONFIG is read every 100 ms while it runs (so the device's 2 s watchdog never
	 * stops it, even with Poll off), Stop and Disconnect write 0 (Disconnect first, before the link closes), a lost
	 * link keeps it wanted, a device that takes the enable and sends nothing is switched off again after 2 s, a bus
	 * greys the card, the Monitor names a block, and the card's texts fit its width in English and Arabic. */
	void fastStreams() {
		auto *sidebar = window_.findChild<Sidebar *>();
		const QString fastMapFile = QCoreApplication::applicationDirPath() + QStringLiteral("/maps/example_fast.json");
		DeviceMap fastMap;
		QString error;
		const bool loaded = fastMap.load(fastMapFile, error) && fastMap.streams.size() == 1;
		const RegDef *enable = loaded ? fastMap.registerNamed(fastMap.streams[0].enable) : nullptr;
		check(sidebar && sidebar->fastCard() && sidebar->fastCard()->isHidden() && loaded && enable,
				"fast streams: no card for a map without streams; the fast example map has a stream and its enable register");
		if (!sidebar || !loaded || !enable) return;
		QTemporaryDir folder;
		/* the same device without its stream: it takes the enable and never sends a block */
		DeviceMap quietMap = fastMap;
		quietMap.streams.clear();
		const QString quietFile = folder.filePath(QStringLiteral("quiet.json"));
		/* the example map at another path: loaded again at the end (the window's own path counts as loaded) */
		const QString exampleFile = folder.filePath(QStringLiteral("example_again.json"));
		const bool written = quietMap.save(quietFile, error) && QFile::copy(map_.path, exampleFile);

		QProcess fake;
		auto startFake = [&](const QString &mapFile, const QStringList &aids) {
			fake.start(QCoreApplication::applicationDirPath() + QStringLiteral("/evre_fake_fast"),
					QStringList{ QString::number(FAKE_FAST_PORT), mapFile, QString::fromLatin1(fakeDeviceToken) } + aids);
			return fake.waitForStarted(3000);
		};
		auto stopFake = [&] {
			fake.kill();
			fake.waitForFinished(3000);
		};
		auto device = std::make_unique<OtherClient>();
		auto openDevice = [&] {
			device = std::make_unique<OtherClient>();
			return QTest::qWaitFor([&] { return device->open(FAKE_FAST_PORT, fastMap.slave); }, 5000);
		};
		auto enableBecomes = [&](int value, int ms = 3000) {
			return QTest::qWaitFor([&] { return device->readU8(enable->addr) == value; }, ms);
		};
		const bool started = written && startFake(fastMapFile, {}) && openDevice();
		check(started, "fast streams: the fake device (evre_fake_fast with the fast example map) started");
		if (!started) return;

		/* the map loaded, not connected: one row, greyed, saying why */
		if (QPushButton *disconnect = buttonWithText(QStringLiteral("Disconnect"))) disconnect->click();
		MainWindow::Startup mapOnly;
		mapOnly.map = fastMapFile;
		window_.applyStartup(mapOnly);
		QPushButton *button = sidebar->fastButton(0);
		check(!sidebar->fastCard()->isHidden() && sidebar->fastStreamCount() == 1 && button
						&& button->text() == QStringLiteral("▶  Start stream") && !button->isEnabled()
						&& sidebar->fastRateText(0) == QLatin1String("not connected")
						&& button->toolTip() == QLatin1String("Offered once connected."),
				"fast streams: a map with a stream: the card shows a row for it, greyed while not connected, saying why");
		{ /* the row is headed by the stream's name, the map's description its tooltip; the card before Polling & recording */
			QLabel *title = sidebar->fastCard()->findChild<QLabel *>(QStringLiteral("fastStreamName"));
			QCheckBox *poll = nullptr;
			for (QCheckBox *box : sidebar->findChildren<QCheckBox *>())
				if (box->text() == QLatin1String("Poll")) poll = box;
			const int cardY = sidebar->fastCard()->mapTo(sidebar, QPoint(0, 0)).y();
			const int pollY = poll ? poll->mapTo(sidebar, QPoint(0, 0)).y() : -1;
			check(title && title->text().contains(QLatin1String("ADC")) && title->toolTip().contains(QLatin1String("sampled together"))
							&& poll && cardY < pollY,
					"fast streams: the row is headed by the stream's name (the map's description its tooltip), the button says "
					"only Start stream; the card sits before Polling & recording");
		}
		if (!button) return;

		/* connected: offered, off, the map's rate (a lost link comes back by itself below) */
		for (QCheckBox *b : window_.findChildren<QCheckBox *>())
			if (b->text() == QLatin1String("Reconnect by itself")) b->setChecked(true);
		MainWindow::Startup connectFast;
		connectFast.tcp = QStringLiteral("127.0.0.1:%1").arg(FAKE_FAST_PORT);
		connectFast.connect = true;
		window_.applyStartup(connectFast);
		const bool offered = QTest::qWaitFor([&] { return button->isEnabled(); }, 5000);
		const bool offShown = QTest::qWaitFor([&] {
			return sidebar->fastRateText(0) == QStringLiteral("off · 10.0 k samples/s");
		}, 3000);
		if (!offShown || !button->toolTip().contains(QLatin1String("Start writes 1 to ADC_STREAM")))
			std::printf("  the rate: \"%s\", the tooltip: \"%s\"\n", qPrintable(sidebar->fastRateText(0)),
					qPrintable(button->toolTip()));
		check(offered && offShown && button->cursor().shape() == Qt::PointingHandCursor
						&& button->toolTip().contains(QLatin1String("Start writes 1 to ADC_STREAM")),
				"fast streams: connected, the button is offered (a pointing hand, a tooltip that says what it writes); "
				"off, the map's 10.0 k samples/s");

		/* Start: the enable written 1, the blocks counted, the rate as fitted, nothing lost */
		button->click();
		const bool on = enableBecomes(1);
		const QRegularExpression rate(QStringLiteral("^(9\\.9|10\\.0|10\\.1) k samples/s \\([+-][0-9]+ ppm\\)$"));
		const bool counted = QTest::qWaitFor([&] { return rate.match(sidebar->fastRateText(0)).hasMatch(); }, 6000);
		if (!counted) std::printf("  the rate: \"%s\"\n", qPrintable(sidebar->fastRateText(0)));
		check(on && button->text() == QStringLiteral("■  Stop stream") && button->objectName() == QLatin1String("danger")
						&& counted && sidebar->fastLostText(0) == QLatin1String("lost 0")
						&& logText().contains(QLatin1String("fast stream ADC on: 10000 samples a second")),
				"fast streams: Start writes 1 to ADC_STREAM; the button becomes a red Stop; the card shows 10.0 k "
				"samples/s with its correction in ppm and lost 0; the Log says so");

		/* a channel's Plot tick in the card: its line on the chart, its records kept, its newest value beside it */
		auto *chartTab = window_.findChild<ChartTab *>();
		ChartView *chartView = chartTab ? chartTab->view() : nullptr;
		QCheckBox *plotBox = sidebar->fastPlotBox(0, 0);
		const int iLoad = ChartView::fastKey(0, 0);
		const auto onChart = [&](int key) {
			if (!chartView) return false;
			for (const ChartView::Info &line : chartView->lines())
				if (line.key == key) return true;
			return false;
		};
		bool plotted = false;
		if (plotBox && chartView) {
			plotBox->setChecked(true);
			plotted = onChart(iLoad) && QTest::qWaitFor([&] { return chartView->pointsKept(iLoad) >= 10000; }, 4000)
					&& QTest::qWaitFor([&] { return sidebar->fastValueText(0, 0).endsWith(QLatin1String(" A")); }, 3000);
		}
		std::printf("  ADC.I_LOAD on the chart (%d): %lld samples kept, the card shows \"%s\", the info line \"%s\"; the box: "
				"hand %d, tip \"%s\"\n", int(plotted),
				(long long) (chartView ? chartView->pointsKept(iLoad) : 0), qPrintable(sidebar->fastValueText(0, 0)),
				chartTab ? qPrintable(chartTab->infoText()) : "", plotBox ? int(plotBox->cursor().shape()) : -1,
				plotBox ? qPrintable(plotBox->toolTip()) : "");
		check(plotted && plotBox->cursor().shape() == Qt::PointingHandCursor && plotBox->toolTip().contains(QLatin1String("ADC.I_LOAD"))
						&& chartTab->infoText().contains(QStringLiteral(" · 1 fast")),
				"fast streams: a channel's Plot tick in the card (a pointing hand, a tooltip): ADC.I_LOAD on the chart, its "
				"samples kept, its newest value beside the tick, \"1 fast\" in the chart's info line");
		/* one cap of 64 lines for every kind (chartOneCap): the chart filled with math lines, a second channel's tick is
		 * taken back with the same words in the status bar; the lines removed, it is taken */
		{
			auto *math = chartTab ? chartTab->findChild<QPushButton *>(QStringLiteral("math")) : nullptr;
			QCheckBox *second = sidebar->fastPlotBox(0, 1);
			bool refused = false, takenAfter = false;
			int fields = 0;
			QVector<int> plotted;
			if (math && second && chartTab) {
				const bool wasOn = second->isChecked(); /* ADC.V_BUS, off for the check, as it was after */
				second->setChecked(false);
				fields = fillWithFields(chartTab, *enable, plotted);
				window_.statusBar()->clearMessage();
				second->setChecked(true);
				QApplication::processEvents();
				refused = chartTab->lineCount() == RegisterModel::MAX_PLOTTED && !second->isChecked()
						&& !chartTab->fastPlotted(0, 1) && window_.statusBar()->currentMessage() == ChartTab::lineCapText()
						&& chartTab->infoText().startsWith(QStringLiteral("64/64 plotted · %1 math · 1 fast")
								.arg(chartTab->mathLinesShown()));
				if (!refused) std::printf("     (the 64th line: %d lines, the tick %d, said \"%s\", info \"%s\")\n",
						chartTab->lineCount(), int(second->isChecked()), qPrintable(window_.statusBar()->currentMessage()),
						qPrintable(chartTab->infoText()));
				removeFields(math, *enable, fields, plotted);
				second->setChecked(true);
				takenAfter = chartTab->fastPlotted(0, 1);
				second->setChecked(wasOn);
				window_.statusBar()->clearMessage();
			}
			check(refused && takenAfter, "fast streams: one cap of 64 lines with the math lines and the registers: with the "
					"chart full a channel's tick is taken back with the same words in the status bar; with room, it is taken");
		}
		/* measured as any line: its row in the Measure table (the device's 50 Hz sine of 6.55 A: RMS 4.63 A); its chip's
		 * menu offers the histogram and the spectrum, the spectrum of its records as they are (evenly spaced) */
		QString rms, mean, summary, histogramSummary;
		double peak = 0, resolution = 0;
		bool inTrigger = false;
		bool menuOffered = false;
		if (plotted) {
			auto *measure = chartTab->findChild<QPushButton *>(QStringLiteral("measure"));
			auto *table = chartTab->findChild<QTableWidget *>(QStringLiteral("measures"));
			auto *tabs = window_.findChild<QTabWidget *>();
			if (tabs) tabs->setCurrentIndex(MainWindow::TabChart);
			if (measure && table) {
				measure->setChecked(true);
				const auto cellOf = [&](int column) {
					for (int row = 0; row < table->rowCount(); row++)
						if (table->item(row, ChartTab::ColLine) && table->item(row, ChartTab::ColLine)->text().contains(QLatin1String("ADC.I_LOAD")))
							return table->item(row, column) ? table->item(row, column)->text() : QString();
					return QString();
				};
				(void) QTest::qWaitFor([&] { return cellOf(ChartTab::ColRms).startsWith(QLatin1String("4.")); }, 3000);
				rms = cellOf(ChartTab::ColRms);
				auto *line = chartTab->findChild<QComboBox *>(QStringLiteral("triggerLine"));
				auto *trigger = chartTab->findChild<QAction *>(QStringLiteral("chartTrigger"));
				if (line && trigger) {
					trigger->setChecked(true);
					inTrigger = QTest::qWaitFor([&] { return line->findText(QStringLiteral("ADC.I_LOAD")) >= 0; }, 2000)
							&& line->toolTip().contains(QLatin1String("fast line"));
					trigger->setChecked(false);
					chartView->setLive(true); /* the trigger may have held it */
				}
				mean = cellOf(ChartTab::ColMean);
				measure->setChecked(false);
			}
			if (tabs) tabs->setCurrentIndex(MainWindow::TabRegisters);
			chartTab->showLineMenu(iLoad, QPoint(0, 0));
			QMenu *menu = chartTab->lineMenu();
			menuOffered = menu && menu->actions().size() == 4; /* Histogram, Spectrum, Trigger on this line */
			for (QAction *action : menu ? menu->actions() : QList<QAction *>()) menuOffered = menuOffered && action->isEnabled();
			if (menu) menu->hide();
			if (AnalysisWindow *spectrum = chartTab->openAnalysis(AnalysisWindow::Kind::Spectrum, iLoad)) {
				summary = spectrum->summary();
				const analysis::Spectrum &found = spectrum->spectrum();
				qsizetype best = 1;
				for (qsizetype j = 1; j < found.amplitude.size(); j++)
					if (found.amplitude[j] > found.amplitude[best]) best = j;
				peak = found.frequency.value(best);
				resolution = found.resolution();
				spectrum->close();
			}
			if (AnalysisWindow *histogram = chartTab->openAnalysis(AnalysisWindow::Kind::Histogram, iLoad)) {
				histogramSummary = histogram->summary();
				histogram->close();
			}
		}
		std::printf("  ADC.I_LOAD measured: RMS \"%s\", mean \"%s\"; spectrum \"%s\"; histogram \"%s\"\n", qPrintable(rms),
				qPrintable(mean), qPrintable(summary), qPrintable(histogramSummary));
		check(QRegularExpression(QStringLiteral("^4\\.[5-7]\\d* A$")).match(rms).hasMatch() && !mean.isEmpty(),
				"fast streams: a fast line's row in the Measure table: the device's 50 Hz sine of 6.55 A reads about 4.6 A RMS");
		check(inTrigger, "fast streams: the trigger's line list offers the fast line (its tooltip names fast lines)");
		/* the trigger on the fast line, its crossing found on the engine's thread as each block comes: armed from its chip's
		 * menu (Trigger on this line), level 0, rising; the view holds on a crossing between two of its records (within
		 * one record), at the time the store gives it; how long after the crossing the window held is said */
		bool engineCrossing = false;
		double offsetRecords = -1, delayMs = -1;
		if (plotted) {
			auto *level = chartTab->findChild<QLineEdit *>(QStringLiteral("triggerLevel"));
			auto *trigger = chartTab->findChild<QAction *>(QStringLiteral("chartTrigger"));
			double heldAt = NAN;
			const QMetaObject::Connection seen = QObject::connect(chartView, &ChartView::triggered, chartView, [&](double) {
				if (std::isnan(heldAt)) heldAt = chartView->timeNow();
			});
			chartTab->triggerOnLine(iLoad);
			if (level) {
				level->setText(QStringLiteral("0"));
				emit level->editingFinished();
			}
			const int before = chartView->triggerHolds();
			heldAt = NAN;
			const bool held = QTest::qWaitFor([&] { return chartView->triggerHolds() > before; }, 4000);
			const double at = chartView->triggeredAt();
			QObject::disconnect(seen);
			const fast::Store *store = chartView->fastStore(0);
			if (held && store) {
				const qsizetype i = store->lowerBound(at);
				if (i > 0 && i < store->size()) {
					const double pt = store->timeAt(i - 1), t = store->timeAt(i);
					const double pv = store->value(0, i - 1), v = store->value(0, i);
					const double expected = v != pv ? pt + (0 - pv) / (v - pv) * (t - pt) : t;
					offsetRecords = std::fabs(at - expected) / (t - pt);
					engineCrossing = pv < 0 && v >= 0 && at >= pt && at <= t && offsetRecords < 1e-3;
				}
				delayMs = (heldAt - at) * 1000;
			}
			if (trigger) trigger->setChecked(false);
			chartView->setLive(true);
		}
		std::printf("  the trigger on ADC.I_LOAD: held %d, %.2g records off the crossing between its records; held %.1f ms "
				"after the crossing's time\n", int(engineCrossing), offsetRecords, delayMs);
		check(engineCrossing, "fast streams: the trigger on a fast line, armed from its chip's menu: its crossing found on the "
				"engine's thread as each block comes, the view held at the crossing's time between its two records");
		check(menuOffered && summary.contains(QLatin1String("evenly spaced")) && !summary.contains(QLatin1String("e+")) && resolution > 0 && std::fabs(peak - 50) <= resolution
						&& histogramSummary.contains(QLatin1String("samples")),
				"fast streams: a fast line's chip menu offers Histogram and Spectrum; the spectrum takes its samples as they "
				"are (evenly spaced, its rate written whole: 10000 Hz, not 1e+04), its peak at the device's 50 Hz (within one "
				"step of its frequencies)");
		/* recorded: Record CSV while the stream is on writes its blocks beside the CSV (fast.csv, fast.ADC.evrs); the
		 * recording opens with them, the fast line's samples those the live chart took; the .evrs opens alone too, on the
		 * same clock; one cut off opens up to its last whole piece and says so */
		{
			QTemporaryDir folder;
			const QString csv = folder.filePath(QStringLiteral("fast.csv")), evrs = folder.filePath(QStringLiteral("fast.ADC.evrs"));
			MainWindow::Startup record;
			record.record = csv;
			window_.applyStartup(record);
			QPushButton *stop = nullptr;
			(void) QTest::qWaitFor([&] { return (stop = buttonWithText(QStringLiteral("■  Stop recording"))) != nullptr; }, 3000);
			QTest::qWait(1500);
			if (stop) stop->click();
			(void) QTest::qWaitFor([&] { return !buttonWithText(QStringLiteral("■  Stop recording")); }, 3000);
			const bool written = QFileInfo::exists(evrs) && logText().contains(QLatin1String("fast stream ADC recorded: "));
			RecordingWindow *opened = nullptr, *alone = nullptr, *cut = nullptr;
			/* a fast math line of the recording's window's own (recording/math), computed from the records recorded */
			const QVariant mathBefore = QSettings().value(QStringLiteral("recording/math"));
			QSettings().setValue(QStringLiteral("recording/math"), QStringList{ QStringLiteral("P\tW\tADC.I_LOAD * ADC.V_BUS\t1") });
			RecordingWindow::open(nullptr, csv, {}, 2048, [&](RecordingWindow *w) { opened = w; });
			(void) QTest::qWaitFor([&] { return opened != nullptr; }, 5000);
			qint64 samples = 0, compared = 0, differ = 0;
			quint64 lostThere = 1;
			bool lined = false;
			if (opened && opened->fastRecordings().size() == 1 && opened->fastRecordings()[0].store && chartView) {
				const fast::Store &file = *opened->fastRecordings()[0].store;
				const fast::Store *live = chartView->fastStore(0);
				samples = file.size();
				lostThere = opened->fastRecordings()[0].lost;
				for (const ChartView::Info &line : opened->chartTab()->view()->lines()) lined = lined || line.key == iLoad;
				for (qsizetype i = 0; live && i < file.size(); i += std::max<qsizetype>(1, file.size() / 2000)) {
					const double t = file.timeAt(i);
					const qsizetype j = live->lowerBound(t - 1e-7);
					if (j >= live->size() || std::fabs(live->timeAt(j) - t) > 1e-6) continue;
					compared++;
					differ += live->value(0, j) != file.value(0, i) || live->value(1, j) != file.value(1, i);
				}
			}
			std::printf("  recorded beside the CSV: %d; opened with it: %lld samples, %llu lost, %lld compared with the live "
					"chart's, %lld differ\n", int(written), (long long) samples, (unsigned long long) lostThere,
					(long long) compared, (long long) differ);
			check(written && samples >= 10000 && lostThere == 0 && lined && compared >= 1000 && differ == 0,
					"fast streams recorded: Record CSV writes the stream's blocks beside the CSV (fast.ADC.evrs), the Log says "
					"so; the recording opens with them, a fast line whose samples are those the live chart took");
			{
				const fast::Store *math = opened ? opened->chartTab()->view()->fastStore(MathLines::fastStream(0)) : nullptr;
				const fast::Store *file = opened && !opened->fastRecordings().isEmpty() ? opened->fastRecordings()[0].store.get()
																						: nullptr;
				bool computed = math && file && file->size() > 0 && math->size() == file->size();
				for (qsizetype i = 0; computed && i < file->size(); i++)
					computed = math->timeAt(i) == file->timeAt(i)
							&& math->value(0, i) == double(float(file->value(0, i) * file->value(1, i)));
				std::printf("  the recording's fast math line: %lld records of %lld\n", (long long) (math ? math->size() : -1),
						(long long) (file ? file->size() : -1));
				check(computed, "fast streams recorded: the recording's window computes its own fast math line (recording/math, "
						"ADC.I_LOAD * ADC.V_BUS) from the records recorded, record by record");
				if (mathBefore.isValid()) QSettings().setValue(QStringLiteral("recording/math"), mathBefore);
				else QSettings().remove(QStringLiteral("recording/math"));
			}
			recordingLinesList(opened, folder.path());
			recordingWindowPictures(csv, QStringLiteral("fast"));
			if (opened && qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* for a look: the recording with its fast lines */
				opened->resize(1400, 800);
				QTest::qWait(300);
				opened->grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_fast_recording.png"));
			}
			/* the .evrs alone, on the clock of its head: the same wall-clock time as beside the CSV */
			RecordingWindow::open(nullptr, evrs, {}, 2048, [&](RecordingWindow *w) { alone = w; });
			(void) QTest::qWaitFor([&] { return alone != nullptr; }, 5000);
			const auto infoOf = [](RecordingWindow *w) {
				const QLabel *info = w ? w->findChild<QLabel *>(QStringLiteral("recordingInfo")) : nullptr;
				return info ? info->text() : QString();
			};
			double apart = 1e9;
			if (opened && alone && !alone->fastRecordings().isEmpty() && !opened->fastRecordings().isEmpty())
				apart = std::fabs(double(alone->chartTab()->view()->epochMs()) + alone->fastRecordings()[0].firstTime * 1000
						- double(opened->chartTab()->view()->epochMs()) - opened->fastRecordings()[0].firstTime * 1000);
			std::printf("  the .evrs alone: \"%s\", its first sample %.1f ms from where the CSV's window lays it\n",
					qPrintable(infoOf(alone)), apart);
			check(alone && infoOf(alone).contains(QLatin1String("ADC: ")) && apart < 50,
					"fast streams recorded: the .evrs opens alone, its samples counted in the window's line, on the same "
					"wall clock as beside its CSV (its head's start)");
			/* cut off inside its last piece */
			const QString cutFile = folder.filePath(QStringLiteral("cut.ADC.evrs"));
			bool truncated = QFile::copy(evrs, cutFile);
			if (truncated) {
				QFile f(cutFile);
				truncated = f.open(QIODevice::ReadWrite) && f.resize(f.size() - 3);
			}
			if (truncated) RecordingWindow::open(nullptr, cutFile, {}, 2048, [&](RecordingWindow *w) { cut = w; });
			(void) QTest::qWaitFor([&] { return cut != nullptr; }, 5000);
			std::printf("  cut off: \"%s\"\n", qPrintable(infoOf(cut)));
			check(cut && infoOf(cut).contains(QLatin1String("cut off")) && !cut->fastRecordings().isEmpty()
							&& cut->fastRecordings()[0].store->size() > 0,
					"fast streams recorded: a .evrs cut off opens up to its last whole piece, and its window says so");
			RecordingWindow::closeAll();
			/* Log off in the card: the next recording has no .evrs of the stream, its line still plotted; the tick kept
			 * by the stream's name (a card made again shows it off); on again, the next one has it */
			QCheckBox *log = sidebar->fastLogBox(0);
			bool tipped = false, notWritten = false, keptOff = false, writtenAgain = false;
			const auto recordFor = [&](const QString &name) {
				MainWindow::Startup again;
				again.record = folder.filePath(name);
				window_.applyStartup(again);
				QPushButton *stopIt = nullptr;
				(void) QTest::qWaitFor([&] { return (stopIt = buttonWithText(QStringLiteral("■  Stop recording"))) != nullptr; },
						3000);
				QTest::qWait(800);
				if (stopIt) stopIt->click();
				(void) QTest::qWaitFor([&] { return !buttonWithText(QStringLiteral("■  Stop recording")); }, 3000);
			};
			if (log) {
				tipped = log->isChecked() && log->cursor().shape() == Qt::PointingHandCursor
						&& log->toolTip().contains(QLatin1String("logged whole or not at all"))
						&& log->toolTip().contains(QLatin1String("run.ADC.evrs"));
				log->setChecked(false);
				recordFor(QStringLiteral("nolog.csv"));
				notWritten = QFileInfo::exists(folder.filePath(QStringLiteral("nolog.csv")))
						&& !QFileInfo::exists(folder.filePath(QStringLiteral("nolog.ADC.evrs"))) && chartTab->fastPlotted(0, 0);
				{
					Sidebar card;
					card.setFastStreams(fastMap.streams);
					keptOff = QSettings().value(QStringLiteral("fast/notLogged")).toStringList() == QStringList{ QStringLiteral("ADC") }
							&& card.fastLogBox(0) && !card.fastLogBox(0)->isChecked();
				}
				log->setChecked(true);
				recordFor(QStringLiteral("logged.csv"));
				writtenAgain = QFileInfo::exists(folder.filePath(QStringLiteral("logged.ADC.evrs")))
						&& QSettings().value(QStringLiteral("fast/notLogged")).toStringList().isEmpty();
			}
			std::printf("  Log off: the CSV %d, no .evrs %d, the line still on the chart %d; kept off %d; on again: written %d\n",
					int(QFileInfo::exists(folder.filePath(QStringLiteral("nolog.csv")))), int(notWritten),
					int(chartTab->fastPlotted(0, 0)), int(keptOff), int(writtenAgain));
			check(tipped, "fast streams, Log: a tick per stream in the card, on by default, a pointing hand; its tooltip says "
					"it writes run.ADC.evrs beside the CSV and that a stream is logged whole or not at all (each block "
					"carries every channel)");
			check(notWritten && keptOff && writtenAgain, "fast streams, Log off: a recording writes its CSV and no .evrs of the "
					"stream, whose line still plots; the choice kept by the stream's name (fast/notLogged); on again, the "
					"next recording has its .evrs");
		}
		/* the trigger armed on the fast line (from its chip's menu) while its stream comes and goes: Disconnect, Connect
		 * again, Arm, the stream stopped, the line removed. Each time the engine watches what the window asks (the same
		 * line, the same arm) or, the line gone, nothing; no watch, crossing or rescan reaches a stream, a run or a line
		 * that is gone (the test crashed once in this Disconnect). Normal: the engine keeps watching after a crossing;
		 * after the stream came back, Single armed takes the next one (Arm is Single's: in Normal it keeps the last
		 * crossing's hold-off, the 30 s window here). */
		bool watchArmed = false, watchKept = false, watchBack = false, armedAgain = false, stoppedKept = false,
				watchGone = false;
		if (plotted) {
			auto *trigger = chartTab->findChild<QAction *>(QStringLiteral("chartTrigger"));
			auto *mode = chartTab->findChild<QComboBox *>(QStringLiteral("triggerMode"));
			auto *level = chartTab->findChild<QLineEdit *>(QStringLiteral("triggerLevel"));
			const auto watched = [&] { /* the engine's watch, the window's arm */
				int stream = -1;
				const fast::TriggerWatch asked = chartView->fastTriggerWatch(stream);
				const fast::TriggerWatch engine = window_.engineFastWatch(0);
				return stream == 0 && asked.on && engine.on && engine.channel == asked.channel && engine.serial == asked.serial;
			};
			const auto inStep = [&] { return QTest::qWaitFor(watched, 3000); };
			const auto holdsAgain = [&] {
				const int before = chartView->triggerHolds();
				return QTest::qWaitFor([&] { return chartView->triggerHolds() > before; }, 6000);
			};
			const int modeWas = mode ? mode->currentIndex() : -1;
			if (mode) mode->setCurrentIndex(mode->findData(int(ChartView::TriggerMode::Normal)));
			chartTab->triggerOnLine(iLoad);
			if (level) {
				level->setText(QStringLiteral("0"));
				emit level->editingFinished();
			}
			watchArmed = inStep() && holdsAgain();
			if (QPushButton *disconnect = buttonWithText(QStringLiteral("Disconnect"))) disconnect->click();
			watchKept = QTest::qWaitFor([&] { return !button->isEnabled(); }, 3000) && enableBecomes(0) && inStep();
			window_.applyStartup(connectFast);
			if (QTest::qWaitFor([&] { return button->isEnabled(); }, 5000) && button->text() == QStringLiteral("▶  Start stream"))
				button->click();
			/* the watch as it was: the same arm, waiting out its re-arm (a window's length); Single armed takes the next
			 * crossing; Normal armed again stays armed through the stream's stop */
			watchBack = enableBecomes(1) && inStep();
			const auto setMode = [mode](ChartView::TriggerMode m) {
				if (!mode) return;
				mode->setCurrentIndex(mode->findData(int(m)));
				emit mode->activated(mode->currentIndex());
			};
			setMode(ChartView::TriggerMode::Single);
			armedAgain = holdsAgain();
			setMode(ChartView::TriggerMode::Normal);
			armedAgain = armedAgain && inStep();
			button->click(); /* Stop, armed */
			stoppedKept = enableBecomes(0);
			QTest::qWait(300); /* blocks of the stopped stream may still come */
			stoppedKept = stoppedKept && inStep();
			button->click();
			stoppedKept = stoppedKept && enableBecomes(1) && inStep();
			setMode(ChartView::TriggerMode::Single);
			stoppedKept = stoppedKept && holdsAgain();
			setMode(ChartView::TriggerMode::Normal);
			if (plotBox) plotBox->setChecked(false); /* the line removed, armed */
			watchGone = !onChart(iLoad) && QTest::qWaitFor([&] {
				int stream = -1;
				(void) chartView->fastTriggerWatch(stream);
				return stream == -1 && !window_.engineFastWatch(0).on;
			}, 3000);
			QTest::qWait(300); /* blocks with crossings for the arm before may still come: none is used */
			if (trigger) trigger->setChecked(false);
			if (mode) mode->setCurrentIndex(modeWas);
			chartView->setLive(true);
			if (plotBox) plotBox->setChecked(true);
		}
		std::printf("  the trigger on ADC.I_LOAD, armed: in step %d; Disconnect: kept %d; Connect: back %d; Single armed: "
				"held %d; the stream stopped and started, Single armed: held %d; the line removed: no watch %d\n",
				int(watchArmed), int(watchKept), int(watchBack), int(armedAgain), int(stoppedKept), int(watchGone));
		check(watchArmed && watchKept && watchBack && armedAgain && stoppedKept && watchGone,
				"fast streams, the trigger armed on a fast line: through Disconnect, Connect, Single armed and the stream stopped "
				"the engine watches what the window asks and holds again; the line removed, the engine watches nothing; no crash");
		if (plotBox) plotBox->setChecked(false);
		check(!onChart(iLoad), "fast streams: the tick off: the line off the chart");

		/* Poll off: CONFIG still read every 100 ms; the device's 2 s watchdog never stops the stream */
		auto *traffic = [&]() -> QLabel * {
			for (QLabel *label : window_.statusBar()->findChildren<QLabel *>())
				if (label->text().startsWith(QLatin1String("TX "))) return label;
			return nullptr;
		}();
		auto sent = [&] { return traffic ? traffic->text().section(QLatin1Char(' '), 1, 1).toLongLong() : 0; };
		poll_->setChecked(false);
		QTest::qWait(600);
		const qint64 txBefore = sent();
		QTest::qWait(3000);
		const qint64 heartbeats = sent() - txBefore;
		std::printf("  Poll off for 3 s, a fast stream on: %lld requests sent\n", heartbeats);
		check(traffic && heartbeats >= 24 && heartbeats <= 45 && device->readU8(enable->addr) == 1
						&& rate.match(sidebar->fastRateText(0)).hasMatch(),
				"fast streams, Poll off: CONFIG read every 100 ms; after 3 s the stream still runs (the device's 2 s "
				"host watchdog sees the Studio)");
		poll_->setChecked(true);

		/* the Monitor names a block */
		auto *monitor = window_.findChild<MonitorTab *>();
		auto *frames = monitor ? monitor->findChild<QPlainTextEdit *>() : nullptr;
		QCheckBox *logFrames = nullptr;
		for (QCheckBox *box : monitor ? monitor->findChildren<QCheckBox *>() : QList<QCheckBox *>())
			if (box->text() == QLatin1String("Log frames")) logFrames = box;
		bool named = false;
		if (frames && logFrames) {
			const bool was = logFrames->isChecked();
			logFrames->setChecked(true);
			named = QTest::qWaitFor([&] {
				return frames->toPlainText().contains(QLatin1String("7B 01 AB 00 DC"))
						&& frames->toPlainText().contains(QLatin1String("READ_RESP (fast stream ADC)"));
			}, 3000);
			logFrames->setChecked(was);
			frames->clear();
		}
		check(named, "fast streams: the Monitor names a block \"READ_RESP (fast stream ADC)\"");

		/* Stop: the enable written 0, off again */
		button->click();
		const bool stopped = enableBecomes(0);
		if (!QTest::qWaitFor([&] { return sidebar->fastRateText(0) == QStringLiteral("off · 10.0 k samples/s"); }, 3000))
			std::printf("  stopped: %d, the button \"%s\", the rate \"%s\"\n", stopped, qPrintable(button->text()),
					qPrintable(sidebar->fastRateText(0)));
		check(stopped && button->text() == QStringLiteral("▶  Start stream")
						&& QTest::qWaitFor([&] { return sidebar->fastRateText(0) == QStringLiteral("off · 10.0 k samples/s"); }, 3000)
						&& logText().contains(QLatin1String("fast stream ADC off")),
				"fast streams: Stop writes 0 to ADC_STREAM; the button is Start again, the card says off");

		/* on again, then Disconnect: 0 written before the link closes; the button off and greyed */
		button->click();
		const bool onAgain = enableBecomes(1);
		QPushButton *disconnectButton = buttonWithText(QStringLiteral("Disconnect"));
		if (disconnectButton) disconnectButton->click();
		check(onAgain && disconnectButton && enableBecomes(0) && button->text() == QStringLiteral("▶  Start stream")
						&& !button->isEnabled() && sidebar->fastRateText(0) == QLatin1String("not connected"),
				"fast streams, then Disconnect: ADC_STREAM 0 on the device first; the button Start, greyed, not connected");

		/* --fast (the command line, for an API client's script): as Start, the stream wanted at once and switched on once
		 * connected; a name the map has no stream of is said in the Log. Stopped and disconnected again after. */
		{
			MainWindow::Startup fastAtStart = connectFast;
			fastAtStart.fast = QStringList{ QStringLiteral("adc"), QStringLiteral("NOPE") };
			window_.applyStartup(fastAtStart);
			const bool started = enableBecomes(1, 5000) && QTest::qWaitFor([&] { return button->isEnabled(); }, 3000);
			check(started && button->text() == QStringLiteral("■  Stop stream")
							&& logText().contains(QLatin1String("--fast: no such fast stream in the map: NOPE")),
					"fast streams: --fast adc,NOPE: the stream switched on once connected (ADC_STREAM 1, the button Stop); "
					"the name the map has no stream of said in the Log");
			button->click();
			(void) enableBecomes(0);
			if (QPushButton *disconnect = buttonWithText(QStringLiteral("Disconnect"))) disconnect->click();
			(void) QTest::qWaitFor([&] { return !button->isEnabled(); }, 3000);
		}

		/* a lost link keeps it: the device comes back (losing every 5th block now), the stream goes on again by itself,
		 * and the samples lost are counted and shown */
		window_.applyStartup(connectFast);
		(void) QTest::qWaitFor([&] { return button->isEnabled(); }, 5000);
		button->click();
		const bool onBeforeLoss = enableBecomes(1);
		stopFake();
		const bool back = startFake(fastMapFile, { QStringLiteral("--fast-lose"), QStringLiteral("5") }) && openDevice();
		const bool again = back && enableBecomes(1, 10000) && button->text() == QStringLiteral("■  Stop stream");
		const bool lostShown = QTest::qWaitFor([&] {
			const QString text = sidebar->fastLostText(0);
			return text.startsWith(QLatin1String("lost ")) && text != QLatin1String("lost 0");
		}, 5000);
		QLabel *lostLabel = nullptr;
		for (QLabel *label : sidebar->fastCard()->findChildren<QLabel *>())
			if (label->text().startsWith(QLatin1String("lost "))) lostLabel = label;
		if (!again || !lostShown || !lostLabel)
			std::printf("  on before: %d, back: %d, again: %d, the button \"%s\", lost: \"%s\"\n", onBeforeLoss, back,
					again, qPrintable(button->text()), qPrintable(sidebar->fastLostText(0)));
		check(onBeforeLoss && again && lostShown && lostLabel && lostLabel->styleSheet().contains(QLatin1String("font-weight:600")),
				"fast streams: after a lost link and a reconnect, switched on again by itself; every 5th block lost: "
				"the card counts the samples lost, highlighted");
		button->click();
		(void) enableBecomes(0);

		/* a device that takes the enable and never sends: off again after 2 s, said in the Log */
		stopFake();
		const bool quietUp = startFake(quietFile, {}) && openDevice();
		window_.applyStartup(connectFast);
		(void) QTest::qWaitFor([&] { return button->isEnabled(); }, 5000);
		button->click();
		const bool tookIt = enableBecomes(1);
		const bool backOff = QTest::qWaitFor([&] { return button->text() == QStringLiteral("▶  Start stream"); }, 5000);
		check(quietUp && tookIt && backOff && enableBecomes(0)
						&& logText().contains(QLatin1String("fast stream ADC switched off: no block came in 2 s")),
				"fast streams: a device that takes the enable and sends no block: switched off again after 2 s (0 "
				"written), said in the Log");
		if (QPushButton *disconnect = buttonWithText(QStringLiteral("Disconnect"))) disconnect->click();
		stopFake();

		/* a bus: greyed, not on a bus */
		const QString busFile = folder.filePath(QStringLiteral("fast_bus.json"));
		QFile bus(busFile);
		if (bus.open(QIODevice::WriteOnly))
			bus.write(QStringLiteral("{ \"format\": \"evre-bus/1\", \"devices\": [ { \"name\": \"D1\", \"slave\": 1, \"map\": "
					"\"%1\" }, { \"name\": \"D2\", \"slave\": 2, \"map\": \"%1\" } ] }").arg(fastMapFile).toUtf8());
		bus.close();
		MainWindow::Startup onBus;
		onBus.bus = busFile;
		window_.applyStartup(onBus);
		check(!sidebar->fastCard()->isHidden() && !button->isEnabled() && sidebar->fastRateText(0) == QLatin1String("not on a bus")
						&& sidebar->fastButton(0)->toolTip().contains(QLatin1String("several devices")),
				"fast streams: a bus of devices with streams: the card greyed, not on a bus (they would collide)");

		/* the card's texts fit its width, in English and in Arabic, the widest numbers too */
		bool fits = true, arabicReads = false;
		QString notes, arabicNotes;
		bool header = true; /* the row's header: the name, then muted what the stream is, one line, both languages */
		QString headerNotes;
		for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
			language::apply(*qApp, code);
			Sidebar card;
			card.setFastStreams(fastMap.streams);
			card.show();
			card.setFastOffered(true, QString(), QString());
			IoEngine::Stats busy;
			IoEngine::Stats::Fast f;
			f.on = true;
			f.rate = 1234567.8;
			f.ppm = -123;
			f.lost = 123456789;
			busy.fast = { f };
			card.showStats(busy, true);
			card.setFastOn(0, true);
			QApplication::processEvents();
			QPushButton *b = card.fastButton(0);
			{
				QLabel *name = card.fastCard()->findChild<QLabel *>(QStringLiteral("fastStreamName"));
				auto *about = card.fastCard()->findChild<ElidedLabel *>(QStringLiteral("fastStreamAbout"));
				const QString rate = QChar(0x2066) + QStringLiteral("10 kS/s") + QChar(0x2069);
				const QString expected = code == QLatin1String("en") ? QStringLiteral("· 2 channels · ") + rate
						: QStringLiteral("· قناتان · ") + rate;
				const int line = name ? name->fontMetrics().height() : 0;
				const bool one = name && about && b && name->text() == QLatin1String("ADC") && about->fullText() == expected
						&& !about->isCut() && name->height() <= line + 8 && about->height() <= line + 8
						&& std::abs(name->geometry().center().y() - about->geometry().center().y()) <= 2
						&& name->geometry().bottom() < b->geometry().top() && name->font().bold()
						&& about->toolTip().contains(QLatin1String("I_LOAD, V_BUS"))
						&& about->toolTip().contains(QLatin1String("sampled together"))
						&& (code == QLatin1String("en") ? name->x() < about->x() : name->x() > about->x());
				if (!one)
					headerNotes += QStringLiteral(" %1: \"%2\" \"%3\" cut %4, heights %5 %6 (a line %7), x %8 %9;").arg(code,
							name ? name->text() : QString(), about ? about->fullText() : QString())
							.arg(about ? int(about->isCut()) : -1).arg(name ? name->height() : -1).arg(about ? about->height() : -1)
							.arg(line).arg(name ? name->x() : -1).arg(about ? about->x() : -1);
				header = header && one;
			}
			const auto labels = card.fastCard()->findChildren<QLabel *>();
			for (QLabel *label : labels) {
				if (label->text().isEmpty() || label->objectName() == QLatin1String("cardTitle")) continue;
				if (label->fontMetrics().horizontalAdvance(label->text()) > label->width()) {
					fits = false;
					notes += QStringLiteral(" %1: \"%2\" %3 px in %4").arg(code, label->text())
							.arg(label->fontMetrics().horizontalAdvance(label->text())).arg(label->width());
				}
			}
			if (!b || b->fontMetrics().horizontalAdvance(b->text()) > b->width() - 16) {
				fits = false;
				notes += QStringLiteral(" %1: the button").arg(code);
			}
			/* Arabic: both number lines are laid out right to left, so they sit at the right like the card's title, and
			 * in them a number keeps its prefix, its unit and its groups of three left to right: not "k 10.0". Asked of
			 * the text's layout: where each piece's glyphs come to lie on the line (a cursor position between two
			 * runs of opposite direction says nothing) */
			if (code == QLatin1String("ar")) {
				auto inOrder = [&card](const QString &text, const QStringList &pieces) {
					QTextLayout layout(text, card.font());
					QTextOption option;
					option.setTextDirection(text.isRightToLeft() ? Qt::RightToLeft : Qt::LeftToRight);
					layout.setTextOption(option);
					layout.beginLayout();
					QTextLine line = layout.createLine();
					line.setLineWidth(100000);
					layout.endLayout();
					const QList<QGlyphRun> runs = line.glyphRuns(-1, -1, QTextLayout::RetrieveGlyphIndexes
							| QTextLayout::RetrieveGlyphPositions | QTextLayout::RetrieveStringIndexes);
					auto leftOf = [&runs](int at, int size) { /* the leftmost glyph of the text's characters at..at+size */
						qreal left = -1;
						for (const QGlyphRun &run : runs) {
							const QList<qsizetype> indexes = run.stringIndexes();
							const QList<QPointF> positions = run.positions();
							for (int g = 0; g < indexes.size() && g < positions.size(); g++)
								if (indexes[g] >= at && indexes[g] < at + size && (left < 0 || positions[g].x() < left))
									left = positions[g].x();
						}
						return left;
					};
					qreal before = -1;
					int from = 0;
					for (const QString &piece : pieces) {
						const int at = int(text.indexOf(piece, from));
						if (at < 0) return false;
						const qreal x = leftOf(at, int(piece.size()));
						if (x < 0 || x <= before) return false;
						before = x;
						from = at + int(piece.size());
					}
					return true;
				};
				const QString running = card.fastRateText(0), lost = card.fastLostText(0);
				card.setFastOn(0, false);
				card.showStats(IoEngine::Stats(), true);
				const QString off = card.fastRateText(0);
				arabicReads = running.isRightToLeft() && inOrder(running, { QStringLiteral("1.23"), QStringLiteral("M") })
						&& inOrder(running, { QStringLiteral("("), QStringLiteral("123"), QStringLiteral("ppm"), QStringLiteral(")") })
						&& lost.isRightToLeft()
						&& inOrder(lost, { QStringLiteral("123"), QStringLiteral("456"), QStringLiteral("789") })
						&& off.isRightToLeft() && inOrder(off, { QStringLiteral("10.0"), QStringLiteral("k") });
				arabicNotes = QStringLiteral("\"%1\" | \"%2\" | \"%3\"").arg(running, lost, off);
			}
			if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* for a look: running and off, in both themes */
				for (const bool running : { true, false }) {
					for (const bool dark : { false, true }) {
						Theme::apply(*qApp, dark);
						card.setFastOn(0, running);
						card.showStats(running ? busy : IoEngine::Stats(), true); /* the lost count's colour is the theme's */
						QApplication::processEvents();
						card.fastCard()->grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_fast_%1_%2_%3.png")
								.arg(code, running ? QStringLiteral("on") : QStringLiteral("off"),
										dark ? QStringLiteral("dark") : QStringLiteral("light")));
					}
				}
			}
		}
		language::apply(*qApp, QStringLiteral("en"));
		if (!fits) std::printf("  %s\n", qPrintable(notes));
		check(fits, "fast streams: the card's button and numbers fit the sidebar's width in English and Arabic "
				"(1.23 M samples/s, lost 123 456 789)");
		if (!arabicReads) std::printf("  %s\n", qPrintable(arabicNotes));
		if (!header) std::printf("  the header:%s\n", qPrintable(headerNotes));
		check(header, "fast streams: each stream's row has a one-line header: its name in the card's name weight (ADC), then "
				"muted \"· 2 channels · 10 kS/s\" (the rate one left-to-right piece, Arabic too), not cut at the sidebar's "
				"width, both on one line above the button, the name first in the reading direction; the tooltip the "
				"channels and the map's description");
		check(arabicReads, "fast streams, Arabic: the rate (running and off) and the lost count are laid out right to left, "
				"like the card's title, and each number keeps its prefix, its unit and its groups left to right "
				"(1.23 M, (-123 ppm), 123 456 789, 10.0 k)");

		/* the example map and the fake device of the other steps again */
		MainWindow::Startup example;
		example.map = exampleFile;
		example.tcp = QStringLiteral("127.0.0.1:%1").arg(FAKE_DEVICE_PORT);
		example.connect = true;
		window_.applyStartup(example);
		check(sidebar->fastCard()->isHidden()
						&& cellShows(valueCell(table_, regs_.u8.name), QString::number(other_.readU8(regs_.u8.addr)), 5000),
				"fast streams: done; the example map again (no card), the window polls the fake device of the other steps");
		/* run twice, the step held on records with the times of the link before (a view 20 s off its crossing) */
		check(chartView && !chartView->fastStore(0),
				"fast streams: a map without the stream leaves no store of it in the chart: the same stream in a map loaded "
				"later starts afresh");
	}

	/* Fast EVRe's speed on this machine (FAST_PLAN.md section 16): evre_fake_fast sending a million records a second of
	 * two i16 (--fast-rate), both channels on the chart over a 10 s window, for 20 s: no record lost, no bad block,
	 * none left unshown, and the chart's paint on average at most 8 ms a frame over the last 10 s */
	void fastSpeed() {
		auto *sidebar = window_.findChild<Sidebar *>();
		auto *chartTab = window_.findChild<ChartTab *>();
		auto *tabs = window_.findChild<QTabWidget *>();
		const QString fastMapFile = QCoreApplication::applicationDirPath() + QStringLiteral("/maps/example_fast.json");
		QTemporaryDir folder;
		const QString exampleFile = folder.filePath(QStringLiteral("example_again.json"));
		QProcess fake;
		fake.start(QCoreApplication::applicationDirPath() + QStringLiteral("/evre_fake_fast"),
				{ QString::number(FAKE_FAST_PORT), fastMapFile, QString::fromLatin1(fakeDeviceToken), QStringLiteral("--fast-rate"),
					QStringLiteral("1000000") });
		OtherClient device;
		const bool started = sidebar && chartTab && tabs && QFile::copy(map_.path, exampleFile) && fake.waitForStarted(3000)
				&& QTest::qWaitFor([&] { return device.open(FAKE_FAST_PORT, 1); }, 5000);
		check(started, "fast speed: evre_fake_fast at 1 000 000 records a second started");
		if (!started) return;
		if (QPushButton *disconnect = buttonWithText(QStringLiteral("Disconnect"))) disconnect->click();
		MainWindow::Startup connectFast;
		connectFast.map = fastMapFile;
		connectFast.tcp = QStringLiteral("127.0.0.1:%1").arg(FAKE_FAST_PORT);
		connectFast.connect = true;
		window_.applyStartup(connectFast);
		QPushButton *button = sidebar->fastButton(0);
		const bool offered = button && QTest::qWaitFor([&] { return button->isEnabled(); }, 5000);
		ChartView *view = chartTab->view();
		const double windowBefore = view->window();
		view->setWindow(10);
		sidebar->fastPlotBox(0, 0)->setChecked(true);
		sidebar->fastPlotBox(0, 1)->setChecked(true);
		tabs->setCurrentIndex(MainWindow::TabChart);
		if (offered) button->click();
		const QRegularExpression rate(QStringLiteral("^(9[5-9][0-9]\\.[0-9]|10[0-4][0-9]\\.[0-9]) k samples/s|^1\\.0[0-4] M samples/s"));
		QTest::qWait(10000); /* the window filled */
		(void) view->takePerfStats();
		QTest::qWait(10000);
		const ChartView::PerfStats perf = view->takePerfStats();
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* for a look: the window with two fast lines, live */
			window_.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_fast_chart.png"));
			view->setLive(false);
			view->setWindow(0.002);
			view->refresh();
			QTest::qWait(300);
			window_.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_fast_records.png"));
			view->setLive(true);
			view->setWindow(10);
		}
		const double paint = perf.frames ? perf.paintSum / perf.frames : 1e9;
		const QString tip = sidebar->fastRateTip(0);
		const QRegularExpressionMatch counts = QRegularExpression(
				QStringLiteral("(\\d+) samples in (\\d+) blocks since Start; (\\d+) bad blocks; (\\d+) samples not shown")).match(tip);
		const qint64 records = counts.hasMatch() ? counts.captured(1).toLongLong() : 0;
		const qint64 bad = counts.hasMatch() ? counts.captured(3).toLongLong() : -1;
		const qint64 notShown = counts.hasMatch() ? counts.captured(4).toLongLong() : -1;
		std::printf("  fast speed: %s, %s; %lld records, %lld bad blocks, %lld not shown; %d frames in 10 s, paint %.2f ms "
				"on average (bin %.2f, lines %.2f, strip %.2f), at most %.1f ms\n", qPrintable(sidebar->fastRateText(0)),
				qPrintable(sidebar->fastLostText(0)), (long long) records, (long long) bad, (long long) notShown, perf.frames,
				paint, perf.bin / std::max(1, perf.frames), perf.lines / std::max(1, perf.frames),
				perf.strip / std::max(1, perf.frames), perf.paintMax);
		check(offered && records >= 15000000 && sidebar->fastLostText(0) == QLatin1String("lost 0") && bad == 0 && notShown == 0
						&& rate.match(sidebar->fastRateText(0)).hasMatch(),
				"fast speed: 20 s at a million records a second of two i16: about 20 million records taken, none lost, "
				"no bad block, none left unshown");
		check(perf.frames > 100 && paint <= 8.0, "fast speed: two fast lines over a 10 s window of a million records a "
				"second: the chart's paint at most 8 ms a frame on average");
		/* the window's thread held as Windows holds it while a title bar's button is pressed (a dialog closed with its
		 * X): the blocks pile up meanwhile, and the chart must not stand still for the frames after paying for them */
		const double framePeriod = 1000.0 / std::max(60.0, window_.screen() ? window_.screen()->refreshRate() : 60.0);
		(void) view->takePerfStats();
		const int paintsBefore = view->paints();
		const int leftOverBefore = window_.fastSyncsLeftOver();
		QThread::msleep(700);
		QElapsedTimer sinceHold;
		sinceHold.start();
		while (view->paints() == paintsBefore && !sinceHold.hasExpired(2000)) QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
		const double firstPaint = double(sinceHold.nsecsElapsed()) / 1e6;
		QString inSlots;
		int fewest = 1000;
		double paintAfter = 0;
		for (int slot = 0; slot < 5; slot++) {
			QTest::qWait(200);
			const ChartView::PerfStats after = view->takePerfStats();
			inSlots += QStringLiteral("%1%2 (%3 ms)").arg(slot ? QStringLiteral(", ") : QString()).arg(after.frames)
					.arg(after.paintMax, 0, 'f', 1);
			if (slot > 0) fewest = std::min(fewest, after.frames); /* the first slot holds the frame that took the pile */
			paintAfter = std::max(paintAfter, after.paintMax);
		}
		/* the pile is appended over several frames (MainWindow::sync, FAST_APPEND_NS), none of it lost */
		const int backlogSyncs = window_.fastSyncsLeftOver() - leftOverBefore;
		const QRegularExpressionMatch afterCounts = QRegularExpression(
				QStringLiteral("(\\d+) samples not shown")).match(sidebar->fastRateTip(0));
		const qint64 notShownAfter = afterCounts.hasMatch() ? afterCounts.captured(1).toLongLong() : -1;
		std::printf("  fast speed, after 700 ms held: the first paint %.1f ms after (a frame %.1f ms); frames in 200 ms "
				"slots: %s; the longest paint %.1f ms; the blocks left for a later frame by %d syncs; %s, %lld not shown\n",
				firstPaint, framePeriod, qPrintable(inSlots), paintAfter, backlogSyncs,
				qPrintable(sidebar->fastLostText(0)), (long long) notShownAfter);
		check(firstPaint <= 2 * framePeriod + 2 && paintAfter <= 40 && fewest >= 6,
				"fast speed: the window's thread held 700 ms (a title bar's button pressed): the chart paints again within "
				"two frames, no paint after it over 40 ms, and the frames go on (at least 6 in each 200 ms)");
		check(backlogSyncs >= 1 && sidebar->fastLostText(0) == QLatin1String("lost 0") && notShownAfter == 0,
				"fast speed: the blocks piled up in the 700 ms are appended over more than one frame (a sync left some for "
				"the next), none lost and none left unshown");
		/* held after a crossing (the trigger on I_LOAD, a 50 Hz sine): at 2.5 s Single holds at once and the records
		 * after T fill the view's right part, only the columns they reach binned at each frame; at 100 ms Normal holds a
		 * view once it is full (a window under a second: steady), the next one binned while it fills behind it */
		const int fastKey0 = ChartView::fastKey(0, 0);
		const double columnsWide = view->lastPlot().width();
		struct Held { int frames = 0; double seconds = 0, columns = 0, paint = 0, paintMax = 0, bin = 0; bool held = false; };
		auto heldFill = [&](double window, ChartView::TriggerMode mode) {
			Held out;
			view->setWindow(window);
			view->setTrigger(fastKey0, 0.0, ChartView::TriggerEdge::Rising, mode);
			const int holdsBefore = view->triggerHolds();
			out.held = QTest::qWaitFor([&] { return view->triggerHolds() > holdsBefore; }, 3000);
			(void) view->takePerfStats();
			QElapsedTimer held;
			held.start();
			if (mode == ChartView::TriggerMode::Single)
				(void) QTest::qWaitFor([&] { return !view->triggerCapturing(); }, 4000);
			else
				QTest::qWait(2000);
			out.seconds = held.nsecsElapsed() / 1e9;
			const ChartView::PerfStats filled = view->takePerfStats();
			out.frames = filled.frames;
			const int frames = std::max(1, filled.frames);
			out.columns = double(filled.fastColumns) / frames;
			out.paint = filled.paintSum / frames;
			out.paintMax = filled.paintMax;
			out.bin = filled.bin / frames;
			view->stopTrigger();
			view->setLive(true);
			return out;
		};
		const Held slowFill = heldFill(2.5, ChartView::TriggerMode::Single);
		const Held shortHeld = heldFill(0.1, ChartView::TriggerMode::Normal);
		for (const auto &[name, h] : { std::pair<const char *, Held>{ "2.5 s, Single, filling", slowFill },
					 std::pair<const char *, Held>{ "100 ms, Normal", shortHeld } })
			std::printf("  fast speed, held after a crossing at %s: %.0f frames a second, fast columns %.1f a frame (the "
					"plot %.0f wide, two lines), paint %.2f ms on average (bin %.2f), at most %.1f ms\n", name,
					h.frames / std::max(0.001, h.seconds), h.columns, columnsWide, h.paint, h.bin, h.paintMax);
		/* the columns of two lines: the view's once at its crossing, the new ones as they come (a fifth more for where a
		 * column's edge falls), the first and the open one of each frame, and the memory strip's whole once a second;
		 * at 100 ms a view's columns a view (one every 100 ms at most), not a view at each frame. The frames and the paint
		 * are printed for the owner's run: they follow the PC's load, the columns do not */
		const auto most = [&](const Held &h, double window) {
			return 2 * columnsWide * (1 + h.seconds * 1.2 / window + std::ceil(h.seconds)) + 8.0 * h.frames;
		};
		check(slowFill.held && shortHeld.held && slowFill.frames > 20 && shortHeld.frames > 20
						&& slowFill.columns * slowFill.frames <= most(slowFill, 2.5)
						&& shortHeld.columns * shortHeld.frames <= most(shortHeld, 0.1),
				"fast speed: held after a crossing, two fast lines of a million records a second: a 2.5 s view filling bins "
				"only the columns its new records reach, not the filled part again at each frame; a 100 ms view re-triggered "
				"bins a view's columns a view, not a view at each frame");
		/* a fast math line over the stream (ADC.I_LOAD * ADC.V_BUS) at a million records a second: its cost on the
		 * window's thread (EVRE_PERF_LOG's "math"), the frames and the paint over 10 s; none of its records lost */
		{
			view->setWindow(10);
			const int mathIndex = int(chartTab->mathLines().lines().size());
			MathLine product;
			product.name = QStringLiteral("P_FAST");
			product.unit = QStringLiteral("W");
			product.formula = QStringLiteral("ADC.I_LOAD * ADC.V_BUS");
			const bool added = chartTab->addMathLine(product);
			QTest::qWait(3000);
			const qint64 nsBefore = chartTab->fastMathNs();
			const fast::Store *math = view->fastStore(MathLines::fastStream(mathIndex));
			const fast::Store *stream = view->fastStore(0);
			const qint64 mathBefore = math ? math->dropped() + math->size() : 0;
			const qint64 streamBefore = stream ? stream->dropped() + stream->size() : 0;
			(void) view->takePerfStats();
			const QString from = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"));
			QTest::qWait(10000);
			const ChartView::PerfStats withMath = view->takePerfStats();
			const double msPerSecond = (chartTab->fastMathNs() - nsBefore) / 1e6 / 10.0;
			const qint64 mathRecords = math ? math->dropped() + math->size() - mathBefore : 0;
			const qint64 streamRecords = stream ? stream->dropped() + stream->size() - streamBefore : 0;
			const double mathPaint = withMath.frames ? withMath.paintSum / withMath.frames : 1e9;
			std::printf("  fast speed, a fast math line (from %s, 10 s): %.1f ms a second on the window's thread computing it "
					"(%lld records, the stream's %lld); %d frames, paint %.2f ms on average, at most %.1f ms\n", qPrintable(from),
					msPerSecond, (long long) mathRecords, (long long) streamRecords, withMath.frames, mathPaint, withMath.paintMax);
			check(added && math && mathRecords > 0 && mathRecords == streamRecords && withMath.frames > 100 && mathPaint <= 8.0
							&& msPerSecond < 250,
					"fast speed: a fast math line over a stream of a million records a second: every record computed, the "
					"chart's paint at most 8 ms a frame on average, its cost under a quarter of the window's thread");
			chartTab->removeMathLine(mathIndex);
		}
		/* recorded at a million records a second beside its CSV, and opened (the owner's look at a 5 min recording) */
		{
			QTemporaryDir recorded;
			const QString csv = recorded.filePath(QStringLiteral("speed.csv"));
			MainWindow::Startup record;
			record.record = csv;
			window_.applyStartup(record);
			QPushButton *stop = nullptr;
			(void) QTest::qWaitFor([&] { return (stop = buttonWithText(QStringLiteral("■  Stop recording"))) != nullptr; }, 3000);
			QTest::qWait(3000);
			if (stop) stop->click();
			(void) QTest::qWaitFor([&] { return !buttonWithText(QStringLiteral("■  Stop recording")); }, 3000);
			RecordingWindow *opened = nullptr;
			RecordingWindow::open(nullptr, csv, {}, 2048, [&](RecordingWindow *w) { opened = w; });
			(void) QTest::qWaitFor([&] { return opened != nullptr; }, 20000);
			recordedFastEnds(opened, csv);
			RecordingWindow::closeAll();
		}
		button->click();
		view->setWindow(windowBefore);
		tabs->setCurrentIndex(MainWindow::TabRegisters);
		MainWindow::Startup example;
		example.map = exampleFile;
		example.tcp = QStringLiteral("127.0.0.1:%1").arg(FAKE_DEVICE_PORT);
		example.connect = true;
		window_.applyStartup(example);
		check(cellShows(valueCell(table_, regs_.u8.name), QString::number(other_.readU8(regs_.u8.addr)), 5000),
				"fast speed: done; the example map again, the window polls the fake device of the other steps");
		fake.kill();
		fake.waitForFinished(3000);
	}

	/* A recent recording whose file was deleted: in every recent list (the sidebar's Open, the chart's Recent recordings)
	 * greyed with "(not found)" and a tooltip that says so; a click takes it off the list at once and the status bar
	 * says so (a disabled entry did nothing); Clear the list at the end empties it */
	void recentMissing() {
		const QVariant before = QSettings().value(QStringLiteral("recording/recent"));
		QTemporaryDir folder;
		const auto makeFile = [&](const QString &name) {
			QFile file(folder.filePath(name));
			if (file.open(QIODevice::WriteOnly | QIODevice::Text))
				file.write("time_s,datetime,V [V]\n0,2026-10-08T12:00:00.000,1\n1,2026-10-08T12:00:01.000,2\n");
			return QFileInfo(file).absoluteFilePath();
		};
		const QString kept = makeFile(QStringLiteral("kept.csv")), gone = makeFile(QStringLiteral("gone.csv"));
		QSettings().remove(QStringLiteral("recording/recent"));
		RecordingWindow::remember(gone);
		RecordingWindow::remember(kept);
		QFile::remove(gone);
		const auto missingIn = [](QMenu *menu) {
			return menu ? menu->findChild<QPushButton *>(QStringLiteral("recentMissing")) : nullptr;
		};
		/* the sidebar's Open */
		auto *open = window_.findChild<QPushButton *>(QStringLiteral("openRecording"));
		QMenu *menu = open ? open->menu() : nullptr;
		bool shown = false, keptEntry = false, clearLast = false;
		QPushButton *missing = nullptr;
		if (menu) {
			menu->popup(open->mapToGlobal(QPoint(0, open->height())));
			QApplication::processEvents();
			missing = missingIn(menu);
			for (QAction *action : menu->actions()) keptEntry = keptEntry || action->text().startsWith(QLatin1String("kept.csv"));
			const QList<QAction *> actions = menu->actions();
			clearLast = actions.size() >= 2 && actions.last()->objectName() == QLatin1String("recentClear")
					&& actions[actions.size() - 2]->isSeparator() && actions.last()->isEnabled();
			shown = missing && missing->text().startsWith(QStringLiteral("gone.csv (not found)"))
					&& missing->toolTip().contains(QLatin1String("not there any more")) && missing->isEnabled()
					&& missing->cursor().shape() == Qt::PointingHandCursor
					&& qApp->styleSheet().contains(QLatin1String("QMenu QPushButton#recentMissing { color: "))
					&& qApp->styleSheet().contains(QLatin1String("QMenu QPushButton#recentMissing:hover"));
			std::printf("  a recent recording deleted: \"%s\", tooltip \"%s\"; the one kept listed %d; Clear the list last %d\n",
					missing ? qPrintable(missing->text()) : "(none)", missing ? qPrintable(missing->toolTip()) : "",
					int(keptEntry), int(clearLast));
			if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) /* for a look: the menu with the entry not found */
				for (const bool dark : { true, false }) {
					menu->hide();
					Theme::apply(*qApp, dark);
					menu->popup(open->mapToGlobal(QPoint(0, open->height())));
					QTest::qWait(300);
					menu->grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_recent_missing_%1.png")
							.arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
				}
			Theme::apply(*qApp, true);
			missing = missingIn(menu);
		}
		check(shown && keptEntry && clearLast, "recent recordings: a file deleted since is listed greyed with \"(not found)\" "
				"after its name, its tooltip says it is not there any more, a pointing hand and a highlight under the mouse; "
				"the file still there is listed as before; Clear the list last, after a line");
		/* the chart's Recent recordings: the same */
		auto *chartTab = window_.findChild<ChartTab *>();
		bool inChartMenu = false;
		if (chartTab) {
			chartTab->showChartMenu(QPoint(100, 100), 0);
			QApplication::processEvents();
			for (QMenu *sub : chartTab->chartMenu() ? chartTab->chartMenu()->findChildren<QMenu *>() : QList<QMenu *>())
				inChartMenu = inChartMenu || missingIn(sub) != nullptr;
			if (chartTab->chartMenu()) chartTab->chartMenu()->hide();
		}
		/* a click: off the list at once, said in the status bar */
		bool removed = false;
		if (missing) {
			window_.statusBar()->clearMessage();
			missing->click();
			QApplication::processEvents();
			removed = RecordingWindow::recentFiles() == QStringList{ kept } && !menu->isVisible()
					&& window_.statusBar()->currentMessage().contains(QLatin1String("gone.csv taken off the recent recordings"));
			std::printf("  clicked: the list \"%s\", the status bar \"%s\"\n",
					qPrintable(RecordingWindow::recentFiles().join(QStringLiteral(", "))),
					qPrintable(window_.statusBar()->currentMessage()));
		}
		check(inChartMenu && removed, "recent recordings: the chart's Recent recordings lists it the same way; a click takes "
				"it off the list at once (saved without it) and the status bar says so");
		/* Clear the list */
		bool cleared = false;
		if (menu) {
			menu->popup(open->mapToGlobal(QPoint(0, open->height())));
			QApplication::processEvents();
			for (QAction *action : menu->actions())
				if (action->objectName() == QLatin1String("recentClear")) action->trigger();
			menu->hide();
			cleared = RecordingWindow::recentFiles().isEmpty()
					&& window_.statusBar()->currentMessage().contains(QLatin1String("list cleared")) && QFileInfo::exists(kept);
		}
		check(cleared, "recent recordings: Clear the list empties it (the files stay), the status bar says so");
		if (before.isValid()) QSettings().setValue(QStringLiteral("recording/recent"), before);
		else QSettings().remove(QStringLiteral("recording/recent"));
	}

	/* The recording window's Lines: a checklist of every line it offers, grouped (Registers, Fast: ADC, Math), each
	 * with its colour dot and unit, All and None, a search from 13 lines, the count on the button ("Lines 8/11"), the
	 * lines unticked kept by name (recording/linesHidden) for the next recording opened */
	struct LinesList {
		QMenu *menu = nullptr;
		QWidget *panel = nullptr;
		QList<QCheckBox *> boxes;
		QStringList groups;
		QPushButton *all = nullptr, *none = nullptr;
		QLineEdit *search = nullptr;
	};
	static LinesList openLinesList(RecordingWindow *w) {
		LinesList out;
		auto *button = w ? w->findChild<QPushButton *>(QStringLiteral("recordingLines")) : nullptr;
		out.menu = button ? button->menu() : nullptr;
		if (!out.menu) return out;
		out.menu->popup(button->mapToGlobal(QPoint(0, button->height())));
		QApplication::processEvents();
		out.panel = out.menu->findChild<QWidget *>(QStringLiteral("recordingLinesList"));
		if (!out.panel) return out;
		out.boxes = out.panel->findChildren<QCheckBox *>(QStringLiteral("recordingLine"));
		for (QLabel *label : out.panel->findChildren<QLabel *>(QStringLiteral("linesGroup"))) out.groups << label->text();
		out.all = out.panel->findChild<QPushButton *>(QStringLiteral("recordingLinesAll"));
		out.none = out.panel->findChild<QPushButton *>(QStringLiteral("recordingLinesNone"));
		out.search = out.panel->findChild<QLineEdit *>(QStringLiteral("recordingLinesSearch"));
		return out;
	}
	void recordingLinesList(RecordingWindow *w, const QString &folder) {
		const QVariant hiddenBefore = QSettings().value(QStringLiteral("recording/linesHidden"));
		auto *button = w ? w->findChild<QPushButton *>(QStringLiteral("recordingLines")) : nullptr;
		LinesList list = openLinesList(w);
		const int total = w ? int(w->definitions().size()) + 2 + int(w->chartTab()->mathLines().lines().size()) : 0;
		bool grouped = false, dotted = !list.boxes.isEmpty(), hands = list.all && list.none, counted = false;
		if (w && list.panel) {
			QStringList names;
			for (QCheckBox *box : std::as_const(list.boxes)) {
				names << box->text();
				dotted = dotted && !box->icon().isNull();
				hands = hands && box->cursor().shape() == Qt::PointingHandCursor && !box->toolTip().isEmpty();
			}
			hands = hands && list.all->cursor().shape() == Qt::PointingHandCursor && !list.all->toolTip().isEmpty()
					&& list.none->cursor().shape() == Qt::PointingHandCursor && !list.none->toolTip().isEmpty();
			grouped = list.groups == QStringList{ QStringLiteral("Registers"), QStringLiteral("Fast: ADC"), QStringLiteral("Math") }
					&& list.boxes.size() == total && names.contains(QStringLiteral("ADC.I_LOAD [A]"))
					&& names.contains(QStringLiteral("ADC.V_BUS [V]")) && names.contains(QStringLiteral("P [W]"));
			int ticked = 0;
			for (QCheckBox *box : std::as_const(list.boxes)) ticked += box->isChecked();
			counted = button->text() == QStringLiteral("Lines %1/%2").arg(w->chartTab()->lineCount()).arg(total)
					&& ticked == w->chartTab()->lineCount();
			std::printf("  the Lines list: groups %s, %lld lines (%s), the button \"%s\"\n",
					qPrintable(list.groups.join(QStringLiteral(", "))), (long long) list.boxes.size(),
					qPrintable(names.join(QStringLiteral(", "))), qPrintable(button->text()));
		}
		check(grouped && dotted && counted, "recording window, Lines: a checklist of every line, grouped Registers, Fast: "
				"ADC, Math, each with its dot and unit (ADC.I_LOAD [A], P [W]); the button counts them, \"Lines 11/11\"");
		check(hands, "recording window, Lines: All, None and every line's tick have a pointing hand and a tooltip");
		/* None, then All */
		bool noneAll = false;
		if (list.none && list.all) {
			list.none->click();
			const bool allOff = w->chartTab()->lineCount() == 0 && button->text() == QStringLiteral("Lines 0/%1").arg(total);
			list.all->click();
			bool allOn = w->chartTab()->lineCount() == total && button->text() == QStringLiteral("Lines %1/%1").arg(total);
			for (QCheckBox *box : std::as_const(list.boxes)) allOn = allOn && box->isChecked();
			noneAll = allOff && allOn && list.search && list.search->isHidden(); /* 11 lines: no search */
			if (!noneAll) std::printf("     (None: %d, All: %d, the search hidden %d, \"%s\")\n", int(allOff), int(allOn),
					list.search ? int(list.search->isHidden()) : -1, qPrintable(button->text()));
		}
		check(noneAll, "recording window, Lines: None takes every line off the chart (\"Lines 0/11\"), All puts them back; "
				"no search box under 13 lines");
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT") && list.menu) { /* for a look: the list open, both themes */
			for (const bool dark : { true, false }) {
				list.menu->hide();
				Theme::apply(*qApp, dark);
				list = openLinesList(w);
				QTest::qWait(300);
				if (list.menu)
					list.menu->grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_lines_%1.png")
							.arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
			}
			Theme::apply(*qApp, true);
		}
		if (list.menu) list.menu->hide();
		/* many lines: the search, All and None on what it finds, kept for the next recording */
		const QString many = folder + QStringLiteral("/many.csv");
		{
			QFile file(many);
			if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
				QString text = QStringLiteral("time_s,datetime");
				for (int k = 0; k < 14; k++) text += QStringLiteral(",L%1 [V]").arg(k);
				text += QLatin1Char('\n');
				for (int row = 0; row < 3; row++) {
					text += QStringLiteral("%1,2026-10-08T12:00:0%1.000").arg(row);
					for (int k = 0; k < 14; k++) text += QStringLiteral(",%1").arg(k + row);
					text += QLatin1Char('\n');
				}
				file.write(text.toUtf8());
			}
		}
		RecordingWindow *wide = nullptr;
		RecordingWindow::open(nullptr, many, {}, 2048, [&](RecordingWindow *x) { wide = x; });
		(void) QTest::qWaitFor([&] { return wide != nullptr; }, 5000);
		LinesList more = openLinesList(wide);
		bool searched = false;
		int found = -1, nothing = -1;
		bool titleHidden = false;
		if (more.search && more.none) {
			more.search->setText(QStringLiteral("l1"));
			found = 0;
			for (QCheckBox *box : std::as_const(more.boxes)) found += !box->isHidden();
			more.none->click();
			more.search->setText(QStringLiteral("zzz"));
			nothing = 0;
			for (QCheckBox *box : std::as_const(more.boxes)) nothing += !box->isHidden();
			titleHidden = true;
			for (QLabel *label : more.panel->findChildren<QLabel *>(QStringLiteral("linesGroup")))
				titleHidden = titleHidden && label->isHidden();
			/* the recording's own math lines (recording/math) are listed too, and on the chart */
			const int math = int(wide->chartTab()->mathLines().lines().size());
			const int mathShown = wide->chartTab()->mathLinesShown();
			const QString text = wide->findChild<QPushButton *>(QStringLiteral("recordingLines"))->text();
			searched = !more.search->isHidden() && found == 5 && nothing == 0 && titleHidden
					&& wide->chartTab()->lineCount() == 9 + mathShown
					&& text == QStringLiteral("Lines %1/%2").arg(9 + mathShown).arg(14 + math);
			if (!searched)
				std::printf("     (the search shown %d, the button \"%s\", math lines %d, %d shown)\n",
						int(!more.search->isHidden()), qPrintable(text), math, mathShown);
			more.menu->hide();
		}
		std::printf("  14 lines: the search \"l1\" lists %d, \"zzz\" %d (the group's title hidden %d); None on the 5: %d "
				"on the chart\n", found, nothing, int(titleHidden), wide ? wide->chartTab()->lineCount() : -1);
		check(searched, "recording window, Lines with 14 lines: a search box; \"l1\" lists L1 and L10 to L13 (case does not "
				"matter), a search that finds nothing hides the group's title too; None takes only the lines found off "
				"(\"Lines 9/14\")");
		delete wide;
		const QStringList hidden = QSettings().value(QStringLiteral("recording/linesHidden")).toStringList();
		RecordingWindow *again = nullptr;
		RecordingWindow::open(nullptr, many, {}, 2048, [&](RecordingWindow *x) { again = x; });
		(void) QTest::qWaitFor([&] { return again != nullptr; }, 5000);
		bool kept = again && hidden.contains(QStringLiteral("L13")) && !hidden.contains(QStringLiteral("L2"))
				&& again->chartTab()->lineCount() == 9 + again->chartTab()->mathLinesShown();
		if (again)
			for (const ChartView::Info &line : again->chartTab()->view()->lines())
				kept = kept && !line.name.startsWith(QLatin1String("L1"));
		std::printf("  kept: recording/linesHidden \"%s\", opened again with %d lines\n",
				qPrintable(hidden.join(QStringLiteral(", "))), again ? again->chartTab()->lineCount() : -1);
		check(kept, "recording window, Lines: the lines unticked are kept by name (recording/linesHidden): the recording "
				"opened again shows the 9 others");
		delete again;
		if (hiddenBefore.isValid()) QSettings().setValue(QStringLiteral("recording/linesHidden"), hiddenBefore);
		else QSettings().remove(QStringLiteral("recording/linesHidden"));
	}

	/* A recording of a fast stream beside its CSV, opened (the owner's 5 min file: at a 10 ms window at its end the fast
	 * lines stopped short of the view's end, the polled lines ran to it, UPTIME jumped at the very end, "11 fps"):
	 *  - where each ends: the CSV's rows at each poll, the stream's blocks as they come, so the two stop apart by a
	 *    few ms, either way; the line above the chart says so when the stream ends first, its tooltip gives both spans;
	 *  - the fast line drawn up to its record at (or just past) the view's end at every window, 1 ms to the whole file,
	 *    at its end, its start and its middle: within a column (a column's records are drawn at its middle);
	 *  - Normalise at 10 ms at the CSV's end: a register polled every 10 ms or slower has one sample there; its range
	 *    takes its value at the view's edge too, so the piece from the edge to that sample is drawn in the plot, not
	 *    from far below it;
	 *  - held and still it paints nothing, and the info line says "idle", not the frames of its last change */
	void recordedFastEnds(RecordingWindow *w, const QString &csv) {
		check(w && !w->fastRecordings().isEmpty() && w->fastRecordings()[0].store
						&& w->fastRecordings()[0].store->size() > 1000000,
				"fast recording at a million records a second: recorded beside its CSV and opened with it");
		if (!w || w->fastRecordings().isEmpty() || !w->fastRecordings()[0].store) return;
		(void) QTest::qWaitForWindowExposed(w);
		w->resize(1400, 800);
		QTest::qWait(300);
		ChartView *v = w->chartTab()->view();
		const fast::Store &store = *w->fastRecordings()[0].store;
		double rowsEnd = NAN;
		{
			QFile file(csv);
			if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
				const QList<QByteArray> rows = file.readAll().trimmed().split('\n');
				rowsEnd = rows.isEmpty() ? NAN : rows.last().split(',').value(0).toDouble();
			}
		}
		const double fastEnd = w->fastRecordings()[0].lastTime;
		const auto *info = w->findChild<QLabel *>(QStringLiteral("recordingInfo"));
		const QString text = info ? info->text() : QString(), tip = info ? info->toolTip() : QString();
		const bool said = rowsEnd - fastEnd >= 0.001 ? text.contains(QLatin1String("before the CSV's last row"))
													  : !text.contains(QLatin1String("before the CSV's last row"));
		std::printf("  recorded at 1 M/s: the CSV's last row %.6f s, the stream's last record %.6f s (the rows end %+.2f ms "
				"after it); the line \"%s\"\n", rowsEnd, fastEnd, (rowsEnd - fastEnd) * 1000, qPrintable(text));
		check(said && std::fabs(w->lastTime() - std::max(rowsEnd, fastEnd)) < 1e-9 && tip.contains(QLatin1String("The CSV's rows: "))
						&& tip.contains(QLatin1String("ADC's samples (speed.ADC.evrs): ")),
				"fast recording: the CSV's rows and the stream's samples end a few ms apart (each written as it comes); the "
				"view ends at the later, the line above the chart says when the stream ends first, its tooltip both spans");
		/* the fast line's last point against its record at the view's end, at every window and place */
		const int key = ChartView::fastKey(0, 0);
		const double first = w->firstTime(), last = w->lastTime();
		int wrong = 0, tried = 0;
		QString worst;
		double worstColumns = 0;
		for (const double asked : { 0.001, 0.01, 0.1, 1.0, 10.0, last - first }) {
			const double window = std::min(asked, last - first); /* a longer one would grow the memory */
			for (int place = 0; place < 3; place++) {
				const double end = place == 0 ? last : place == 1 ? first + window : (first + last + window) / 2;
				w->chartTab()->showSpan(end - window, end);
				v->repaint();
				const double t0 = v->lastViewStart(), t1 = v->lastViewEnd();
				const double column = (t1 - t0) / std::max(1.0, v->lastPlot().width());
				const qsizetype at = std::min(store.size() - 1, store.upperBound(t1));
				const double drawn = v->drawnTo(key), off = std::fabs(drawn - store.timeAt(at)) / column;
				tried++;
				if (!(off <= 1.0)) wrong++;
				if (!(off <= worstColumns)) {
					worstColumns = std::isfinite(off) ? off : 1e9;
					worst = QStringLiteral("%1 s at the %2: drawn to %3, its record %4").arg(window).arg(place == 0 ? "end"
							: place == 1 ? "start" : "middle").arg(drawn, 0, 'f', 6).arg(store.timeAt(at), 0, 'f', 6);
				}
			}
		}
		std::printf("  the fast line's last point: %d of %d views within a column of its record at the view's end; the "
				"furthest %.2f columns (%s)\n", tried - wrong, tried, worstColumns, qPrintable(worst));
		check(tried == 18 && wrong == 0, "fast recording: the fast line drawn up to its record at the view's end at every "
				"window (1 ms, 10 ms, 100 ms, 1 s, 10 s, the whole file), at the file's end, start and middle");
		/* Normalise at 10 ms at the CSV's end */
		auto *normalise = w->findChild<QAction *>(QStringLiteral("chartNormalise"));
		int uptime = -1;
		for (const RegDef &def : w->definitions())
			if (def.name == QLatin1String("UPTIME")) uptime = int(regKey(def));
		bool inRange = false;
		double atEdge = NAN, lo = NAN, hi = NAN;
		if (normalise && uptime >= 0) {
			normalise->setChecked(true);
			w->chartTab()->showSpan(rowsEnd - 0.01, rowsEnd); /* its last sample at the view's end */
			v->repaint();
			QVector<double> times, values;
			const double t0 = v->lastViewStart();
			if (v->lineSamples(uptime, t0 - 1, rowsEnd + 1, times, values) && v->drawnRange(uptime, lo, hi)) {
				const qsizetype k = std::lower_bound(times.begin(), times.end(), t0) - times.begin();
				if (k > 0 && k < times.size())
					atEdge = values[k - 1] + (values[k] - values[k - 1]) * (t0 - times[k - 1]) / (times[k] - times[k - 1]);
				inRange = std::isfinite(atEdge) && atEdge >= lo - 1e-6 && atEdge <= hi + 1e-6;
			}
		}
		std::printf("  Normalise, 10 ms at the CSV's end: UPTIME %.3f at the view's left edge, its range %.3f .. %.3f\n", atEdge, lo,
				hi);
		check(inRange, "fast recording, Normalise at 10 ms at the CSV's end: a register with one sample in view (UPTIME) "
				"is ranged with its value at the view's edge, so the piece drawn to its sample stays in the plot (it came "
				"from far below it: a jump at the end that is not in the data)");
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* for a look: the fast lines at 10 ms at the file's end */
			w->chartTab()->showSpan(last - 0.01, last);
			for (const bool dark : { true, false }) {
				Theme::apply(*qApp, dark);
				QTest::qWait(300);
				w->grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_viewer_end10ms_%1.png")
						.arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
			}
			Theme::apply(*qApp, true);
		}
		if (normalise) normalise->setChecked(false);
		/* held and still: no paint, the info line says idle; painted, the frames again */
		v->repaint();
		const int paints = v->paints(), binnings = v->binnings();
		QTest::qWait(1500);
		w->chartTab()->refreshStatus();
		const int idlePaints = v->paints() - paints;
		const QString idle = w->chartTab()->infoText();
		for (int i = 0; i < 10; i++) v->repaint();
		const QString painting = w->chartTab()->infoText();
		std::printf("  held and still for 1.5 s: %d paints, \"%s\"; 10 paints later \"%s\", %d binnings\n", idlePaints,
				qPrintable(idle), qPrintable(painting), v->binnings() - binnings);
		check(idlePaints == 0 && idle.contains(QStringLiteral(" · idle")) && !idle.contains(QLatin1String(" fps"))
						&& painting.contains(QLatin1String(" fps")) && v->binnings() == binnings,
				"fast recording, a held 10 ms view of it: nothing painted while nothing changes and the info line says "
				"\"idle\" (not the frames of its last change); painted again, its frames counted, its lines reused (no "
				"binning)");
	}

	/* the I/O thread's table, given the map again (a device picked, a map edited): a register keeps its value only
	 * if it is the same device's. On a bus D2's register at D1's address took D1's value until its next poll, and an
	 * edit open on it then asked "Value changed while editing" (22 when started, 11 "now on the device") */
	void valuesKeptByDevice() {
		RegDef one;
		one.name = QStringLiteral("D1_U8");
		one.addr = 0xD010;
		one.type = RegType::U8;
		one.size = 1;
		one.slave = 1;
		RegDef two = one;
		two.name = QStringLiteral("D2_U8");
		two.slave = 2;
		RegTable table;
		table.setDefs({ one, two }, 1);
		table.setRaw(0, QByteArray(1, char(11)));
		table.setRaw(1, QByteArray(1, char(22)));
		table.setDefs({ one, two }, 2); /* the same map again: each keeps its own */
		const bool same = table.rows()[0].raw == QByteArray(1, char(11)) && table.rows()[1].raw == QByteArray(1, char(22));
		RegTable fresh;
		fresh.setDefs({ one }, 1);
		fresh.setRaw(0, QByteArray(1, char(11)));
		fresh.setDefs({ one, two }, 2); /* D2 added: nothing read of it yet */
		const bool notTaken = fresh.rows()[0].valid && !fresh.rows()[1].valid;
		check(same && notTaken, "bus: given the map again, a register keeps its own device's value, never another's "
				"at the same address");
	}

	/* Several devices on one link (a bus file): evre_fake_fast serves this map at slaves 1 and 2 on a port of its own
	 * (and, in the second bus, another map at slave 2 and nothing at slave 9). The table holds every device's
	 * registers named after it and shows the selected one; a write reaches only its device; a broadcast from the
	 * Monitor reaches every device, and is refused when the maps differ, except into the reserved bank; a device
	 * that never answers goes offline while the others keep being polled; Close bus gives one device again. */
	void busDevices() {
		valuesKeptByDevice();
		QTemporaryDir folder;
		const QString mapFile = map_.path;
		DeviceMap otherKind = map_; /* another kind of device: another DEVICE_ID */
		otherKind.deviceId = uint16_t(map_.deviceId ^ 0x0100);
		const QString otherFile = folder.filePath(QStringLiteral("other_kind.json"));
		QString error;
		auto writeBus = [&](const QString &name, const QVector<QPair<int, QString>> &devices) {
			BusFile bus;
			for (const auto &d : devices) {
				BusDevice device;
				device.name = QStringLiteral("D%1").arg(d.first);
				device.slave = uint8_t(d.first);
				device.map = d.second;
				bus.devices << device;
			}
			const QString file = folder.filePath(name);
			return bus.save(file, error) ? file : QString();
		};
		const QString sameMaps = writeBus(QStringLiteral("same.json"), { { 1, mapFile }, { 2, mapFile } });
		const QString mixed = writeBus(QStringLiteral("mixed.json"), { { 1, mapFile }, { 2, otherFile }, { 9, mapFile } });
		QProcess fake;
		fake.start(QCoreApplication::applicationDirPath() + QStringLiteral("/evre_fake_fast"),
				{ QString::number(FAKE_BUS_PORT), mapFile, QString::fromLatin1(fakeDeviceToken), QStringLiteral("--slave"),
						QStringLiteral("1"), QStringLiteral("--node"), QStringLiteral("2=") + mapFile });
		OtherClient one, two;
		const bool started = folder.isValid() && otherKind.save(otherFile, error) && !sameMaps.isEmpty()
				&& !mixed.isEmpty() && fake.waitForStarted(3000)
				&& QTest::qWaitFor([&] { return one.open(FAKE_BUS_PORT, 1); }, 5000) && two.open(FAKE_BUS_PORT, 2);
		check(started, "bus: the fake devices (evre_fake_fast, slaves 1 and 2) started");
		if (!started) return;
		const QString u8 = regs_.u8.name, d1u8 = QStringLiteral("D1_") + u8, d2u8 = QStringLiteral("D2_") + u8;
		one.writeU8(regs_.u8.addr, 11);
		two.writeU8(regs_.u8.addr, 22);
		const bool apart = one.readU8(regs_.u8.addr) == 11 && two.readU8(regs_.u8.addr) == 22;

		auto *doc = window_.findChild<MapDocument *>();
		auto *model = window_.findChild<RegisterModel *>();
		auto *devices = window_.findChild<QListWidget *>(QStringLiteral("busDevices"));
		auto open = [&](const QString &busFile) {
			MainWindow::Startup startup;
			startup.tcp = QStringLiteral("127.0.0.1:%1").arg(FAKE_BUS_PORT);
			startup.bus = busFile;
			startup.connect = true;
			/* an unsaved map or bus is asked about first (Discard); with nothing unsaved no question comes */
			answerDialog(QStringLiteral("Discard"), [&] { window_.applyStartup(startup); });
		};
		/* Auto send ticked (as the device's state would tick it) before the bus: a bus switches it off */
		auto *autoSendBox = window_.findChild<QCheckBox *>(QStringLiteral("autoSend"));
		auto *sidebar = window_.findChild<Sidebar *>();
		if (sidebar) sidebar->setAutoSendOn(true);
		open(sameMaps);
		/* the line under the pill: there from the start ("not read yet"), two lines tall, D1's */
		auto *deviceInfo = window_.findChild<QLabel *>(QStringLiteral("deviceInfo"));
		const bool infoAtOpen = deviceInfo && deviceInfo->isVisible() && deviceInfo->text().startsWith(QLatin1String("D1: "))
				&& deviceInfo->minimumHeight() >= 2 * deviceInfo->fontMetrics().lineSpacing();
		const int infoHeight = deviceInfo ? deviceInfo->height() : -1;
		const int perDevice = int(map_.regs.size());
		auto names = [&] {
			QStringList out;
			for (const RegisterModel::Row &row : model->rows()) out << row.def.name;
			return out;
		};
		check(apart && devices && devices->count() == 2 && model->rows().size() == 2 * perDevice
						&& names().contains(d1u8) && names().contains(d2u8) && !names().contains(u8)
						&& table_->model()->rowCount() == perDevice,
				"bus: every device's registers, named after it (D1_, D2_); the table shows the selected device");
		auto *autoSendRate = window_.findChild<QComboBox *>(QStringLiteral("autoSendRate"));
		check(autoSendBox && !autoSendBox->isEnabled() && !autoSendBox->isChecked()
						&& autoSendBox->toolTip().contains(QLatin1String("collide")) && autoSendRate
						&& autoSendRate->placeholderText() == QLatin1String("not on a bus") && autoSendRate->currentIndex() < 0,
				"bus: Auto send is switched off and disabled, the tooltip says why (devices sending by themselves would "
				"collide)");
		check(cellShows(valueCell(table_, d1u8), QStringLiteral("11"), 5000) && !valueCell(table_, d2u8).isValid(),
				"bus: D1 selected: its values, polled from slave 1");
		const bool d1Read = deviceInfo && QTest::qWaitFor([&] {
			return deviceInfo->text().startsWith(QLatin1String("D1: ")) && deviceInfo->text().contains(QLatin1String("protocol rev"));
		}, 3000);
		if (devices) devices->setCurrentRow(1);
		check(cellShows(valueCell(table_, d2u8), QStringLiteral("22"), 5000) && !valueCell(table_, d1u8).isValid(),
				"bus: D2 selected in the Devices card: the table shows D2's registers, from slave 2");
		/* D2's ID comes from its own read after the switch: waited for, not taken at once (a slow run failed here) */
		const bool d2Read = deviceInfo && QTest::qWaitFor([&] {
			return deviceInfo->text().startsWith(QLatin1String("D2: ")) && deviceInfo->text().contains(QLatin1String("protocol rev"));
		}, 3000);
		check(infoAtOpen && d1Read && d2Read && deviceInfo->height() == infoHeight,
				"bus: the device's ID under the pill: there from the start, D1's, then D2's; the card keeps its height");
		/* the Map editor says whose map it is, and whose live values it shows */
		auto *banner = window_.findChild<QWidget *>(QStringLiteral("mapDevices"));
		auto *live = window_.findChild<QComboBox *>(QStringLiteral("liveDevice"));
		auto *bannerLabel = banner ? banner->findChild<ElidedLabel *>(QStringLiteral("mapDevicesText")) : nullptr;
		const QString bannerText = bannerLabel ? bannerLabel->fullText() : QString();
		check(banner && !banner->isHidden() && bannerText.contains(QLatin1String("D1, D2"))
						&& bannerText.contains(QLatin1String("all 2 devices")) && live && live->count() == 2
						&& live->currentData().toInt() == 2,
				"bus: the Map editor names the devices of the map it edits; live values from the selected one (D2)");
		/* one line that never pushes: cut to its room, the whole of it in the tooltip */
		ElidedLabel narrow;
		narrow.setFixedWidth(60);
		narrow.setFullText(QStringLiteral("Map of <b>D1, D2, D3 and 9 more</b> · example_device.json"));
		check(bannerLabel && !bannerLabel->wordWrap() && bannerLabel->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored
						&& narrow.isCut() && narrow.toolTip() == QStringLiteral("Map of D1, D2, D3 and 9 more · example_device.json"),
				"bus: the Map editor's banner is one line, cut to its room; the tooltip has it whole");
		/* the Registers tab's picker: every device at once */
		auto *picker = window_.findChild<QComboBox *>(QStringLiteral("registersDevice"));
		if (picker) {
			picker->setCurrentIndex(picker->findData(-1));
			emit picker->activated(picker->currentIndex());
		}
		const bool all = picker && table_->model()->rowCount() == 2 * perDevice && valueCell(table_, d1u8).isValid();
		if (picker) {
			picker->setCurrentIndex(picker->findData(2));
			emit picker->activated(picker->currentIndex());
		}
		check(all && table_->model()->rowCount() == perDevice && !valueCell(table_, d1u8).isValid(),
				"bus: the Registers tab's picker: All devices shows every device's registers, D2 its own again");
		/* the pickers look alike, as the Devices card: the state's dot, "D2 · slave 2", the state as the row's tooltip */
		auto pickerRow = [](const QComboBox *box, int slave) {
			const int row = box ? box->findData(slave) : -1;
			if (row < 0) return QString();
			return box->itemText(row) + QLatin1Char('|') + box->itemData(row, Qt::ToolTipRole).toString()
					+ (box->itemIcon(row).isNull() ? QStringLiteral("|no dot") : QString());
		};
		auto *monitorDevice = window_.findChild<QComboBox *>(QStringLiteral("monitorDevice"));
		const QString d2Row = QStringLiteral("D2 · slave 2|answers");
		check(pickerRow(picker, 2) == d2Row && pickerRow(live, 2) == d2Row && pickerRow(monitorDevice, 2) == d2Row
						&& monitorDevice->itemData(monitorDevice->count() - 1).toInt() == evre::BROADCAST,
				"bus: the device pickers (Registers, Map editor, Monitor) show a device alike: its dot, D2 · slave 2, "
				"its state");
		/* one width whatever the names: a long one is cut (its slave never), the row's tooltip has it whole */
		{
			QComboBox box;
			fillDevicePicker(&box, { { QStringLiteral("D3"), 3, Theme::colors().good, QStringLiteral("answers") } }, 3);
			const int width = box.width();
			const QString longName = QStringLiteral("THE_LONGEST_DEVICE_NAME_ON_THIS_LINK");
			fillDevicePicker(&box, { { longName, 3, Theme::colors().good, QStringLiteral("answers") } }, 3);
			check(box.width() == width && box.minimumWidth() == box.maximumWidth()
							&& box.itemText(0).endsWith(QStringLiteral("… · slave 3"))
							&& box.itemData(0, Qt::ToolTipRole).toString().startsWith(longName),
					"bus: a device picker keeps its width; a long name is cut before \" · slave 3\", whole in the tooltip");
		}
		QStringList twelve;
		for (int i = 1; i <= 12; i++) twelve << QStringLiteral("D%1").arg(i);
		check(BusPanel::namesText(twelve.mid(0, 4)) == QLatin1String("D1, D2, D3, D4")
						&& BusPanel::namesText(twelve) == QLatin1String("D1, D2, D3 and 9 more"),
				"bus: many devices are named in short (the Map editor's banner on one line), four in full");

		allowWrites_->setChecked(true);
		QLineEdit *editor = typeInto(valueCell(table_, d2u8), QStringLiteral("33"));
		if (editor) QTest::keyClick(editor, Qt::Key_Return);
		const bool wrote = QTest::qWaitFor([&] { return two.readU8(regs_.u8.addr) == 33; }, 3000);
		check(wrote && one.readU8(regs_.u8.addr) == 11, "bus: a write to D2_ reaches slave 2 only");
		quickWriteToAll(d2u8, one, two);
		devicesCard(devices, model, d1u8, d2u8, one, two, folder.path());

		/* the API: any device's register by its name, no slave given; the pass-through to the slave a frame names */
		QCheckBox *serve = nullptr;
		for (QCheckBox *box : window_.findChildren<QCheckBox *>())
			if (box->text() == QLatin1String("Serve API")) serve = box;
		if (serve) serve->setChecked(true);
		QTcpSocket json, evreClient;
		const bool served = serve && QTest::qWaitFor([&] {
			json.abort();
			json.connectToHost(QStringLiteral("127.0.0.1"), 1220);
			return json.waitForConnected(200);
		}, 3000) && (evreClient.connectToHost(QStringLiteral("127.0.0.1"), 1219), evreClient.waitForConnected(2000));
		QByteArray reply;
		if (served) {
			json.write(QStringLiteral("{\"cmd\":\"get\",\"names\":[\"%1\",\"%2\"]}\n").arg(d1u8, d2u8).toUtf8());
			(void) QTest::qWaitFor([&] {
				if (json.waitForReadyRead(50)) reply += json.readAll();
				return reply.contains('\n');
			}, 3000);
		}
		const QJsonObject values = QJsonDocument::fromJson(reply).object().value(QStringLiteral("values")).toObject();
		check(values.value(d1u8).toInt(-1) == 11 && values.value(d2u8).toInt(-1) == 33,
				"bus: the JSON API reads D1_ and D2_ registers by name, each from its device");
		/* the JSON broadcast: refused while API writes are off, then to both devices, read back from each */
		auto jsonLine = [&](const QString &line) {
			QByteArray answer;
			json.write(line.toUtf8() + '\n');
			(void) QTest::qWaitFor([&] {
				if (json.waitForReadyRead(50)) answer += json.readAll();
				return answer.contains('\n');
			}, 3000);
			return QJsonDocument::fromJson(answer).object();
		};
		const QString broadcastLine = QStringLiteral("{\"cmd\":\"broadcast\",\"name\":\"%1\",\"value\":77}").arg(d1u8);
		const bool refusedOff = served && !jsonLine(broadcastLine).value(QStringLiteral("ok")).toBool();
		QCheckBox *apiWrites = nullptr;
		for (QCheckBox *box : window_.findChildren<QCheckBox *>())
			if (box->text() == QLatin1String("Allow API writes")) apiWrites = box;
		if (apiWrites) apiWrites->setChecked(true);
		QTest::qWait(100);
		const QJsonObject sent = served ? jsonLine(broadcastLine) : QJsonObject();
		const QJsonObject after = sent.value(QStringLiteral("values")).toObject();
		check(refusedOff && sent.value(QStringLiteral("ok")).toBool() && after.value(d1u8).toInt() == 77
						&& after.value(d2u8).toInt() == 77 && one.readU8(regs_.u8.addr) == 77 && two.readU8(regs_.u8.addr) == 77,
				"bus: the JSON broadcast (off without Allow API writes) reaches every device and reads each one back");
		if (apiWrites) apiWrites->setChecked(false);
		two.writeU8(regs_.u8.addr, 33);
		one.writeU8(regs_.u8.addr, 11);
		QTest::qWait(300);
		auto passRead = [&](uint8_t slave) {
			evreClient.write(evre::build(slave, evre::READ, regs_.u8.addr, 1));
			evre::Parser parser;
			evre::Frame frame;
			for (int i = 0; i < 30; i++) {
				if (evreClient.waitForReadyRead(100)) parser.feed(evreClient.readAll());
				if (parser.next(frame)) return frame.slave == slave && frame.data.size() == 1 ? int(uint8_t(frame.data[0])) : -1;
			}
			return -1;
		};
		check(served && passRead(1) == 11 && passRead(2) == 33,
				"bus: the EVRe pass-through sends a frame to the slave it names, and answers as that slave");
		if (serve) serve->setChecked(false);

		MathLine both;
		both.formula = QStringLiteral("D1_%1 + D2_%1").arg(regs_.volts.name);
		const bool compiled = both.compile(model->definitions());
		check(compiled && both.inputs.size() == 2 && both.inputs[0] != both.inputs[1],
				"bus: a math line may read registers of two devices (D1_V + D2_V)");

		/* the Monitor: slave 0 is a broadcast (a WRITE, no acknowledge); both devices take it */
		auto *monitor = window_.findChild<MonitorTab *>();
		auto *slaveBox = monitor ? monitor->findChild<QSpinBox *>(QStringLiteral("monitorSlave")) : nullptr;
		auto *deviceBox = monitor ? monitor->findChild<QComboBox *>(QStringLiteral("monitorDevice")) : nullptr;
		auto *function = monitor ? monitor->findChild<QComboBox *>(QStringLiteral("monitorFunction")) : nullptr;
		QList<QLineEdit *> boxes = monitor ? monitor->findChildren<QLineEdit *>() : QList<QLineEdit *>();
		boxes.removeIf([](const QLineEdit *box) { return qobject_cast<QAbstractSpinBox *>(box->parent()) != nullptr; });
		QPushButton *send = monitor ? buttonWithText(*monitor, QStringLiteral("Send")) : nullptr;
		auto *frames = monitor ? monitor->findChild<QPlainTextEdit *>() : nullptr;
		const bool monitorFound = slaveBox && function && deviceBox && boxes.size() >= 2 && send && frames;
		check(monitorFound && slaveBox->value() == 2 && deviceBox->currentData().toInt() == 2
						&& deviceBox->isVisibleTo(monitor) && !slaveBox->isVisibleTo(monitor),
				"bus: the Monitor names the devices in place of the Slave number, the selected one (D2) chosen");
		if (!monitorFound) return;
		auto broadcast = [&](uint16_t addr, const QString &bytes) {
			deviceBox->setCurrentIndex(deviceBox->findData(int(evre::BROADCAST)));
			emit deviceBox->activated(deviceBox->currentIndex());
			boxes[0]->setText(addrText(addr));
			boxes[1]->setText(bytes);
			frames->clear();
			send->click();
		};
		broadcast(regs_.u8.addr, QStringLiteral("2C"));
		const int sentTo = slaveBox->value();
		const bool locked = function->currentData().toInt() == int(evre::WRITE) && !function->isEnabled();
		const bool both2C = QTest::qWaitFor([&] {
			return one.readU8(regs_.u8.addr) == 0x2C && two.readU8(regs_.u8.addr) == 0x2C;
		}, 3000);
		/* the Monitor writes its "sent" line when the link reports the frame, which can come after the devices took
		 * it: waited for, not read at once */
		const bool monitorSent = QTest::qWaitFor([&] { return frames->toPlainText().contains(QLatin1String("sent")); },
				3000);
		/* it failed once in many runs: say which part, so the next failure tells why */
		if (!(locked && both2C && monitorSent))
			std::printf("     detail: slave %d, function locked %d, D1 0x%02X, D2 0x%02X, Monitor: %s\n", sentTo, int(locked),
					one.readU8(regs_.u8.addr), two.readU8(regs_.u8.addr), qPrintable(frames->toPlainText().simplified()));
		check(locked && both2C && monitorSent,
				"bus: a broadcast from the Monitor (slave 0, WRITE only) reaches every device; none answers");

		/* a broadcast kept in the bus: made in its dialog, then sent from the Broadcast menu */
		auto *presetButton = window_.findChild<QPushButton *>(QStringLiteral("busBroadcast"));
		auto menuAction = [&](const QString &text) -> QAction * {
			for (QAction *action : presetButton && presetButton->menu() ? presetButton->menu()->actions() : QList<QAction *>())
				if (action->text().startsWith(text)) return action;
			return nullptr;
		};
		QAction *newPreset = menuAction(QStringLiteral("New broadcast"));
		bool calmAtOpen = false, wrongValueSaid = false;
		QTimer::singleShot(300, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog) return;
			/* nothing typed yet: no red text, OK waits */
			auto texts = [dialog] {
				QString all;
				for (QLabel *label : dialog->findChildren<QLabel *>()) all += label->text();
				return all;
			};
			QPushButton *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
			auto *problem = dialog->findChild<ElidedLabel *>(QStringLiteral("problem"));
			calmAtOpen = problem && problem->fullText().isEmpty() && !texts().contains(Theme::colors().bad.name())
					&& !ok->isEnabled();
			const QRect okAtOpen = ok->geometry();
			const QSize sizeAtOpen = dialog->size();
			dialog->findChild<QLineEdit *>(QStringLiteral("presetName"))->setText(QStringLiteral("Fan 55"));
			auto *reg = dialog->findChild<QComboBox *>(QStringLiteral("presetRegister"));
			reg->setCurrentIndex(reg->findData(u8));
			dialog->findChild<QLineEdit *>(QStringLiteral("presetValue"))->setText(QStringLiteral("not a number"));
			QApplication::processEvents();
			wrongValueSaid = problem && problem->fullText().contains(QLatin1String("does not take")) && !ok->isEnabled()
					&& ok->geometry() == okAtOpen && dialog->size() == sizeAtOpen;
			dialog->findChild<QLineEdit *>(QStringLiteral("presetValue"))->setText(QStringLiteral("55"));
			dialog->accept();
		});
		if (newPreset) newPreset->trigger();
		QAction *fan55 = menuAction(QStringLiteral("Fan 55"));
		check(presetButton && presetButton->menu() && presetButton->menu()->toolTipsVisible(),
				"bus: the Broadcast menu shows its actions' tooltips (why a preset is not offered)");
		const QString asked = fan55 ? answerDialog(QStringLiteral("Broadcast"), [&] { fan55->trigger(); }) : QString();
		const bool presetSent = QTest::qWaitFor([&] {
			return one.readU8(regs_.u8.addr) == 55 && two.readU8(regs_.u8.addr) == 55;
		}, 3000);
		check(newPreset && fan55 && asked == QLatin1String("Broadcast") && presetSent,
				"bus: a broadcast preset made in its dialog, sent from the Broadcast menu (after a confirmation) to both");
		check(calmAtOpen && wrongValueSaid,
				"bus: the preset dialog opens with no red text (OK waits for a name and a value); a wrong value is said "
				"on its line, kept from the start: nothing moves");
		/* a preset can be sent only while connected with Allow writes on: else disabled, the tooltip says why */
		allowWrites_->setChecked(false);
		fan55 = menuAction(QStringLiteral("Fan 55"));
		const bool blockedOff = fan55 && !fan55->isEnabled() && fan55->toolTip().contains(QLatin1String("Allow writes"));
		allowWrites_->setChecked(true);
		fan55 = menuAction(QStringLiteral("Fan 55"));
		check(blockedOff && fan55 && fan55->isEnabled(),
				"bus: with Allow writes off a preset is disabled, its tooltip says why; on again, it is offered");
		/* Remove: the preset leaves the menu, the bus is modified */
		QAction *removeFan = nullptr;
		if (QAction *removeMenu = menuAction(QStringLiteral("Remove")); removeMenu && removeMenu->menu())
			for (QAction *action : removeMenu->menu()->actions())
				if (action->text() == QLatin1String("Fan 55")) removeFan = action;
		if (removeFan) removeFan->trigger();
		check(removeFan && !menuAction(QStringLiteral("Fan 55")) && busInfoText().contains(QLatin1String("modified")),
				"bus: Broadcast > Remove takes the preset off; the card says the bus is modified");

		/* another kind of device on the link, and one that never answers */
		open(mixed);
		const bool threeDevices = devices && QTest::qWaitFor([&] { return devices->count() == 3; }, 3000);
		broadcast(regs_.u8.addr, QStringLiteral("05"));
		const bool refused = QTest::qWaitFor([&] { return frames->toPlainText().contains(QLatin1String("!! no broadcast")); },
				2000);
		QTest::qWait(300);
		check(threeDevices && refused && one.readU8(regs_.u8.addr) == 55,
				"bus: different maps on the link: a broadcast into the device bank is refused, nothing sent");
		const QByteArray config = QByteArray::fromHex("0100");
		broadcast(0xA004, QStringLiteral("01 00"));
		const bool reserved = QTest::qWaitFor([&] { return one.read(0xA004, 2) == config; }, 3000);
		check(reserved, "bus: ... but the reserved bank (CONFIG, 0xA004) is the same on every device: broadcast");
		broadcast(0xA004, QStringLiteral("08 4F"));
		const bool autoSendRefused = QTest::qWaitFor([&] {
			return frames->toPlainText().contains(QLatin1String("!! no broadcast")) && frames->toPlainText().contains(QLatin1String("AUTO_SEND"));
		}, 2000);
		QTest::qWait(300);
		check(autoSendRefused && one.read(0xA004, 2) == config,
				"bus: ... except one that switches AUTO_SEND on (every device would send at once): refused, nothing sent");
		one.write(0xA004, QByteArray(2, '\0'));
		two.write(0xA004, QByteArray(2, '\0'));

		const bool offline = QTest::qWaitFor([&] { return logText().contains(QLatin1String("D9: no answer: offline")); },
				10000);
		one.writeU8(regs_.u8.addr, 44);
		if (devices) devices->setCurrentRow(0);
		check(offline && cellShows(valueCell(table_, d1u8), QStringLiteral("44"), 4000),
				"bus: a device that never answers (slave 9) goes offline; the others are still polled");
		const QListWidgetItem *d9 = devices && devices->count() == 3 ? devices->item(2) : nullptr;
		check(d9 && d9->toolTip().contains(QLatin1String("offline")) && d9->text().contains(QStringLiteral("slave 9 · offline"))
						&& !d9->icon().isNull() && d9->data(Qt::ForegroundRole).isNull() /* text colour: readable in both looks */
						&& devices->height() < 6 * devices->sizeHintForRow(0) + 12,
				"bus: the Devices card shows it offline, in words too; the list is as tall as its devices");

		/* the map edited, then a device with another map selected: the question first; Cancel keeps all as it was */
		if (doc) doc->edit(QStringLiteral("test"), [](DeviceMap &m) { m.desc = QStringLiteral("edited by the test"); });
		const QString cancelled = devices ? answerDialog(QStringLiteral("Cancel"), [&] { devices->setCurrentRow(1); })
										  : QString();
		const bool kept = devices && devices->currentRow() == 0 && doc && doc->isModified();
		const QString discarded = devices ? answerDialog(QStringLiteral("Discard"), [&] { devices->setCurrentRow(1); })
										  : QString();
		check(cancelled == QLatin1String("Unsaved map") && kept && discarded == QLatin1String("Unsaved map")
						&& devices->currentRow() == 1 && !doc->isModified() && doc->map().deviceId == otherKind.deviceId,
				"bus: a device with another map selected while the map has edits: asked first; Cancel keeps D1, Discard "
				"takes D2's map");
		if (devices) devices->setCurrentRow(0);

		/* one device again */
		QPushButton *busFile = window_.findChild<QPushButton *>(QStringLiteral("busFile"));
		QAction *close = nullptr;
		for (QAction *action : busFile && busFile->menu() ? busFile->menu()->actions() : QList<QAction *>())
			if (action->text() == QLatin1String("Close bus")) close = action;
		if (close) close->trigger();
		check(close && model->rows().size() == perDevice && names().contains(u8) && devices && !devices->isVisible(),
				"bus: Close bus: one device again, the selected one's map, its names without a prefix");
		allowWrites_->setChecked(false);
		/* the fake device of the other steps again */
		MainWindow::Startup back;
		back.tcp = QStringLiteral("127.0.0.1:%1").arg(FAKE_DEVICE_PORT);
		back.connect = true;
		window_.applyStartup(back);
		check(cellShows(valueCell(table_, u8), QString::number(other_.readU8(regs_.u8.addr)), 5000),
				"bus: closed, the window polls the one device again");
		check(deviceInfo && QTest::qWaitFor([&] {
			return deviceInfo->text().startsWith(QLatin1String("Device ID ")) && deviceInfo->text().contains(QLatin1String("protocol rev"));
		}, 3000),
				"bus: closed, the device's ID under the pill again, without a device's name before it");
		fake.kill();
		fake.waitForFinished(3000);
	}

	/* the Devices card's line ("2 device(s) · same.json · modified") */
	QString busInfoText() const {
		auto *panel = window_.findChild<BusPanel *>();
		auto *line = panel ? panel->findChild<ElidedLabel *>() : nullptr;
		return line ? line->fullText() : QString();
	}

	/* Quick write's "To all devices" on a bus: offered only once a value is typed and with Allow writes on; it asks
	 * first, then sends one broadcast frame, reads every device back, and the Log says each one took it. */
	void quickWriteToAll(const QString &d2u8, OtherClient &one, OtherClient &two) {
		auto *panel = window_.findChild<QFrame *>(QStringLiteral("quickWrite"));
		auto *value = panel ? panel->findChild<QLineEdit *>(QStringLiteral("qwValue")) : nullptr;
		auto *toAll = panel ? panel->findChild<QPushButton *>(QStringLiteral("qwBroadcast")) : nullptr;
		table_->setCurrentIndex(valueCell(table_, d2u8));
		if (value) value->clear();
		const bool emptyOff = toAll && !toAll->isHidden() && !toAll->isEnabled()
				&& toAll->toolTip().contains(QLatin1String("Type a value"));
		if (value) value->setText(QStringLiteral("66"));
		const bool typedOn = toAll && toAll->isEnabled();
		allowWrites_->setChecked(false);
		const bool writesOff = toAll && !toAll->isEnabled();
		allowWrites_->setChecked(true);
		if (value) value->setText(QStringLiteral("66")); /* Allow writes again: the panel was made again */
		const QString asked = toAll && toAll->isEnabled()
				? answerDialog(QStringLiteral("Broadcast"), [&] { toAll->click(); }) : QString();
		const bool both = QTest::qWaitFor([&] {
			return one.readU8(regs_.u8.addr) == 66 && two.readU8(regs_.u8.addr) == 66;
		}, 3000);
		const bool logged = QTest::qWaitFor([&] { return logText().contains(QLatin1String("every device took it")); }, 3000);
		check(emptyOff && typedOn && writesOff, "bus: To all devices is offered once a value is typed, with Allow writes on");
		check(asked == QLatin1String("Broadcast") && both && logged,
				"bus: To all devices asks first, reaches both devices, and the Log says every device took it");
		one.writeU8(regs_.u8.addr, 11);
		two.writeU8(regs_.u8.addr, 33);
	}

	/* The Devices card: + Device and Edit... through their dialog (its problem line kept from the start, OK only when
	 * nothing is wrong), a device not polled, - Device, Bus file > Save. Leaves D1 and D2, D2 selected, saved. */
	void devicesCard(QListWidget *devices, RegisterModel *model, const QString &d1u8, const QString &d2u8,
			OtherClient &one, OtherClient &two, const QString &folder) {
		QPushButton *add = buttonWithText(QStringLiteral("+ Device"));
		QPushButton *edit = buttonWithText(QStringLiteral("Edit…"));
		QPushButton *remove = buttonWithText(QStringLiteral("− Device"));
		check(devices && add && edit && remove, "bus: the Devices card's + Device, Edit…, − Device");
		if (!devices || !add || !edit || !remove) return;
		/* + Device: a slave taken is said on the problem line (nothing moves), OK waits; a free one is taken */
		bool takenSaid = false, stays = false;
		const bool added = fillDialog([&](QDialog *dialog) {
			auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("deviceName"));
			auto *slave = dialog->findChild<QSpinBox *>(QStringLiteral("deviceSlave"));
			auto *poll = dialog->findChild<QCheckBox *>();
			auto *problem = dialog->findChild<ElidedLabel *>(QStringLiteral("problem"));
			QPushButton *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
			if (!name || !slave || !poll || !problem) return;
			const QRect okBefore = ok->geometry();
			name->setText(QStringLiteral("D3"));
			slave->setValue(1);
			QApplication::processEvents();
			takenSaid = !problem->fullText().isEmpty() && !ok->isEnabled();
			stays = ok->geometry() == okBefore && problem->height() > 0;
			slave->setValue(3);
			poll->setChecked(false); /* no device answers at 3: not polled, it never goes offline */
			if (ok->isEnabled()) dialog->accept();
		}, [&] { add->click(); });
		const QListWidgetItem *d3 = devices->count() == 3 ? devices->item(2) : nullptr;
		check(added && takenSaid && stays && d3 && d3->text().startsWith(QStringLiteral("D3 · slave 3 · not polled · "))
						&& busInfoText().contains(QLatin1String("modified")),
				"bus: + Device: a slave already taken is said on the dialog's line (OK waits, nothing moves); D3 added, "
				"its row D3 · slave 3 · not polled · map");
		/* - Device: D3 selected, asked, taken off */
		devices->setCurrentRow(2);
		const QString asked = answerDialog(QStringLiteral("Yes"), [&] { remove->click(); });
		check(asked == QLatin1String("Remove device") && devices->count() == 2 && model->rows().size() == 2 * map_.regs.size(),
				"bus: − Device asks, then takes the device off the bus (its registers leave the table)");
		devices->setCurrentRow(1);
		/* Edit…: D2 not polled; another client's change of D2 is not seen, D1's is */
		auto setPolled = [&](bool polled) {
			return fillDialog([&](QDialog *dialog) {
				if (auto *poll = dialog->findChild<QCheckBox *>()) poll->setChecked(polled);
				dialog->accept();
			}, [&] { edit->click(); });
		};
		auto rawOf = [&](const QString &name) {
			for (const RegisterModel::Row &row : model->rows())
				if (row.def.name == name) return row.valid && row.raw.size() == 1 ? int(uint8_t(row.raw[0])) : -1;
			return -1;
		};
		const bool unpolled = setPolled(false);
		two.writeU8(regs_.u8.addr, 99);
		one.writeU8(regs_.u8.addr, 12);
		const bool d1Seen = QTest::qWaitFor([&] { return rawOf(d1u8) == 12; }, 3000);
		QTest::qWait(500);
		check(unpolled && devices->item(1)->text().contains(QLatin1String("not polled")) && d1Seen && rawOf(d2u8) != 99,
				"bus: a device not polled (Edit…, unticked) is left out of the polls; the others are polled");
		const bool polledAgain = setPolled(true);
		check(polledAgain && QTest::qWaitFor([&] { return rawOf(d2u8) == 99; }, 3000),
				"bus: polled again, its value comes");
		one.writeU8(regs_.u8.addr, 11);
		two.writeU8(regs_.u8.addr, 33);
		(void) QTest::qWaitFor([&] { return rawOf(d1u8) == 11 && rawOf(d2u8) == 33; }, 3000);
		/* Bus file > Save: written at once to its file (Save as… asks for a file in the system's dialog: not here) */
		QPushButton *fileButton = window_.findChild<QPushButton *>(QStringLiteral("busFile"));
		QAction *save = nullptr;
		for (QAction *action : fileButton && fileButton->menu() ? fileButton->menu()->actions() : QList<QAction *>())
			if (action->text() == QLatin1String("Save")) save = action;
		if (save) save->trigger();
		BusFile saved;
		QString error;
		check(save && !busInfoText().contains(QLatin1String("modified"))
						&& saved.load(folder + QStringLiteral("/same.json"), error) && saved.devices.size() == 2,
				"bus: Bus file > Save writes the bus file; the card no longer says modified");
	}

	/* Several devices on one link, in the master: each request carries its slave, an answer completes only a request
	 * to the slave it comes from, and the broadcast address (0) takes a WRITE without acknowledge, nothing else. */
	void masterSlaves() {
		evre::Master master;
		LoopLink link;
		master.setKeepAlive(false);
		master.setInFlight(4);
		master.setLink(&link);
		QByteArray fromTwo, fromThree;
		int answers = 0, unsolicited = 0;
		QObject::connect(&master, &evre::Master::unsolicited, [&](const evre::Frame &) { unsolicited++; });
		master.readFrom(2, 0xA000, 2, [&](const evre::Result &r) { fromTwo = r.data; answers++; });
		master.readFrom(3, 0xA000, 2, [&](const evre::Result &r) { fromThree = r.data; answers++; });
		const bool addressed = link.sent.size() == 2 && link.sent[0].slave == 2 && link.sent[1].slave == 3;
		/* slave 3 answers first; then a stranger (slave 5), then slave 2 */
		link.answer(3, evre::READ_RESP, 0xA000, 2, QByteArray::fromHex("3333"));
		const bool threeOnly = answers == 1 && fromThree == QByteArray::fromHex("3333") && fromTwo.isEmpty();
		link.answer(5, evre::READ_RESP, 0xA000, 2, QByteArray::fromHex("5555"));
		const bool strangerIgnored = answers == 1 && unsolicited == 1;
		link.answer(2, evre::READ_RESP, 0xA000, 2, QByteArray::fromHex("2222"));
		check(addressed && threeOnly && strangerIgnored && answers == 2 && fromTwo == QByteArray::fromHex("2222"),
				"master: each request goes to its slave; an answer completes only a request to the slave it comes from");

		/* AUTO_SEND: a frame of the whole read-only block (0xD000, 22 bytes) while a read of its first 4 bytes waits is
		 * no answer to that read; the answer with the count asked for is */
		QByteArray part;
		int partAnswers = 0;
		unsolicited = 0;
		master.readFrom(2, 0xD000, 4, [&](const evre::Result &r) { part = r.data; partAnswers++; });
		link.answer(2, evre::READ_RESP, 0xD000, 22, QByteArray(22, '\x11'));
		const bool streamFrameApart = partAnswers == 0 && unsolicited == 1;
		link.answer(2, evre::READ_RESP, 0xD000, 4, QByteArray::fromHex("01020304"));
		check(streamFrameApart && partAnswers == 1 && part == QByteArray::fromHex("01020304"),
				"master: a READ_RESP at the offset asked for but with another count is unsolicited, not the answer");

		link.sent.clear();
		QString readRefusal, ackRefusal;
		bool broadcastSent = false;
		master.readFrom(evre::BROADCAST, 0xA000, 2, [&](const evre::Result &r) { readRefusal = r.ok ? QString() : r.message; });
		master.writeTo(evre::BROADCAST, 0xA004, QByteArray(2, 0), [&](const evre::Result &r) {
			ackRefusal = r.ok ? QString() : r.message;
		});
		const bool nothingSent = link.sent.isEmpty();
		master.writeNoAckTo(evre::BROADCAST, 0xA004, QByteArray(2, 0), [&](const evre::Result &r) { broadcastSent = r.ok; });
		check(readRefusal.contains(QLatin1String("broadcast")) && ackRefusal.contains(QLatin1String("broadcast"))
						&& nothingSent && broadcastSent && link.sent.size() == 1 && link.sent[0].slave == evre::BROADCAST
						&& link.sent[0].fn == evre::WRITE,
				"master: slave 0 (broadcast) takes a WRITE without acknowledge; a READ or WRITE_ACK to it is refused unsent");

		link.sent.clear();
		master.setSlave(7);
		master.read(0xA000, 2);
		check(link.sent.size() == 1 && link.sent[0].slave == 7, "master: a request without a slave goes to the master's own");
		master.setLink(nullptr);

		DeviceMap broadcastDevice = map_;
		broadcastDevice.slave = 0;
		const QVector<MapIssue> issues = checkMap(broadcastDevice);
		const bool flagged = std::any_of(issues.begin(), issues.end(),
				[](const MapIssue &i) { return i.error && i.reg < 0 && i.text.contains(QLatin1String("broadcast")); });
		const QVector<MapIssue> asItIs = checkMap(map_);
		check(flagged && std::none_of(asItIs.begin(), asItIs.end(),
						  [](const MapIssue &i) { return i.text.contains(QLatin1String("broadcast")); }),
				"map check: slave 0 is an error (the broadcast address, no device answers it)");
	}

	/* The formula box's completion (the Math line dialog): the order of the candidates, the word at the cursor,
	 * and the list while typing: Enter takes a register or a function (name(), the cursor inside), Esc closes it. */
	void formulaCompletion() {
		using Candidate = FormulaCompleter::Candidate;
		const QVector<Candidate> all = { { QStringLiteral("SUPPLY_V"), {}, false }, { QStringLiteral("SUPPLY_I"), {}, false },
			{ QStringLiteral("ISENSE"), {}, false }, { QStringLiteral("sin"), {}, true }, { QStringLiteral("sqrt"), {}, true } };
		auto names = [](const QVector<Candidate> &list) {
			QStringList out;
			for (const Candidate &c : list) out << c.name;
			return out;
		};
		check(names(FormulaCompleter::rank(all, QStringLiteral("s")))
						== QStringList({ "sin", "sqrt", "SUPPLY_I", "SUPPLY_V", "ISENSE" })
						&& names(FormulaCompleter::rank(all, QStringLiteral("i"))) == QStringList({ "ISENSE", "SUPPLY_I", "sin" })
						&& FormulaCompleter::rank(all, QString()).isEmpty(),
				"formula completion: names that start with the word, then a part after _ (I: SUPPLY_I), then any that contain it");
		const QString formula = QStringLiteral("SUPPLY_V * SUP");
		check(FormulaCompleter::wordStart(formula, int(formula.size())) == 11
						&& FormulaCompleter::wordStart(QStringLiteral("2.5"), 3) == 3
						&& FormulaCompleter::wordStart(QStringLiteral("abs(x"), 5) == 4,
				"formula completion: the word at the cursor (not a number, not the whole formula)");

		MathLine start;
		MathLineDialog dialog(start, false, map_.regs, &window_);
		dialog.show();
		auto *box = dialog.findChild<QLineEdit *>(QStringLiteral("formula"));
		auto *completer = dialog.findChild<FormulaCompleter *>();
		check(box && completer, "Math line dialog: the formula box has its completion");
		if (!box || !completer) return;
		const QString volts = regs_.volts.name;
		/* a register: its first letters, the list, Enter */
		QTest::keyClicks(box, volts.left(3));
		const QStringList offered = completer->shown();
		const bool listed = offered.contains(volts) && !offered.isEmpty()
				&& offered.front().startsWith(volts.left(3), Qt::CaseInsensitive);
		QTest::keyClick(completer->completer()->popup(), Qt::Key_Down);
		while (completer->completer()->popup()->currentIndex().data(Qt::UserRole + 1).toString() != volts
				&& completer->completer()->popup()->currentIndex().row() < offered.size() - 1)
			QTest::keyClick(completer->completer()->popup(), Qt::Key_Down);
		QTest::keyClick(completer->completer()->popup(), Qt::Key_Return);
		check(listed && box->text() == volts && dialog.isVisible() && completer->shown().isEmpty(),
				"formula completion: typing lists the registers, Enter puts the one picked in (the dialog stays open)");
		/* a function, after the rest of the formula: name(), the cursor inside */
		QTest::keyClicks(box, QStringLiteral(" * sq"));
		const bool sqrtFirst = completer->shown().value(0) == QLatin1String("sqrt");
		QTest::keyClick(completer->completer()->popup(), Qt::Key_Return);
		check(sqrtFirst && box->text() == volts + QStringLiteral(" * sqrt()")
						&& box->cursorPosition() == int(box->text().size()) - 1,
				"formula completion: a function goes in as sqrt(), the cursor inside; the rest of the formula kept");
		/* Esc closes the list; a word that is a whole name already offers nothing */
		box->setText(QString());
		QTest::keyClicks(box, QStringLiteral("ab"));
		const bool open = !completer->shown().isEmpty();
		QTest::keyClick(completer->completer()->popup(), Qt::Key_Escape);
		const bool closed = completer->shown().isEmpty() && box->text() == QLatin1String("ab");
		box->setText(QString());
		QTest::keyClicks(box, QStringLiteral("pi"));
		check(open && closed && completer->shown().isEmpty(),
				"formula completion: Esc closes the list; a whole name (pi) offers nothing more");
		dialog.close();

		/* without the compositor's open and close animations (Windows, STUDIO.md 27): the dialog is made so, and from
		 * the Chart tab's Math menu it opens and closes as the title bar's X closes it, the Math button as before */
		auto *math = window_.findChild<ChartTab *>() ? window_.findChild<ChartTab *>()->findChild<QPushButton *>(
				QStringLiteral("math")) : nullptr;
		QAction *newLine = math && math->menu() ? math->menu()->actions().value(0) : nullptr;
		const QString mathText = math ? math->text() : QString();
		bool unanimated = false;
		const bool opened = newLine && fillDialog([&](QDialog *d) {
			unanimated = qobject_cast<MathLineDialog *>(d) && d->property("noAnimation").toBool();
			d->close();
		}, [&] { newLine->trigger(); });
		check(dialog.property("noAnimation").toBool() && opened && unanimated && !QApplication::activeModalWidget()
						&& math->text() == mathText,
				"Math line dialog: made without the window animations; New math line… opens it, closed as by its X it "
				"goes and the Math button is as before");
	}

	/* The Monitor's own requests: READ, the checks of what is typed, WRITE + ack (read back into the table), WRITE
	 * without ack (sent, not "OK"), Enter in either box, the second box following the function, Clear. */
	void monitorRequests() {
		auto *tab = window_.findChild<MonitorTab *>();
		auto *function = tab ? tab->findChild<QComboBox *>(QStringLiteral("monitorFunction")) : nullptr;
		/* the address and value boxes: not the line edit inside the Slave box */
		QList<QLineEdit *> boxes = tab ? tab->findChildren<QLineEdit *>() : QList<QLineEdit *>();
		boxes.removeIf([](const QLineEdit *box) { return qobject_cast<QAbstractSpinBox *>(box->parent()) != nullptr; });
		QPushButton *send = tab ? buttonWithText(*tab, QStringLiteral("Send")) : nullptr;
		QPushButton *clear = tab ? buttonWithText(*tab, QStringLiteral("Clear")) : nullptr;
		auto *frames = tab ? tab->findChild<QPlainTextEdit *>() : nullptr;
		check(function && boxes.size() >= 2 && send && clear && frames, "Monitor: function, address, value, Send, Clear");
		if (!function || boxes.size() < 2 || !send || !clear || !frames) return;
		QLineEdit *address = boxes[0], *argument = boxes[1];
		const QString u8 = addrText(regs_.u8.addr);
		const int u8Before = other_.readU8(regs_.u8.addr);
		const bool writesWere = allowWrites_->isChecked();
		auto shows = [&](const QString &text) {
			return QTest::qWaitFor([&] { return frames->toPlainText().contains(text); }, 3000);
		};
		auto request = [&](int index, const QString &addr, const QString &value) {
			function->setCurrentIndex(index);
			address->setText(addr);
			argument->setText(value);
			frames->clear();
			send->click();
		};

		/* READ: the bytes the device holds */
		other_.writeU8(regs_.u8.addr, 7);
		request(0, u8, QStringLiteral("1"));
		check(shows(QStringLiteral("== %1 OK: 07").arg(u8)) && frames->toPlainText().contains(QLatin1String(" ms)")),
				"Monitor READ: the device's bytes and the answer's time");
		/* Enter in the address box sends too */
		frames->clear();
		QTest::keyClick(address, Qt::Key_Return);
		check(shows(QStringLiteral("== %1 OK: 07").arg(u8)), "Monitor: Enter in the address box sends");
		/* what is typed is checked first */
		request(0, QStringLiteral("0xZZ"), QStringLiteral("1"));
		const bool badAddress = shows(QStringLiteral("!! bad address"));
		request(0, u8, QStringLiteral("0"));
		check(badAddress && shows(QStringLiteral("!! READ needs a count")), "Monitor: a bad address or count is refused");

		/* the second box follows the function: READ's count is no value to write */
		function->setCurrentIndex(0);
		argument->setText(QStringLiteral("2"));
		function->setCurrentIndex(1);
		check(argument->text().isEmpty() && argument->placeholderText().contains(QLatin1String("low byte first")),
				"Monitor: WRITE empties READ's count and says how to type the bytes (low byte first)");

		/* writes: only with Allow writes; hex bytes only (a decimal 300 is not taken as 03 00) */
		allowWrites_->setChecked(false);
		request(1, u8, QStringLiteral("05"));
		const bool off = shows(QStringLiteral("!! writes are off"));
		allowWrites_->setChecked(true);
		request(1, u8, QStringLiteral("300"));
		check(off && shows(QStringLiteral("!! not hex bytes")) && other_.readU8(regs_.u8.addr) == 7,
				"Monitor WRITE: refused with writes off, and refused for what is not hex bytes");
		QByteArray parsed;
		const bool forms = MonitorTab::parseHexBytes(QStringLiteral("2C 01"), parsed) && parsed == QByteArray("\x2C\x01", 2)
				&& MonitorTab::parseHexBytes(QStringLiteral("0x2C,0x01"), parsed) && parsed == QByteArray("\x2C\x01", 2)
				&& MonitorTab::parseHexBytes(QStringLiteral("2c01"), parsed) && parsed == QByteArray("\x2C\x01", 2)
				&& MonitorTab::parseHexBytes(QStringLiteral("5"), parsed) && parsed == QByteArray("\x05", 1)
				&& !MonitorTab::parseHexBytes(QStringLiteral("300"), parsed)
				&& !MonitorTab::parseHexBytes(QStringLiteral("2G"), parsed);
		check(forms, "Monitor: hex bytes as 2C 01, 0x2C,0x01, 2c01 or 5; 300 and 2G refused");

		/* WRITE + ack: the device's OK (not timed: no "0.0 ms"), the value in the device and in the Log */
		request(1, u8, QStringLiteral("05"));
		check(shows(QStringLiteral("== %1 OK").arg(u8)) && !frames->toPlainText().contains(QLatin1String("ms)"))
						&& u8Becomes(5) && logText().contains(QStringLiteral("raw write 05 at %1: OK").arg(u8)),
				"Monitor WRITE + ack: OK, the device holds it, the Log says so");
		/* WRITE without ack: sent, and it says nothing confirms it (it said "OK") */
		request(2, u8, QStringLiteral("06"));
		check(shows(QStringLiteral("-> %1 sent: 06").arg(u8)) && !frames->toPlainText().contains(QLatin1String("OK"))
						&& u8Becomes(6) && logText().contains(QStringLiteral("raw write 06 at %1: sent (no acknowledge)").arg(u8)),
				"Monitor WRITE (no ack): \"sent\", not OK; the device holds it");

		/* one device: the Slave box follows the sidebar's (Poll off meanwhile: no poll goes to a slave that is not
		 * there); 0 is "0 (broadcast)" and locks the function to WRITE (no ack); a device again gets READ back */
		auto *slaveBox = tab->findChild<LimitSpinBox *>(QStringLiteral("monitorSlave"));
		auto *side = window_.findChild<Sidebar *>();
		const int slaveWas = side ? side->slave() : 1;
		poll_->setChecked(false);
		if (side) side->setSlave(slaveWas == 7 ? 8 : 7);
		const bool follows = side && slaveBox && slaveBox->value() == side->slave();
		if (side) side->setSlave(slaveWas);
		poll_->setChecked(true);
		function->setCurrentIndex(0);
		if (slaveBox) slaveBox->setValue(evre::BROADCAST);
		const bool locked = slaveBox && slaveBox->text() == QLatin1String("0 (broadcast)")
				&& function->currentData().toInt() == int(evre::WRITE) && !function->isEnabled();
		if (slaveBox) slaveBox->setValue(slaveWas);
		check(follows && locked && slaveBox->value() == slaveWas && function->currentIndex() == 0 && function->isEnabled(),
				"Monitor: Slave follows the sidebar's; 0 (broadcast) locks WRITE (no ack), a device again gets READ back");

		clear->click();
		check(frames->toPlainText().isEmpty(), "Monitor: Clear empties the view");
		function->setCurrentIndex(0);
		argument->setText(QStringLiteral("2"));
		allowWrites_->setChecked(writesWere);
		if (u8Before >= 0) other_.writeU8(regs_.u8.addr, uint8_t(u8Before));
	}

	/* the UI audit of 2026-10-01: each finding, as it is now */
	/* The input boxes' edge in the accent while the mouse is over one, as a button's (the Chart tab's first row had
	 * none: only a click showed it); a disabled one keeps its plain edge */
	void hoverEdges() {
		const QColor accent = Theme::colors().accent;
		auto edge = [](QWidget &w) { /* the top edge's middle pixel */
			const QImage image = w.grab().toImage();
			return image.pixelColor(image.width() / 2, 0);
		};
		auto near = [](const QColor &a, const QColor &b) {
			return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue()) < 40;
		};
		auto *ram = window_.findChild<QComboBox *>(QStringLiteral("chartRam"));
		QLineEdit edit;
		QSpinBox spin;
		QComboBox off;
		off.addItem(QStringLiteral("1 GB"));
		off.setEnabled(false);
		bool all = ram != nullptr;
		QString failed;
		for (QWidget *w : { static_cast<QWidget *>(ram), static_cast<QWidget *>(&edit), static_cast<QWidget *>(&spin),
				 static_cast<QWidget *>(&off) }) {
			if (!w) continue;
			w->resize(std::max(w->width(), 120), w->sizeHint().height());
			w->ensurePolished();
			const bool idle = !near(edge(*w), accent);
			w->setAttribute(Qt::WA_UnderMouse, true);
			const bool hovered = near(edge(*w), accent);
			w->setAttribute(Qt::WA_UnderMouse, false);
			const bool ok = idle && (w == &off ? !hovered : hovered);
			if (!ok) failed += QString::fromLatin1(w->metaObject()->className()) + QLatin1Char(' ');
			all = all && ok;
		}
		if (!failed.isEmpty()) std::printf("     (no hover edge: %s)\n", qPrintable(failed));
		check(all, "the look: a combo box, text box or spin box gets the accent edge while the mouse is over it, as a "
				"button; a disabled one does not");
	}

	void uiAudit() {
		auto *tabs = window_.findChild<QTabWidget *>();
		const int tabBefore = tabs ? tabs->currentIndex() : 0;

		/* contrast, both looks: text 4.5:1 on what it sits on, a control's edge 3:1; the log's lines, logged in the
		 * dark look, drawn again in the colours of the look shown (they were near-white on the light one) */
		auto *logView = window_.findChild<QPlainTextEdit *>(QStringLiteral("eventLog"));
		bool textOk = true, controlOk = true, scrollOk = true, logFollows = logView && logView->document()->blockCount() > 1;
		for (bool dark : { true, false }) {
			Theme::apply(*qApp, dark);
			QApplication::processEvents();
			const ThemeColors &c = Theme::colors();
			for (QTextBlock block = logView ? logView->document()->firstBlock() : QTextBlock(); block.isValid();
					block = block.next()) {
				if (block.text().isEmpty()) continue;
				const QColor color = block.begin().fragment().charFormat().foreground().color();
				if (color != c.text && color != c.muted && color != c.warn && color != c.bad) logFollows = false;
			}
			for (const QColor &back : { c.bg, c.surface, c.surface2 }) {
				for (const QColor &fore : { c.text, c.muted, c.accent, c.good, c.warn, c.bad })
					if (contrast(fore, back) < 4.5) {
						textOk = false;
						std::printf("contrast %s on %s: %.2f (%s)\n", qPrintable(fore.name()), qPrintable(back.name()),
								contrast(fore, back), dark ? "dark" : "light");
					}
				if (contrast(c.control, back) < 3.0) controlOk = false;
			}
			/* the scroll bars' handles: the control colour (3:1), not the border's (barely there) */
			const QString sheet = qApp->styleSheet();
			for (const char *orientation : { "vertical", "horizontal" })
				if (!sheet.contains(QStringLiteral("QScrollBar::handle:%1 { background: %2;")
								.arg(QLatin1String(orientation), c.control.name())))
					scrollOk = false;
			for (const QColor &fill : { c.accentFill, c.badFill, c.accentFill.lighter(106) })
				if (contrast(Qt::white, fill) < 4.5) textOk = false;
		}
		Theme::apply(*qApp, true);
		check(textOk, "contrast: every text colour 4.5:1 on the window, panels and cards, both looks (WCAG AA)");
		check(controlOk, "contrast: the edge of boxes and check boxes 3:1, both looks (WCAG AA)");
		check(scrollOk, "contrast: the scroll bars' handles in the control colour, 3:1, both looks (WCAG AA)");

		/* why the polls are slower than asked: in the status bar, where it comes and goes moving nothing; the polling
		 * card keeps only the rate (it grew and shrank, and the sidebar's scroll bar with it) */
		{
			auto *slow = window_.findChild<QLabel *>(QStringLiteral("statusSlow"));
			bool cardRateOnly = false;
			for (QLabel *label : window_.findChild<Sidebar *>()->findChildren<QLabel *>())
				if (label->text().contains(QLatin1String("polls/s"))) cardRateOnly = !label->text().contains(QLatin1String("<br>"));
			check(slow && window_.statusBar()->isAncestorOf(slow), "poll hint: in the status bar (it moves nothing there)");
			check(cardRateOnly && slow && slow->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored
							&& slow->textFormat() == Qt::PlainText,
					"poll hint: the polling card shows the rate alone; the hint is always there (empty when not slow), "
					"plain, cut rather than widen the window: the status bar does not move");
			/* In flight below the reads of a poll: the reads go In flight at a time (12 devices, 36 reads, In flight 1:
			 * 36 answer times per poll); the limit is that, and the hint says so */
			auto *side = window_.findChild<Sidebar *>();
			const double interval = side->pollIntervalMs();
			const int inFlight = side->inFlight();
			side->setPollInterval(0.1);
			side->setInFlight(1);
			IoEngine::Stats stats;
			stats.blocks = 36;
			stats.pollable = 180;
			stats.pollHz = 45;
			stats.master.avgLatencyMs = 0.6;
			const QString oneAtATime = side->slowPollHintFor(stats);
			side->setInFlight(64);
			stats.pollHz = 1500; /* 64 in flight: a poll's 36 reads go together, ~1600 polls/s at the most */
			const QString together = side->slowPollHintFor(stats);
			side->setPollInterval(interval);
			side->setInFlight(inFlight);
			/* In flight past its range: the number stays as typed, the edge amber, and Enter takes it to the range's end
			 * (a plain spin box dropped the digit, saying nothing; no floating tooltip either) */
			QSpinBox *inFlightBox = nullptr;
			for (QSpinBox *box : side->findChildren<QSpinBox *>())
				if (box->maximum() == 1024) inFlightBox = box;
			bool typedKept = false, amber = false, settled = false;
			if (inFlightBox) {
				const int was = inFlightBox->value();
				inFlightBox->setFocus();
				inFlightBox->selectAll();
				QTest::keyClicks(inFlightBox, QStringLiteral("2000"));
				typedKept = inFlightBox->text() == QLatin1String("2000");
				amber = inFlightBox->property("outOfRange").toBool();
				QTest::keyClick(inFlightBox, Qt::Key_Return);
				settled = inFlightBox->value() == 1024 && !inFlightBox->property("outOfRange").toBool()
						&& inFlightBox->toolTip().endsWith(QLatin1String("1 to 1024"));
				inFlightBox->setValue(was);
			}
			check(inFlightBox && typedKept && amber && settled,
					"In flight: up to 1024; a number past it stays as typed with an amber edge, Enter makes it 1024");
			check(oneAtATime.contains(QLatin1String("36 reads, sent 1 at a time")) && oneAtATime.contains(QLatin1String("Set In flight"))
							&& !together.isEmpty(),
					"poll hint: In flight below a poll's reads (36 reads, In flight 1, 45 polls/s) is said, with the In flight to set");
		}
		/* the sidebar's cards as wide with its scroll bar as without: one that comes or goes rewraps nothing */
		{
			auto *side = window_.findChild<QScrollArea *>(QStringLiteral("sideScroll"));
			QWidget *content = side ? side->widget() : nullptr;
			check(content && content->minimumWidth() == content->maximumWidth()
							&& content->width() + side->verticalScrollBar()->sizeHint().width() <= side->width(),
					"sidebar: its cards one width, with the scroll bar or without (no rewrap that brings it back)");
		}
		check(logFollows, "log: its lines drawn again in the colours of the look shown (readable after a switch)");

		/* Allow writes in bold when on: as wide as off, the toolbar beside it does not move */
		{
			if (tabs) tabs->setCurrentIndex(MainWindow::TabRegisters);
			const bool was = allowWrites_->isChecked();
			allowWrites_->setChecked(false);
			QApplication::processEvents();
			QPushButton *plotButton = buttonWithText(QStringLiteral("Plot shown"));
			const int offWidth = allowWrites_->width(), offX = plotButton ? plotButton->x() : -1;
			allowWrites_->setChecked(true);
			QApplication::processEvents();
			check(allowWrites_->width() == offWidth && plotButton && plotButton->x() == offX,
					"Allow writes on (bold): as wide as off, the buttons beside it stay in place");
			allowWrites_->setChecked(was);
		}

		/* a highlight drawn in a theme colour follows a switch too: Allow writes' amber */
		const bool writesWere = allowWrites_->isChecked();
		allowWrites_->setChecked(true);
		Theme::apply(*qApp, false);
		QApplication::processEvents();
		const bool lightAmber = allowWrites_->styleSheet().contains(Theme::colors().warn.name());
		Theme::apply(*qApp, true);
		QApplication::processEvents();
		const bool darkAmber = allowWrites_->styleSheet().contains(Theme::colors().warn.name());
		allowWrites_->setChecked(writesWere);
		check(lightAmber && darkAmber, "Allow writes: its highlight in the amber of the look shown, after a switch");

		/* polling below the timer's resolution: a wake-up late by several periods counts every one of them (they
		 * were dropped: 0.25 ms gave 1750 polls/s); a long stall starts again from now */
		{
			using namespace std::chrono;
			const auto period = microseconds(250);
			auto deadline = steady_clock::now() - microseconds(1100); /* woke 1.1 ms late: 4 more periods passed */
			const auto reached = deadline;
			const int due = tickPeriodsDue(deadline, period);
			auto onTime = steady_clock::now();
			const int one = tickPeriodsDue(onTime, period);
			auto stalled = steady_clock::now() - seconds(60);
			const int many = tickPeriodsDue(stalled, period);
			check(due == 5 && deadline == reached + 4 * period && one == 1 && many > 1000
							&& steady_clock::now() - stalled < milliseconds(100),
					"poll ticker: a late wake-up counts every period passed (0.25 ms: 4000 polls/s, not 1750)");
		}

		/* the hint's In flight: never past the engine's polls at once (8 x 3 reads = 24; it said 33, which runs no
		 * more polls than 24) */
		check(Sidebar::suggestedInFlight(3, 5, 4000, 2.6, 64) == 3 * IoEngine::MAX_POLLS_UNDER_WAY
						&& Sidebar::suggestedInFlight(3, 5, 1000, 2.6, 64) == 9 /* 3 polls at once: 9 */
						&& Sidebar::suggestedInFlight(3, 12, 0, 0.4, 64) == 24,
				"poll hint: the In flight it suggests runs more polls (at most 8 at once: 24 for 3 reads)");

		/* focus: a ring for the focus from the keyboard, none for a click */
		QPushButton *plotShown = buttonWithText(window_, QStringLiteral("Plot shown"));
		QPushButton *registerButton = buttonWithText(window_, QStringLiteral("+ Register"));
		if (tabs) tabs->setCurrentIndex(MainWindow::TabRegisters);
		if (plotShown && registerButton) {
			/* the focus events themselves: a window that is not active (xvfb, no window manager) gets none */
			auto focus = [](QWidget *widget, QEvent::Type type, Qt::FocusReason reason) {
				QFocusEvent event(type, reason);
				QApplication::sendEvent(widget, &event);
			};
			focus(plotShown, QEvent::FocusIn, Qt::TabFocusReason);
			const bool ring = plotShown->property("keyFocus").toBool();
			focus(plotShown, QEvent::FocusOut, Qt::MouseFocusReason);
			focus(registerButton, QEvent::FocusIn, Qt::MouseFocusReason);
			const bool clicked = registerButton->property("keyFocus").toBool();
			focus(registerButton, QEvent::FocusOut, Qt::MouseFocusReason);
			check(ring && !plotShown->property("keyFocus").toBool() && !clicked
							&& qApp->styleSheet().contains(QStringLiteral("QPushButton[keyFocus=\"true\"]")),
					"focus: a ring on a button reached with Tab, none after a click");
		}

		/* check boxes: a tick when ticked, a dash when partly, not only a colour */
		const QRegularExpressionMatch tick = QRegularExpression(
				QStringLiteral("QCheckBox::indicator:checked \\{[^}]*image: url\\(\"([^\"]+)\"\\)")).match(qApp->styleSheet());
		const QRegularExpressionMatch dash = QRegularExpression(
				QStringLiteral("QCheckBox::indicator:indeterminate \\{[^}]*image: url\\(\"([^\"]+)\"\\)")).match(qApp->styleSheet());
		check(tick.hasMatch() && dash.hasMatch() && !QImage(tick.captured(1)).isNull() && !QImage(dash.captured(1)).isNull(),
				"check boxes: a tick when ticked and a dash when partly (their images exist)");

		/* the window fits a 1366 x 768 screen (1280 x 720 at 150 %): it was 1236 x 902 at least */
		const QSize least = window_.minimumSizeHint();
		std::printf("window minimum %d x %d\n", least.width(), least.height());
		check(least.width() <= 1280 && least.height() <= 660, "the window's minimum size fits 1280 x 720");

		/* the spin boxes as tall as the text boxes beside them (a text box of its own, not a combo box's) */
		auto *sidebar = window_.findChild<Sidebar *>();
		QLineEdit *edit = nullptr;
		for (QLineEdit *box : sidebar ? sidebar->findChildren<QLineEdit *>() : QList<QLineEdit *>())
			if (!qobject_cast<QComboBox *>(box->parentWidget()) && !qobject_cast<QAbstractSpinBox *>(box->parentWidget())) {
				edit = box;
				break;
			}
		QSpinBox *spin = sidebar ? sidebar->findChild<QSpinBox *>() : nullptr;
		check(edit && spin && edit->height() == spin->height(), "a spin box is as tall as a text box");

		/* Registers: access as the map writes it; each header aligned as its cells; a bytes register says why it
		 * has no Plot box */
		const QModelIndex rwAccess = model_->index(regRow(regs_.u8.name), RegisterModel::ColAccess);
		const auto headerAlign = [](const QAbstractItemModel *model, int column) {
			return Qt::Alignment(model->headerData(column, Qt::Horizontal, Qt::TextAlignmentRole).toInt());
		};
		check(rwAccess.data().toString().startsWith(QLatin1String("rw"))
						&& (headerAlign(model_, RegisterModel::ColValue) & Qt::AlignRight)
						&& (headerAlign(model_, RegisterModel::ColName) & Qt::AlignLeft),
				"Registers: access \"rw\" as in the map; Value's header at the right as its numbers, Name's at the left");
		for (const RegisterModel::Row &row : model_->rows()) {
			if (row.def.isNumeric()) continue;
			const QModelIndex plot = model_->index(regRow(row.def.name), RegisterModel::ColPlot);
			check(!plot.data(Qt::CheckStateRole).isValid() && plot.data().toString() == QStringLiteral("—")
							&& plot.data(Qt::ToolTipRole).toString().contains(QLatin1String("not a number")),
					"a bytes register: a dash in Plot, and why in its tooltip");
			break;
		}

		/* the status bar: the link's numbers, the poll rate only in the sidebar */
		bool pollTwice = false;
		for (QLabel *label : window_.statusBar()->findChildren<QLabel *>())
			if (label->text().startsWith(QLatin1String("Poll "))) pollTwice = true;
		check(!pollTwice, "the status bar does not repeat the sidebar's poll rate");

		/* the link state: a dot and the state, not a second button; no empty line under it */
		auto *pill = window_.findChild<QLabel *>(QStringLiteral("pill"));
		check(pill && pill->text().startsWith(QStringLiteral("●")), "the link state: a dot and the state");
		/* round ends in every state: Qt squares the corners when the radius (12) is more than half the height */
		if (auto *sidebar = window_.findChild<Sidebar *>(); sidebar && pill) {
			const QString connectedText = pill->toolTip(); /* the whole text (the pill may show it cut) */
			int least = pill->height();
			sidebar->showConnecting();
			QApplication::processEvents();
			least = std::min(least, pill->height());
			sidebar->showLinkError(QStringLiteral("Connection refused"));
			QApplication::processEvents();
			least = std::min(least, pill->height());
			sidebar->showDisconnected();
			QApplication::processEvents();
			least = std::min(least, pill->height());
			sidebar->showConnected(connectedText.mid(connectedText.indexOf(QStringLiteral("· ")) + 2));
			QApplication::processEvents();
			check(least >= 2 * 12 && qApp->styleSheet().contains(QLatin1String("QLabel#pill { border-radius: 12px")),
					"the link state: round ends in every state (its height never under twice the radius)");
			/* a long address: never cut ("Connected · " gives way first), the whole text in the tooltip (the port ran off
			 * the pill's end: "192.168.0.254:120"; then the address was cut in the middle: "...168.0.254:1209") */
			sidebar->showConnected(QStringLiteral("192.168.100.254:12090"));
			QApplication::processEvents();
			const QString shown = pill->text();
			const bool longKept = shown.contains(QLatin1String("192.168.100.254:12090"))
					&& pill->toolTip().contains(QStringLiteral("Connected · 192.168.100.254:12090"))
					&& pill->fontMetrics().horizontalAdvance(shown) <= pill->contentsRect().width();
			sidebar->showLinkError(QStringLiteral("Connection refused by 192.168.100.254:12090 after three tries, see the Log tab for the whole story"));
			QApplication::processEvents();
			const bool reasonWhole = pill->toolTip().endsWith(QLatin1String("the whole story"))
					&& pill->fontMetrics().horizontalAdvance(pill->text()) <= pill->contentsRect().width();
			sidebar->showConnected(connectedText.mid(connectedText.indexOf(QStringLiteral("· ")) + 2));
			QApplication::processEvents();
			check(longKept && reasonWhole,
					"the link state: a long address is shown whole (Connected gives way), the tooltip has all; a long reason "
					"fits, whole in the tooltip");
		}

		/* the log: one line per event; the map loaded once at start */
		auto *log = window_.findChild<QPlainTextEdit *>(QStringLiteral("eventLog"));
		check(log && log->lineWrapMode() == QPlainTextEdit::NoWrap && logText().count(QLatin1String("map loaded")) == 1,
				"log: one line per event (a path scrolls, it does not wrap); the map loaded once at start");

		/* Monitor: each box named, and the empty view says what will show there */
		if (auto *monitor = window_.findChild<MonitorTab *>()) {
			auto *frames = monitor->findChild<QPlainTextEdit *>();
			auto *function = monitor->findChild<QComboBox *>(QStringLiteral("monitorFunction"));
			bool named = false;
			for (QLabel *label : monitor->findChildren<QLabel *>())
				if (label->text() == QLatin1String("Address")) named = true;
			bool bytes = false;
			if (function) {
				function->setCurrentIndex(1);
				for (QLabel *label : monitor->findChildren<QLabel *>())
					if (label->text() == QLatin1String("Bytes")) bytes = true;
				function->setCurrentIndex(0);
			}
			check(frames && !frames->placeholderText().isEmpty() && named && bytes,
					"Monitor: Address and Count / Bytes named; the empty view says what shows there");
		}

		/* the chart's Y boxes in Auto: what the chart does, four digits (4.2, not 4.20007) but the whole part always
		 * (17420: 1.742e+04 was cut to ".742e+04" in the box); the axis: one step's decimals for every label */
		QLineEdit *yMin = nullptr;
		for (QLineEdit *box : window_.findChildren<QLineEdit *>())
			if (box->toolTip().startsWith(QLatin1String("Y range: the bottom"))) yMin = box;
		const bool shortY = yMin && (yMin->text().isEmpty()
				|| yMin->text() == ChartTab::yFieldText(yMin->text().toDouble(), false))
				&& ChartTab::yFieldText(4.20007, false) == QLatin1String("4.2")
				&& ChartTab::yFieldText(17420.3, false) == QLatin1String("17420")
				&& ChartTab::yFieldText(-1290.4, false) == QLatin1String("-1290")
				&& ChartTab::yFieldText(4.20007, true) == QLatin1String("4.20007");
		check(shortY && chartAxisLabel(14, 2, false) == QLatin1String("14") && chartAxisLabel(6, 2, false) == QLatin1String("6")
						&& chartAxisLabel(0.4, 0.2, false) == QLatin1String("0.4") && chartAxisLabel(1, 0.2, false) == QLatin1String("1.0"),
				"chart: Y boxes in Auto with four digits, the whole part always (17420); the axis labels with their step's "
				"decimals");

		/* Map editor: the Default box keeps its hint; the name tables' buttons say what they add; no empty checks box */
		if (auto *editorTab = window_.findChild<MapEditorTab *>()) {
			if (tabs) tabs->setCurrentIndex(MainWindow::TabMap);
			editorTab->selectRegister(model_->rows()[regRow(regs_.u8.name)].def.uid);
			QLineEdit *defaultBox = nullptr;
			for (QLineEdit *box : editorTab->findChildren<QLineEdit *>())
				if (box->property("emptyHint").toString().startsWith(QLatin1String("none: a number"))) defaultBox = box;
			check(defaultBox && defaultBox->placeholderText() == defaultBox->property("emptyHint").toString(),
					"Map editor: an empty Default still shows its hint after a register is selected");
			check(buttonWithText(*editorTab, QStringLiteral("+ Name")) && buttonWithText(*editorTab, QStringLiteral("− Name")),
					"Map editor: the value names' buttons say what they add (+ Name, − Name)");
			/* a value name's good key has no colour of its own: the palette's text, in either look */
			if (!regs_.enumU8.name.isEmpty()) {
				editorTab->selectRegister(model_->rows()[regRow(regs_.enumU8.name)].def.uid);
				auto *names = editorTab->findChild<QWidget *>(QStringLiteral("enumNames"));
				auto *cells = names ? names->findChild<QTableWidget *>() : nullptr;
				check(cells && cells->rowCount() > 0 && cells->item(0, 0)
								&& !cells->item(0, 0)->data(Qt::ForegroundRole).isValid(),
						"Map editor: a value name's key in the text colour of the look (none written into the cell)");
				editorTab->selectRegister(model_->rows()[regRow(regs_.u8.name)].def.uid);
			}
			auto *doc = window_.findChild<MapDocument *>();
			auto *issues = editorTab->findChild<QListWidget *>(QStringLiteral("mapIssues"));
			if (doc && issues) {
				const bool hiddenWhenClean = doc->issues().isEmpty() ? issues->isHidden() : !issues->isHidden();
				check(hiddenWhenClean, "Map editor: nothing found, no empty checks box (the title alone)");
			}
			auto *general = editorTab->findChild<QScrollArea *>(QStringLiteral("formScroll"));
			check(general != nullptr, "Map editor: the General form scrolls (its 18 rows set no minimum height)");
		}
		if (tabs) tabs->setCurrentIndex(tabBefore);
	}

	bool u8Greyed() const {
		/* the table's colour of a value not refreshed: the theme's muted one */
		const QColor color = u8Cell_.data(Qt::ForegroundRole).value<QColor>();
		return color.isValid() && color == Theme::colors().muted;
	}

	/* 9. hovering: the (i) beside a decoded value shows its fields, the rest
	 * of the value cell the usual tooltip (description, raw bytes, age) */
	void decodedFields() {
		const QModelIndex cell = firstDecodedValue();
		check(cell.isValid(), "a register with decoded fields");
		check(table_->isColumnHidden(RegisterModel::ColDecoded), "the Decoded column is hidden by default");
		table_->scrollTo(cell);
		QApplication::processEvents();
		const QRect rect = table_->visualRect(cell);
		const QString onInfo = tooltipAt(QPoint(rect.right() - 8, rect.center().y()));
		const QString onValue = tooltipAt(QPoint(rect.left() + 10, rect.center().y()));
		QToolTip::hideText();
		check(onInfo.contains(QLatin1String("<hr>")) && !onInfo.contains(QLatin1String("raw:")),
				"hover the (i): the decoded fields");
		check(onValue.contains(QLatin1String("raw:")), "hover the value: the usual tooltip (raw bytes, age)");
		table_->setCurrentIndex(cell);
		auto *detail = window_.findChild<QLabel *>(QStringLiteral("detail"));
		const QString detailText = detail ? detail->text() : QString();
		/* U+00C2 shows up where UTF-8 was read as Latin-1 */
		check(detailText.contains(QStringLiteral("Decoded")) && !detailText.contains(QChar(0x00C2)),
				"the line under the table: the decoded fields, no broken characters");
	}

	QModelIndex firstDecodedValue() const {
		const QAbstractItemModel *model = table_->model();
		for (int r = 0; r < model->rowCount(); r++) {
			const QModelIndex value = model->index(r, RegisterModel::ColValue);
			if (!value.siblingAtColumn(RegisterModel::ColDecoded).data().toString().isEmpty()) return value;
		}
		return {};
	}

	/* the tooltip the table shows when hovered at a point of its viewport */
	QString tooltipAt(const QPoint &at) {
		QToolTip::hideText();
		QHelpEvent event(QEvent::ToolTip, at, table_->viewport()->mapToGlobal(at));
		QApplication::sendEvent(table_->viewport(), &event);
		return QToolTip::text();
	}

	/* 10. quick write under the table: value box, enum list, bit buttons, a flag of a danger register */
	void quickWrite() {
		QuickWriteWidgets panel;
		panel.frame = window_.findChild<QFrame *>(QStringLiteral("quickWrite"));
		if (panel.frame) {
			panel.value = panel.frame->findChild<QLineEdit *>(QStringLiteral("qwValue"));
			panel.enumList = panel.frame->findChild<QComboBox *>(QStringLiteral("qwEnum"));
			for (QCheckBox *box : panel.frame->findChildren<QCheckBox *>())
				if (box->text() == QLatin1String("Bits")) panel.bits = box;
		}
		check(panel.complete(), "quick write: value box, enum list, Bits");
		if (!panel.complete()) return;
		quickWriteValue(panel);
		quickWriteBits(panel);
		quickWriteDangerFlag(panel);
		quickWriteFollowsLink(panel);
	}

	/* Disconnect: the panel says so at once and cannot write; Connect: it can again */
	void quickWriteFollowsLink(const QuickWriteWidgets &panel) {
		table_->setCurrentIndex(u8Cell_);
		QApplication::processEvents();
		QPushButton *disconnectButton = buttonWithText(QStringLiteral("Disconnect"));
		if (disconnectButton) disconnectButton->click();
		check(disconnectButton && !panel.value->isEnabled() && panel.shows(QStringLiteral("not connected")),
				"Disconnect: quick write disabled at once, it says \"not connected\"");
		QPushButton *connectButton = buttonWithText(QStringLiteral("Connect"));
		if (connectButton) connectButton->click();
		const bool enabledAgain = QTest::qWaitFor([&] {
			return panel.value->isEnabled() && !panel.shows(QStringLiteral("not connected"));
		}, 5000);
		check(connectButton && enabledAgain && cellShows(u8Cell_, QStringLiteral("0"), 5000),
				"Connect again: quick write enabled, the table polled again");
	}

	void quickWriteValue(const QuickWriteWidgets &panel) {
		if (panel.bits->isChecked()) panel.bits->setChecked(false);
		table_->setCurrentIndex(u8Cell_);
		/* on a busy machine the panel follows a frame or two later: wait for it */
		check(QTest::qWaitFor([&] { return panel.frame->isVisible() && panel.value->isEnabled(); }, 2000),
				"a RW register selected, Allow writes on: quick write shown and enabled");
		panel.value->setText(QStringLiteral("2"));
		QTest::keyClick(panel.value, Qt::Key_Return);
		check(u8Becomes(2), "value box + Enter: the device holds 2");
		quickWriteNamedValue(panel);
	}

	/* a u8 with named values: its list holds them, picking one writes it (the plain u8 has no list).
	 * The device starts from 0, and the name picked is the first of a value other than 0. */
	void quickWriteNamedValue(const QuickWriteWidgets &panel) {
		const bool plainHasNoList = panel.enumList->isHidden();
		const RegDef &def = regs_.enumU8;
		const QList<qint64> values = def.enumValues.keys();
		const auto nonZero = std::find_if(values.begin(), values.end(), [](qint64 v) { return v != 0; });
		const qint64 value = nonZero != values.end() ? *nonZero : -1;
		bool listed = false, written = false;
		if (!def.name.isEmpty() && value > 0) {
			other_.writeU8(def.addr, 0);
			table_->setCurrentIndex(valueCell(table_, def.name));
			listed = QTest::qWaitFor([&] {
				return panel.enumList->isVisible() && panel.enumList->count() == def.enumValues.size()
						&& listsItsNames(panel.enumList, def);
			}, 2000);
			const int entry = panel.enumList->findData(value);
			if (entry >= 0) {
				panel.enumList->setCurrentIndex(entry);
				emit panel.enumList->activated(entry);
				written = QTest::qWaitFor([&] { return other_.readU8(def.addr) == value; }, 3000);
			}
			other_.writeU8(def.addr, 0);
		}
		const QByteArray what = (def.name.isEmpty() || value <= 0
				? QStringLiteral("named values: the map has no writable u8 of the device bank with a name for a value"
						" other than 0")
				: QStringLiteral("named values: %1's list holds its %2 values, each with its name, picking %3 writes"
						" it; the plain u8 has no list").arg(def.name).arg(def.enumValues.size()).arg(value)).toUtf8();
		check(plainHasNoList && listed && written, what.constData());
		table_->setCurrentIndex(u8Cell_);
		QApplication::processEvents();
	}

	/* every entry of the list is one of the register's values, each value once, and its text shows that value's name */
	static bool listsItsNames(const QComboBox *list, const RegDef &def) {
		QList<qint64> seen;
		for (int i = 0; i < list->count(); i++) {
			bool isNumber = false;
			const qint64 value = list->itemData(i).toLongLong(&isNumber);
			if (!isNumber || !def.enumValues.contains(value) || seen.contains(value)
					|| !list->itemText(i).contains(def.enumValues.value(value)))
				return false;
			seen << value;
		}
		return true;
	}

	void quickWriteBits(const QuickWriteWidgets &panel) {
		panel.bits->setChecked(true);
		QApplication::processEvents();
		BitView *bitView = panel.bitView();
		check(bitView != nullptr, "Bits: the register drawn bit by bit");
		QTest::qWait(300); /* the cells follow the device's value */
		const int before = other_.readU8(regs_.u8.addr);
		if (bitView) QTest::mouseClick(bitView, Qt::LeftButton, {}, bitView->bitCell(1).center());
		check(u8Becomes(before ^ 2), "bit 1 clicked: flipped, the other bits kept (read-modify-write)");
		panel.bits->setChecked(false);
		other_.writeU8(regs_.u8.addr, 0);
	}

	/* CONFIG, a danger register of flags: a click on a flag asks, then flips only that bit */
	void quickWriteDangerFlag(const QuickWriteWidgets &panel) {
		table_->setCurrentIndex(valueCell(table_, QStringLiteral("CONFIG")));
		QApplication::processEvents();
		BitView *bitView = panel.bitView();
		const QRect flag = bitView ? bitView->fieldCell(QStringLiteral("MSG_ENABLE")) : QRect();
		check(bitView && flag.isValid(), "CONFIG: drawn as a register, MSG_ENABLE over its bit");
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) {
			saveScreenshots(panel);
			bitView = panel.bitView();
		}
		const int before = other_.readI16(PROTOCOL_CONFIG);
		const QString title = answerDialog(QStringLiteral("Write"), [&] {
			if (bitView) QTest::mouseClick(bitView, Qt::LeftButton, {}, flag.center());
		});
		const auto flipped = [&] { return other_.readI16(PROTOCOL_CONFIG) == (before ^ MSG_ENABLE_MASK); };
		check(title == confirmWriteTitle && QTest::qWaitFor(flipped, 3000),
				"MSG_ENABLE clicked on a danger register: confirm asked, bit 2 flipped");
		other_.writeI16(PROTOCOL_CONFIG, int16_t(before));
	}

	/* EVRE_TEST_SHOT=<path prefix>: pictures of the panel with fields and with bits, for a look */
	void saveScreenshots(const QuickWriteWidgets &panel) {
		const QString prefix = qEnvironmentVariable("EVRE_TEST_SHOT");
		window_.grab().save(prefix + QStringLiteral("_fields.png"));
		panel.bits->setChecked(true);
		QApplication::processEvents();
		window_.grab().save(prefix + QStringLiteral("_bits.png"));
		panel.bits->setChecked(false);
		QApplication::processEvents();
	}

	/* 11. groups: several at once; "Plot shown" charts what the table shows */
	void groups() {
		auto *button = window_.findChild<QPushButton *>(QStringLiteral("groups"));
		const QList<QCheckBox *> boxes =
				button && button->menu() ? button->menu()->findChildren<QCheckBox *>() : QList<QCheckBox *>();
		check(boxes.size() >= 2, "groups: a check box per group");
		if (boxes.size() < 2) return;
		/* the map's first group with an & ("Power & supply" in the example map) shows as it is,
		 * not "Power _supply": its box's text has the & doubled, so it is no mnemonic */
		const QString ampersandGroup = firstGroupWithAmpersand();
		QCheckBox *ampersandBox = nullptr;
		for (QCheckBox *box : boxes)
			if (!ampersandGroup.isEmpty() && box->text() == doubleAmpersands(ampersandGroup)) ampersandBox = box;
		const QByteArray what = QStringLiteral("a group with & in its name keeps its & (%1)")
				.arg(ampersandGroup.isEmpty() ? QStringLiteral("the map has no such group") : ampersandGroup).toUtf8();
		check(ampersandBox != nullptr, what.constData());
		/* ticked first, so the button starts with its name, the & doubled there too */
		QCheckBox *first = ampersandBox ? ampersandBox : boxes[0];
		QCheckBox *second = first == boxes[0] ? boxes[1] : boxes[0];
		first->setChecked(true);
		second->setChecked(true);
		const QString firstName = groupName(first);
		const int want = registersInGroup(firstName) + registersInGroup(groupName(second));
		const int upToAmpersand = firstName.contains(QLatin1Char('&')) ? int(firstName.indexOf(QLatin1Char('&'))) + 1 : 6;
		check(table_->model()->rowCount() == want
						&& button->text().startsWith(doubleAmpersands(firstName.left(upToAmpersand))),
				"two groups ticked: the table shows both, the button says so");
		plotShown();
		first->setChecked(false);
		second->setChecked(false);
		check(table_->model()->rowCount() == model_->rowCount(), "groups unticked: all registers again");
	}

	/* 13. the Map editor: every change through the document, with undo; the live values stay */
	void mapEditor() {
		auto *doc = window_.findChild<MapDocument *>();
		auto *tabs = window_.findChild<QTabWidget *>();
		auto *editorTab = window_.findChild<MapEditorTab *>();
		auto *mapTable = window_.findChild<QTableView *>(QStringLiteral("mapTable"));
		auto *issues = window_.findChild<QListWidget *>(QStringLiteral("mapIssues"));
		check(doc && tabs && editorTab && mapTable && issues, "Map editor: the tab, its table and its checks");
		if (!doc || !tabs || !editorTab || !mapTable || !issues) return;
		tabs->setCurrentIndex(MainWindow::TabMap);
		QTest::qWait(50);
		QAbstractItemModel *table = mapTable->model();
		check(table->rowCount() == model_->rows().size() && !doc->isModified(),
				"Map editor: a row per register, nothing changed yet");
		const int undoStart = doc->undoStack()->index();

		/* the unit of the u8 register: the Registers table follows, its value kept */
		const int u8Row = regRow(regs_.u8.name);
		const quint32 u8Uid = model_->rows()[u8Row].def.uid;
		const QByteArray valueBefore = model_->rows()[u8Row].raw;
		const bool validBefore = model_->rows()[u8Row].valid;
		table->setData(table->index(doc->indexOf(u8Uid), MapTableModel::ColUnit), QStringLiteral("rpm"));
		const RegisterModel::Row &after = model_->rows()[model_->rowOfUid(u8Uid)];
		check(after.def.unit == QLatin1String("rpm") && after.valid == validBefore && after.raw == valueBefore
						&& doc->isModified(),
				"edit a cell: the Registers table shows it at once, the live value is kept, the map is modified");

		/* a bulk edit: two rows selected, the group set on one goes to both, as one undo step */
		const int dangerIndex = doc->indexOf(model_->rows()[regRow(regs_.danger.name)].def.uid);
		const int u8Index = doc->indexOf(u8Uid);
		mapTable->selectionModel()->select(table->index(u8Index, 0),
				QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		mapTable->selectionModel()->select(table->index(dangerIndex, 0),
				QItemSelectionModel::Select | QItemSelectionModel::Rows);
		const int steps = doc->undoStack()->count();
		table->setData(table->index(u8Index, MapTableModel::ColGroup), QStringLiteral("Bulk group"));
		const quint32 dangerUid = doc->map().regs[dangerIndex].uid;
		check(doc->reg(u8Uid)->group == QLatin1String("Bulk group") && doc->reg(dangerUid)->group == QLatin1String("Bulk group")
						&& doc->undoStack()->count() == steps + 1,
				"bulk edit: the group set on one selected row goes to both, in one undo step");
		doc->undoStack()->undo();
		check(doc->reg(u8Uid)->group == regs_.u8.group && doc->reg(dangerUid)->group == regs_.danger.group,
				"undo: both groups back");
		doc->undoStack()->redo();
		check(doc->reg(dangerUid)->group == QLatin1String("Bulk group"), "redo: both in the new group again");
		doc->undoStack()->undo();

		/* + Register, Duplicate, copy and paste, delete */
		const int count = int(doc->map().regs.size());
		editorTab->addRegister(u8Uid);
		const bool added = doc->map().regs.size() == count + 1 && model_->rows().size() == count + 1;
		const QVector<quint32> selectedNew = selectedMapUids(mapTable, doc);
		const RegDef *created = selectedNew.size() == 1 ? doc->reg(selectedNew.front()) : nullptr;
		check(added && created && created->name.startsWith(QLatin1String("REG_")),
				"+ Register: a new register, selected, in the Registers table too");
		editorTab->deleteSelected();
		check(doc->map().regs.size() == count, "Delete: it is gone");
		editorTab->selectRegister(u8Uid);
		editorTab->duplicateSelected();
		const QVector<quint32> copies = selectedMapUids(mapTable, doc);
		const RegDef *copy = copies.size() == 1 ? doc->reg(copies.front()) : nullptr;
		check(copy && copy->name == regs_.u8.name + QStringLiteral("_2") && copy->addr != regs_.u8.addr
						&& copy->type == regs_.u8.type,
				"Duplicate: a copy named _2 at a free address");
		editorTab->copySelected();
		editorTab->paste();
		const QVector<quint32> pasted = selectedMapUids(mapTable, doc);
		const RegDef *third = pasted.size() == 1 ? doc->reg(pasted.front()) : nullptr;
		check(third && third->name == regs_.u8.name + QStringLiteral("_3") && doc->map().regs.size() == count + 2,
				"Copy and Paste: another copy, _3, at the next free address");

		/* the checks: a name used twice is an error, listed; a click on it selects the register */
		table->setData(table->index(doc->indexOf(third->uid), MapTableModel::ColName), regs_.danger.name);
		QListWidgetItem *error = nullptr;
		for (int i = 0; i < issues->count(); i++)
			if (issues->item(i)->text().contains(QLatin1String("is also"))) error = issues->item(i);
		check(error != nullptr, "checks: the name used twice is listed");
		if (error) {
			editorTab->selectRegister(0);
			emit issues->itemClicked(error);
			const QVector<quint32> selected = selectedMapUids(mapTable, doc);
			check(selected.size() == 1 && doc->reg(selected.front())->name == regs_.danger.name,
					"checks: a click selects that register");
		}
		/* the # column: each row's number in the map, a dot before it on a row the checks flag */
		const int thirdRow = doc->indexOf(third->uid), u8MapRow = doc->indexOf(u8Uid);
		check(table->headerData(MapTableModel::ColIssue, Qt::Horizontal, Qt::DisplayRole).toString() == QLatin1String("#")
						&& table->index(u8MapRow, MapTableModel::ColIssue).data().toString()
								== QString::number(u8MapRow + 1)
						&& table->index(thirdRow, MapTableModel::ColIssue).data().toString()
								== QStringLiteral("● %1").arg(thirdRow + 1),
				"# column: the row numbers; a dot before the number of a row the checks flag");

		/* nothing selected: the note in the middle instead of the form; a register selected: the form */
		auto *emptyNote = window_.findChild<QLabel *>(QStringLiteral("editorEmpty"));
		auto *pages = window_.findChild<QTabWidget *>(QStringLiteral("editorPages"));
		mapTable->clearSelection();
		check(emptyNote && pages && emptyNote->isVisibleTo(&window_) && !pages->isVisibleTo(&window_),
				"no register selected: the note shows instead of the form");
		editorTab->selectRegister(u8Uid);
		check(emptyNote && pages && !emptyNote->isVisibleTo(&window_) && pages->isVisibleTo(&window_),
				"a register selected: the form again");
		auto *addrChip = window_.findChild<QLabel *>(QStringLiteral("editorAddr"));
		auto *typeChip = window_.findChild<QLabel *>(QStringLiteral("editorType"));
		auto *liveDot = window_.findChild<QLabel *>(QStringLiteral("liveDot"));
		check(addrChip && typeChip && liveDot && addrChip->text() == addrText(regs_.u8.addr)
						&& typeChip->text() == QLatin1String("u8")
						&& QTest::qWaitFor([&] { return liveDot->property("state").toString() == QLatin1String("ok"); },
								1000),
				"the header card: its address and type chips, the live dot green with a value");
		if (addrChip) {
			const int addrX = addrChip->mapTo(&window_, QPoint()).x();
			editorTab->selectRegister(model_->rows()[regRow(regs_.danger.name)].def.uid); /* another name length */
			const int otherX = addrChip->mapTo(&window_, QPoint()).x();
			editorTab->selectRegister(u8Uid);
			check(regs_.danger.name.size() != regs_.u8.name.size() && otherX == addrX,
					"the address chip keeps its place whatever the name's length");
		}

		/* a 255-byte register: no bits to draw, so the pages are no taller than for a u8 one (a tab widget is as tall
		 * as its tallest page: the bit strip of 2040 bits made the window taller than the screen, the sidebar with it) */
		if (pages) {
			const int u8Height = pages->minimumSizeHint().height();
			doc->edit(QStringLiteral("bytes"), [&](DeviceMap &map) {
				for (RegDef &def : map.regs)
					if (def.uid == u8Uid) {
						def.type = RegType::Bytes;
						def.size = 255;
					}
			});
			check(pages->minimumSizeHint().height() <= u8Height,
					"a 255-byte register selected: the editor pages keep their height (the window and sidebar are not stretched)");
			doc->undoStack()->undo();
		}

		/* a page the selection cannot have: the tab stays where it is, with a warning sign and why as its tooltip,
		 * and the page is a note that says why */
		auto noteShown = [&](const QString &why) {
			for (QLabel *note : window_.findChildren<QLabel *>(QStringLiteral("editorEmpty")))
				if (note->isVisibleTo(&window_) && note->text().contains(why)) return true;
			return false;
		};
		const int valuesPage = 1, fieldsPage = 2;
		if (pages) {
			const int pageBefore = pages->currentIndex();
			pages->setCurrentIndex(fieldsPage);
			auto *strip = window_.findChild<QWidget *>(QStringLiteral("fieldStrip"));
			const int notesPage = 3;
			const int fieldsWidth = pages->tabBar()->tabRect(fieldsPage).width();
			const int notesX = pages->tabBar()->tabRect(notesPage).x();
			check(pages->tabIcon(fieldsPage).isNull() && pages->tabToolTip(fieldsPage).isEmpty() && strip
							&& strip->isVisibleTo(&window_),
					"an integer register: the Bit fields tab has no sign, its page the bit strip");
			doc->edit(QStringLiteral("bytes"), [&](DeviceMap &map) {
				for (RegDef &def : map.regs)
					if (def.uid == u8Uid) def.type = RegType::Bytes;
			});
			const QString why = pages->tabToolTip(fieldsPage);
			check(pages->tabBar()->tabRect(fieldsPage).width() == fieldsWidth
							&& pages->tabBar()->tabRect(notesPage).x() == notesX,
					"the sign on the Bit fields tab: the tab keeps its width, Notes its place");
			check(!pages->tabBar()->drawBase(), "the pages' tab bar draws no base line, as the other tab bars");
			check(pages->currentIndex() == fieldsPage && !pages->tabIcon(fieldsPage).isNull()
							&& why.contains(QLatin1String("bytes")) && noteShown(why) && !strip->isVisibleTo(&window_),
					"a bytes register on the Bit fields page: it stays there, the tab has a sign and why as its tooltip, "
					"the page says why");
			check(why.count(QLatin1Char('\n')) == 1, "the reason: two lines, what is wrong and what is needed");
			doc->undoStack()->undo();
			check(pages->currentIndex() == fieldsPage && pages->tabIcon(fieldsPage).isNull()
							&& pages->tabToolTip(fieldsPage).isEmpty() && strip->isVisibleTo(&window_),
					"an integer register again: no sign, the bit strip back");

			/* two selected: Values (and Bit fields) say to select one */
			pages->setCurrentIndex(valuesPage);
			const int dangerMapRow = doc->indexOf(model_->rows()[regRow(regs_.danger.name)].def.uid);
			mapTable->selectionModel()->select(table->index(doc->indexOf(u8Uid), 0),
					QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
			mapTable->selectionModel()->select(table->index(dangerMapRow, 0),
					QItemSelectionModel::Select | QItemSelectionModel::Rows);
			const QString severalWhy = pages->tabToolTip(valuesPage);
			check(pages->currentIndex() == valuesPage && !pages->tabIcon(valuesPage).isNull()
							&& !pages->tabIcon(fieldsPage).isNull() && !severalWhy.isEmpty() && noteShown(severalWhy),
					"two registers selected on the Values page: it stays there, the tab has a sign, the page says why");
			editorTab->selectRegister(u8Uid);
			auto *names = window_.findChild<QWidget *>(QStringLiteral("enumNames"));
			check(pages->tabIcon(valuesPage).isNull() && names && names->isVisibleTo(&window_),
					"one register again: the Values page is the names again");
			pages->setCurrentIndex(pageBefore);
		}

		/* the live line stays one line, however long: the pages under it do not move */
		auto *liveLine = window_.findChild<QLabel *>(QStringLiteral("editorLive"));
		if (liveLine && pages) {
			QTest::qWait(400); /* the live line follows every 250 ms */
			const int pagesTop = pages->mapTo(&window_, QPoint()).y();
			const QString longUnit = QStringLiteral("LONG_UNIT_").repeated(30);
			doc->edit(QStringLiteral("long unit"), [&](DeviceMap &map) {
				for (RegDef &def : map.regs)
					if (def.uid == u8Uid) def.unit = longUnit;
			});
			QTest::qWait(400);
			check(liveLine->toolTip().contains(longUnit) && liveLine->height() < 2 * liveLine->fontMetrics().height()
							&& pages->mapTo(&window_, QPoint()).y() == pagesTop,
					"a live line longer than the panel: one line, cut short, whole in its tooltip; the pages stay put");
			doc->undoStack()->undo();
		}

		/* the Values page: a name for a value of the u8 register, shown decoded on the Registers tab */
		auto *enumTable = window_.findChild<QWidget *>(QStringLiteral("enumNames"));
		auto *enumCells = enumTable ? enumTable->findChild<QTableWidget *>() : nullptr;
		check(enumCells != nullptr, "Values page: the table of value names");
		if (enumCells) {
			const int rows = enumCells->rowCount();
			const QByteArray raw = model_->rows()[model_->rowOfUid(u8Uid)].raw;
			const qint64 now = raw.isEmpty() ? 0 : qint64(quint8(raw[0]));
			enumCells->insertRow(rows);
			enumCells->setItem(rows, 0, new QTableWidgetItem(QString::number(now)));
			enumCells->setItem(rows, 1, new QTableWidgetItem(QStringLiteral("named-now")));
			const RegDef *named = doc->reg(u8Uid);
			check(named && named->enumValues.value(now) == QLatin1String("named-now"),
					"Values page: a name typed in goes into the map");
			const QModelIndex decoded = model_->index(model_->rowOfUid(u8Uid), RegisterModel::ColDecoded);
			check(cellShows(decoded, QStringLiteral("named-now")),
					"Values page: the Registers table decodes the live value with it at once");
			auto *live = window_.findChild<QLabel *>(QStringLiteral("editorLiveDetail"));
			check(QTest::qWaitFor([&] { return live && live->text().contains(QLatin1String("named-now")); }, 1000),
					"the live value's decoded line shows it too");
		}

		/* the Bit fields page: a drag across bits makes a field */
		auto *fieldEditor = window_.findChild<FieldEditor *>();
		auto *strip = window_.findChild<BitView *>(QStringLiteral("fieldStrip"));
		check(fieldEditor && strip, "Bit fields page: the bit strip");
		if (fieldEditor && strip) {
			const int fieldsBefore = int(doc->reg(u8Uid)->fields.size());
			emit strip->bitsChosen(4, 3);
			const RegDef *withField = doc->reg(u8Uid);
			const bool made = withField->fields.size() == fieldsBefore + 1
					&& std::any_of(withField->fields.begin(), withField->fields.end(),
							[](const BitField &f) { return f.lsb == 4 && f.width == 3; });
			check(made, "Bit fields page: bits 6:4 dragged across are a new field");
			auto *fieldCells = window_.findChild<QTableWidget *>(QStringLiteral("fieldTable"));
			check(fieldCells && fieldCells->rowCount() == fieldsBefore + 1, "the field table lists it");
		}

		/* the Registers tab's "Edit definition" opens it here */
		tabs->setCurrentIndex(MainWindow::TabRegisters);
		auto *registersTab = window_.findChild<RegistersTab *>();
		if (registersTab) emit registersTab->editDefinitionRequested(dangerUid, 0);
		const QVector<quint32> shown = selectedMapUids(mapTable, doc);
		check(tabs->currentIndex() == MainWindow::TabMap && shown.size() == 1 && shown.front() == dangerUid,
				"Registers tab, Edit definition: the Map editor shows that register");

		/* Export and Import CSV: the map as a sheet, and back in as one undo step */
		{
			QTemporaryDir folder;
			const QString csv = folder.filePath(QStringLiteral("map.csv")), md = folder.filePath(QStringLiteral("map.md"));
			QString err;
			const bool exported = editorTab->exportTo(QStringLiteral("csv"), csv, QString(), err)
					&& editorTab->exportTo(QStringLiteral("md"), md, QString(), err);
			check(exported && QFileInfo(md).size() > 1000, "Export: the map as CSV and as a Markdown specification");
			const int before = int(doc->map().regs.size());
			const int steps = doc->undoStack()->count();
			const bool imported = editorTab->importCsvFrom(csv, true, err);
			check(imported && doc->map().regs.size() == before && doc->undoStack()->count() == steps + 1,
					"Import CSV: the same registers back, in one undo step");
			doc->undoStack()->undo();
		}

		/* all undone: the map as loaded, not modified */
		doc->undoStack()->setIndex(undoStart);
		check(!doc->isModified() && doc->map().regs.size() == count && doc->reg(u8Uid)->unit == regs_.u8.unit,
				"undo to the start: the map as loaded, nothing to save");

		/* the device table: written for a map the EVRe library can serve, else refused with the reason */
		{
			QTemporaryDir folder;
			const QString table = folder.filePath(QStringLiteral("map_table.h"));
			QString err;
			const bool ok = editorTab->exportTo(QStringLiteral("table"), table, QString(), err);
			check(ok ? QFileInfo(table).size() > 1000 : err.contains(QLatin1String("EVRe library")) && !QFileInfo::exists(table),
					"Export: the device table for the EVRe library (or why the map cannot be one)");
		}
		tabs->setCurrentIndex(MainWindow::TabRegisters);
	}

	/* 14. a write past the map's max asks first; a bit field goes on the chart as a math line */
	void limitsAndFields() {
		/* the Map editor step added and removed registers: the table was reset, its old indexes are gone */
		u8Cell_ = valueCell(table_, regs_.u8.name);
		const RegDef u8 = model_->rows()[regRow(regs_.u8.name)].def; /* a copy: the rows may move while a dialog is open */
		if (u8.hasMax()) {
			const QString past = QString::number(qint64(u8.max) + 1);
			QString title = enterAnswering(typeInto(u8Cell_, past), QStringLiteral("Cancel"));
			check(title == QLatin1String("Outside the map's limits") && other_.readU8(regs_.u8.addr) != u8.max + 1,
					"past the map's max: asked first, Cancel writes nothing");
			title = enterAnswering(typeInto(u8Cell_, past), QStringLiteral("Write anyway"));
			check(title == QLatin1String("Outside the map's limits") && u8Becomes(int(u8.max) + 1),
					"... Write anyway: written");
			other_.writeU8(regs_.u8.addr, 0);
			(void) u8Becomes(0);
		} else {
			check(false, "the map gives the u8 register a max (the example map: FAN_SPEED 0 … 100)");
		}
		/* the protocol's CONFIG register has fields: its first one as a line of its own */
		auto *chartTab = window_.findChild<ChartTab *>();
		auto *view = window_.findChild<ChartView *>();
		const int configRow = [this] {
			for (int i = 0; i < model_->rows().size(); i++)
				if (model_->rows()[i].def.addr == PROTOCOL_CONFIG) return i;
			return -1;
		}();
		if (chartTab && view && configRow >= 0 && !model_->rows()[configRow].def.fields.isEmpty()) {
			const RegDef config = model_->rows()[configRow].def;
			auto *registersTab = window_.findChild<RegistersTab *>();
			if (registersTab) emit registersTab->plotFieldRequested(configRow, 0);
			const QString name = QStringLiteral("ƒ ") + config.name + QLatin1Char('.') + config.fields[0].name;
			check(QTest::qWaitFor([&] { return lineKey(view, name) >= 0; }, 2000),
					"Plot a field: a line of its own on the chart (bits of the register)");
		} else {
			check(false, "the map's CONFIG register (0xA004) has bit fields");
		}
	}

	/* 15. polling as fast as it goes (interval 0) keeps going through an edit of the map */
	void pollingSurvivesEdits() {
		auto *sidebar = window_.findChild<Sidebar *>();
		auto *doc = window_.findChild<MapDocument *>();
		if (!sidebar || !doc || regs_.volts.name.isEmpty()) {
			check(false, "polling at max through an edit: the sidebar, the map and a moving register");
			return;
		}
		const double interval = sidebar->pollIntervalMs();
		sidebar->setPollInterval(0);
		const quint32 uid = model_->rows()[regRow(regs_.volts.name)].def.uid;
		doc->edit(QStringLiteral("test"), [uid](DeviceMap &map) {
			for (RegDef &def : map.regs)
				if (def.uid == uid) def.desc = QStringLiteral("edited while polling at max");
		});
		/* the fake device moves this register: its value keeps changing while polls go on. First the table
		 * takes the last value read before the edit (that change proves nothing), then it must change again,
		 * several times */
		QTest::qWait(300);
		QByteArray last = model_->rows()[regRow(regs_.volts.name)].raw;
		int changes = 0;
		(void) QTest::qWaitFor([&] {
			const QByteArray now = model_->rows()[regRow(regs_.volts.name)].raw;
			if (now != last) {
				changes++;
				last = now;
			}
			return changes >= 3;
		}, 3000);
		check(changes >= 3, "polling at interval 0 goes on after an edit of the map");
		doc->undoStack()->undo();
		sidebar->setPollInterval(interval);
	}

	/* the Registers table's row of a register, by name */
	int regRow(const QString &name) const {
		for (int i = 0; i < model_->rows().size(); i++)
			if (model_->rows()[i].def.name == name) return i;
		return -1;
	}

	/* the uids of the rows selected in the Map editor */
	static QVector<quint32> selectedMapUids(QTableView *table, MapDocument *doc) {
		QVector<int> rows;
		for (const QModelIndex &index : table->selectionModel()->selectedRows())
			rows << static_cast<QSortFilterProxyModel *>(table->model())->mapToSource(index).row();
		std::sort(rows.begin(), rows.end());
		QVector<quint32> uids;
		for (int row : rows) uids << doc->map().regs[row].uid;
		return uids;
	}

	/* the first group of the map with an & in its name, empty if none has one */
	QString firstGroupWithAmpersand() const {
		for (const RegisterModel::Row &row : model_->rows())
			if (row.def.group.contains(QLatin1Char('&'))) return row.def.group;
		return {};
	}

	int registersInGroup(const QString &group) const {
		return int(std::count_if(model_->rows().begin(), model_->rows().end(),
				[&](const RegisterModel::Row &row) { return row.def.group == group; }));
	}

	/* "Plot shown" puts every plottable register the table shows on the chart; pressed again, takes them off */
	void plotShown() {
		auto *button = window_.findChild<QPushButton *>(QStringLiteral("plotShown"));
		const int plottable = plottableRegistersShown();
		if (button) button->click();
		check(button && plottedCount() == plottable,
				"Plot shown: every plottable register shown is on the chart (not one the map marks \"plot\": false)");
		check(button && button->text() == QLatin1String("Unplot shown"), "... the button then says Unplot shown");
		if (button) button->click();
		check(button && plottedCount() == 0 && button->text() == QLatin1String("Plot shown"),
				"Unplot shown: all off the chart, the button says Plot shown again");
	}

	/* the registers shown that may be a line: numbers the map lets plot (not "plot": false, as DEVICE_ID) */
	int plottableRegistersShown() const {
		int plottable = 0;
		for (int i = 0; i < table_->model()->rowCount(); i++) {
			const QString name = table_->model()->index(i, RegisterModel::ColName).data().toString();
			for (const RegisterModel::Row &row : model_->rows())
				if (row.def.name == name && row.def.canPlot()) plottable++;
		}
		return plottable;
	}

	int plottedCount() const {
		return int(std::count_if(model_->rows().begin(), model_->rows().end(),
				[](const RegisterModel::Row &row) { return row.plot; }));
	}

	/* many fast lines, on a chart of its own (no device): drawn on threads, the same picture as on one; a
	 * one-sample spike in an hour of 500 Hz samples still shown; the samples' budget shared by the lines */
	void chartManyLines() {
		QWidget host; /* the chart at an odd offset, as in the window: its stripes need not fall on device pixels */
		host.resize(1300, 540);
		auto *view = new ChartView(&host);
		view->setGeometry(13, 7, 1270, 520);
		view->setClock([] { return 3600.0; }, 0);
		view->setSmooth(false);
		view->setMemory(3600);
		view->setWindow(60);
		constexpr int LINES = 60, HZ = 500;
		for (int k = 0; k < LINES; k++) {
			view->addSeries(k, QStringLiteral("line %1").arg(k), QString(), QColor::fromHsv(k * 360 / LINES, 200, 230));
			for (int i = 0; i < 60 * HZ; i++) {
				const double t = 3540.0 + double(i) / HZ;
				view->append(k, t, std::sin(t * (1 + k % 7)) * (k + 1) + k);
			}
		}
		view->setDrawThreads(1);
		const QImage one = host.grab().toImage().convertToFormat(QImage::Format_RGB32);
		view->setDrawThreads(0);
		const QImage many = host.grab().toImage().convertToFormat(QImage::Format_RGB32);
		int worst = 0, differ = 0;
		for (int y = 0; y < one.height() && one.size() == many.size(); y++) {
			const QRgb *a = reinterpret_cast<const QRgb *>(one.constScanLine(y));
			const QRgb *b = reinterpret_cast<const QRgb *>(many.constScanLine(y));
			for (int x = 0; x < one.width(); x++) {
				const int d = std::max({ std::abs(qRed(a[x]) - qRed(b[x])), std::abs(qGreen(a[x]) - qGreen(b[x])),
						std::abs(qBlue(a[x]) - qBlue(b[x])) });
				worst = std::max(worst, d);
				if (d > 3) differ++;
			}
		}
		std::printf("     (%d lines on threads vs on one: worst channel difference %d, %d pixels above 3)\n", LINES, worst,
				differ);
		/* where several lines cross, blending them on a stripe first rounds a little differently: a few pixels, never
		 * a seam (a column of them) */
		check(one.size() == many.size() && differ <= 200 && worst <= 32,
				"chart, many lines: drawn on threads in stripes, the same picture as on one thread (no seam, at most a "
				"few pixels rounded apart where lines cross)");

		/* an hour of 500 Hz samples, all 0 but one: the chunks keep it, the view reaches it */
		ChartView spike;
		spike.resize(1270, 520);
		spike.setClock([] { return 3600.0; }, 0);
		spike.setSmooth(false);
		spike.setMemory(3600);
		spike.setWindow(3600);
		spike.addSeries(1, QStringLiteral("spike"), QString(), Qt::red);
		for (int i = 0; i < 3600 * HZ; i++) spike.append(1, double(i) / HZ, i == 1800 * HZ + 3 ? 1.0 : 0.0);
		spike.grab();
		check(spike.pointsKept(1) == 3600 * HZ && spike.yHi() >= 1.0 && spike.yLo() <= 0.0,
				"chart, an hour of 500 Hz (1.8 million samples) in view: a one-sample spike still reaches the top");

		/* the budget: shared by the lines; a line past its share keeps less than the memory, and says so */
		ChartView budget;
		budget.setClock([] { return 1000.0; }, 0);
		budget.setMemory(3600);
		budget.setRamBudget(ChartView::MIN_RAM_MB); /* 256 MB: 11.6 M samples, under the 16 of the largest chunks each */
		for (int k = 0; k < 1000; k++) budget.addSeries(k, QStringLiteral("l%1").arg(k), QString(), Qt::blue);
		const qsizetype share = budget.pointsPerLine();
		for (int i = 0; i < 100000; i++) budget.append(0, double(i) / HZ, i % 100);
		check(share == 65536 && budget.pointsKept(0) <= share && budget.pointsKept(0) >= share / 2 && budget.memoryFull(),
				"chart, the samples' budget: 1000 lines share it (65536 samples each at least), a line past its share "
				"drops its oldest and the memory strip says the memory is full");

		/* what the samples take: a line long at its share holds about that (23 bytes a sample), not up to twice it
		 * (trimmed from the front, its arrays doubled: a RAM of 1 GB took 2 GB) */
		for (int i = 100000; i < 400000; i++) budget.append(0, double(i) / HZ, i % 100);
		const qint64 held = budget.bytesHeld(), share23 = qint64(share) * 23;
		if (held > share23 * 11 / 10)
			std::printf("     (samples' memory: %lld bytes held for a share of %lld)\n", (long long) held, (long long) share23);
		check(held <= share23 * 11 / 10, "chart, the samples' memory: a line at its share holds about its share of the "
				"RAM, not up to twice it");

		/* the memory full on many lines that fill together: their trims spread over frames, a few million samples moved
		 * a frame (65 lines at RAM 1 GB moved 0.9 GB in one frame: 85 ms every 86 s), none past its share */
		ChartView spread;
		spread.setClock([] { return 1000.0; }, 0);
		spread.setMemory(3600);
		spread.setRamBudget(ChartView::MIN_RAM_MB);
		for (int k = 0; k < 64; k++) spread.addSeries(k, QStringLiteral("s%1").arg(k), QString(), Qt::blue);
		const qsizetype spreadShare = spread.pointsPerLine();
		const qsizetype soft = spreadShare - spreadShare / 16;
		qsizetype n = 0;
		for (; n < soft; n++) /* a line is trimmed at its next sample from there (dropExpired comes before the append) */
			for (int k = 0; k < 64; k++) spread.append(k, double(n) / HZ, double(n % 50));
		auto trimmed = [&] {
			int count = 0;
			for (int k = 0; k < 64; k++) count += spread.pointsKept(k) < soft ? 1 : 0;
			return count;
		};
		for (int k = 0; k < 64; k++) spread.append(k, double(n) / HZ, 1.0); /* every line at its soft share: one frame */
		n++;
		const int firstFrame = trimmed();
		int frames = 1;
		while (trimmed() < 64 && frames < 20) {
			spread.frame();
			for (int k = 0; k < 64; k++) spread.append(k, double(n) / HZ, 1.0);
			n++;
			frames++;
		}
		bool withinShare = true;
		for (int k = 0; k < 64; k++) withinShare = withinShare && spread.pointsKept(k) <= spreadShare;
		std::printf("     (64 full lines of %lld: %d trimmed in the first frame, all after %d frames)\n",
				(long long) spreadShare, firstFrame, frames);
		check(firstFrame >= 1 && firstFrame <= 30 && trimmed() == 64 && frames <= 6 && withinShare,
				"chart, the memory full on 64 lines at once: their trims spread over a few frames, not all in one, none "
				"past its share");

		/* lines filling together grow their arrays at different moments (each by its own step, 2 to 2.44 times): no
		 * sample makes them all grow (all at once, 64 lines at RAM 1 GB moved 0.5 GB in one frame at their last growth) */
		ChartView growing;
		growing.setClock([] { return 1000.0; }, 0);
		growing.setMemory(3600);
		growing.setRamBudget(ChartView::MIN_RAM_MB);
		for (int k = 0; k < 32; k++) growing.addSeries(k, QStringLiteral("g%1").arg(k), QString(), Qt::blue);
		const qint64 heldFirst = growing.bytesHeld();
		qint64 heldGrown = heldFirst, largestGrowth = 0;
		for (int i = 0; i < 70000; i++) {
			for (int k = 0; k < 32; k++) growing.append(k, double(i) / HZ, double(i % 30));
			const qint64 after = growing.bytesHeld();
			largestGrowth = std::max(largestGrowth, after - heldGrown);
			heldGrown = after;
		}
		const double growthShare = double(largestGrowth) / double(std::max<qint64>(1, heldGrown - heldFirst));
		std::printf("     (32 lines filling together: the most their arrays grew at one sample, %.0f%% of all)\n",
				growthShare * 100);
		check(growthShare <= 0.25, "chart, lines filling together: their arrays grow at different moments, not all at "
				"one sample");

		/* the RAM lowered with the memory full: a line goes down to its new share at its next sample, in one go (an
		 * eighth at each sample, each moving and copying the whole line: 1 GB to 512 MB held the window 3 s) */
		ChartView lower;
		lower.setClock([] { return 1000.0; }, 0);
		lower.setMemory(3600);
		lower.setRamBudget(512);
		for (int k = 0; k < 8; k++) lower.addSeries(k, QStringLiteral("r%1").arg(k), QString(), Qt::blue);
		const qsizetype wide = lower.pointsPerLine();
		for (qsizetype i = 0; i < wide + 4096; i++) lower.append(0, double(i) / HZ, double(i % 100));
		lower.setRamBudget(256);
		const qsizetype narrow = lower.pointsPerLine();
		QElapsedTimer trimTime;
		trimTime.start();
		lower.append(0, double(wide + 4096) / HZ, 1.0);
		const double trimMs = trimTime.nsecsElapsed() / 1e6;
		const qsizetype keptNow = lower.pointsKept(0);
		const qint64 heldNow = lower.bytesHeld();
		const bool down = keptNow <= narrow && keptNow >= narrow / 2 && heldNow <= qint64(narrow) * 23 * 11 / 10;
		std::printf("     (RAM 512 -> 256 MB, a full line: %lld samples kept of a share of %lld after one sample, %.0f ms)\n",
				(long long) keptNow, (long long) narrow, trimMs);
		check(down, "chart, the RAM lowered with the memory full: a line goes down to its new share at its next sample, "
				"in one go, and lets its room go");

		/* the memory needed: the lines' rates now times the Memory; the note, and over the RAM what fits */
		ChartView need;
		need.setClock([] { return 100.0; }, 0);
		need.setMemory(60);
		for (int k = 0; k < 2; k++) {
			need.addSeries(k, QStringLiteral("n%1").arg(k), QString(), Qt::green);
			for (int i = 0; i < 10 * HZ; i++) need.append(k, 90.0 + double(i) / HZ, i);
		}
		const double expected = 2.0 * HZ * 60 * 23; /* 2 lines x 500/s x 60 s x 23 bytes */
		bool overSmall = true, overBig = false;
		const QString small = ChartTab::ramNeedText(need.bytesNeeded(), 2048, 60, overSmall);
		const QString big = ChartTab::ramNeedText(qint64(3) * 1024 * 1024 * 1024, 2048, 1800, overBig);
		std::printf("     (needed %lld bytes, expected %.0f; \"%s\", \"%s\")\n", (long long) need.bytesNeeded(), expected,
				qPrintable(small), qPrintable(big));
		check(std::fabs(double(need.bytesNeeded()) - expected) < expected * 0.01 && small == QLatin1String("needs 1 MB")
						&& !overSmall && overBig && big == QLatin1String("needs 3.0 GB, keeps 20 min"),
				"chart, the memory needed: the lines' rates times the Memory (2 x 500/s x 60 s = 1 MB); over the RAM "
				"it says what fits (3 GB for 30 min in 2 GB: keeps 20 min)");

		/* the RAM box on the Chart tab: 2 GB unless set, a size typed in MB or GB, saved; not a size: back */
		auto *ram = window_.findChild<QComboBox *>(QStringLiteral("chartRam"));
		auto *chartView = window_.findChild<ChartView *>();
		const bool defaultShown = ram && chartView && chartView->ramBudget() == ChartView::DEFAULT_RAM_MB
				&& ram->currentText() == QLatin1String("2 GB");
		auto typeRam = [&](const QString &text) {
			ram->lineEdit()->setText(text);
			emit ram->lineEdit()->editingFinished();
		};
		if (ram && chartView) typeRam(QStringLiteral("1.5 GB"));
		const bool typed = ram && chartView && chartView->ramBudget() == 1536 && ram->currentText() == QLatin1String("1536 MB")
				&& QSettings().value(QStringLiteral("chart/ramMB")).toInt() == 1536;
		if (ram) typeRam(QStringLiteral("lots"));
		const bool refused = ram && chartView && chartView->ramBudget() == 1536 && ram->currentText() == QLatin1String("1536 MB");
		if (ram) typeRam(QStringLiteral("10"));
		const bool least = ram && chartView && chartView->ramBudget() == ChartView::MIN_RAM_MB;
		if (ram) typeRam(QStringLiteral("2048"));
		check(defaultShown && typed && refused && least && chartView->ramBudget() == ChartView::DEFAULT_RAM_MB
						&& ram->currentText() == QLatin1String("2 GB"),
				"chart, RAM: 2 GB by default; 1.5 GB typed is 1536 MB, saved; \"lots\" goes back; 10 MB is 256 MB at "
				"least; 2048 shows 2 GB");
	}

	/* two pictures alike: the share of 8 x 8 pixel blocks whose mean colour is within `level` (antialiasing differs) */
	static double blocksAlike(const QImage &a, const QImage &b, int level) {
		if (a.size() != b.size()) return 0;
		int alike = 0, blocks = 0;
		for (int by = 0; by + 8 <= a.height(); by += 8) {
			for (int bx = 0; bx + 8 <= a.width(); bx += 8) {
				long sum[2][3] = {};
				for (int y = by; y < by + 8; y++)
					for (int x = bx; x < bx + 8; x++) {
						const QRgb pa = a.pixel(x, y), pb = b.pixel(x, y);
						sum[0][0] += qRed(pa), sum[0][1] += qGreen(pa), sum[0][2] += qBlue(pa);
						sum[1][0] += qRed(pb), sum[1][1] += qGreen(pb), sum[1][2] += qBlue(pb);
					}
				int worst = 0;
				for (int c = 0; c < 3; c++) worst = std::max(worst, int(std::abs(sum[0][c] - sum[1][c]) / 64));
				alike += worst <= level ? 1 : 0;
				blocks++;
			}
		}
		return blocks ? double(alike) / blocks : 0;
	}


	/* Fast EVRe 5.5: the Map settings' Streams page: the map's streams with their window, rate and channels; a stream
	 * and a channel added; the checks live under them; OK one undo step */
	void mapStreamsPage() {
		DeviceMap map;
		QString err;
		const bool loaded = map.load(QCoreApplication::applicationDirPath() + QStringLiteral("/maps/example_fast.json"), err);
		MapDocument doc;
		doc.reset(map);
		MapSettingsDialog dialog(&doc);
		dialog.resize(760, 660);
		dialog.show();
		(void) QTest::qWaitForWindowExposed(&dialog);
		auto *list = dialog.findChild<QListWidget *>(QStringLiteral("fastStreamList"));
		auto *addr = dialog.findChild<QLineEdit *>(QStringLiteral("streamAddr"));
		auto *channels = dialog.findChild<QTableWidget *>(QStringLiteral("streamChannels"));
		auto *record = dialog.findChild<QLabel *>(QStringLiteral("streamRecord"));
		auto *add = dialog.findChild<QPushButton *>(QStringLiteral("addStream"));
		const bool shown = loaded && list && addr && channels && record && list->count() == 1
				&& list->item(0)->text() == QLatin1String("ADC") && addr->text() == QLatin1String("0xDC00")
				&& channels->rowCount() == 2 && channels->item(1, 0)->text() == QLatin1String("V_BUS")
				&& record->text() == QStringLiteral("A sample: 4 bytes · at most 254 samples a block") && dialog.streamChecks().isEmpty()
				&& add && add->cursor().shape() == Qt::PointingHandCursor && !add->toolTip().isEmpty();
		std::printf("  Streams page: %d stream(s), \"%s\", %d channel(s), \"%s\"\n", list ? list->count() : -1,
				addr ? qPrintable(addr->text()) : "", channels ? channels->rowCount() : -1, record ? qPrintable(record->text()) : "");
		check(shown, "map settings, Streams: the map's stream ADC, its window 0xDC00, its 2 channels, a sample's bytes and "
				"samples a block; no check fails; its buttons look clickable, with tooltips");
		if (!shown) return;
		/* in English and Arabic: every label and button of the page whole at the dialog's least size */
		bool fits = true;
		QString notes;
		for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
			language::apply(*qApp, code);
			MapSettingsDialog other(&doc);
			other.resize(other.minimumSize());
			other.show();
			(void) QTest::qWaitForWindowExposed(&other);
			if (auto *pages = other.findChild<QTabWidget *>()) pages->setCurrentWidget(other.streamsPage());
			QApplication::processEvents();
			for (QLabel *label : other.streamsPage()->findChildren<QLabel *>()) {
				if (label->wordWrap() || label->text().isEmpty() || !label->isVisible()) continue;
				if (label->fontMetrics().horizontalAdvance(label->text()) > label->width()) {
					fits = false;
					notes += QStringLiteral(" %1: \"%2\"").arg(code, label->text());
				}
			}
			if (auto *table = other.findChild<QTableWidget *>(QStringLiteral("streamChannels")))
				for (int c = 0; c + 1 < table->columnCount(); c++) /* the headers whole (the last stretches) */
					if (table->horizontalHeader()->fontMetrics().horizontalAdvance(table->horizontalHeaderItem(c)->text())
							> table->columnWidth(c) - 8) {
						fits = false;
						notes += QStringLiteral(" %1: the column \"%2\"").arg(code, table->horizontalHeaderItem(c)->text());
					}
			for (QPushButton *button : other.streamsPage()->findChildren<QPushButton *>())
				if (button->fontMetrics().horizontalAdvance(button->text()) > button->width() - 12) {
					fits = false;
					notes += QStringLiteral(" %1: the button \"%2\"").arg(code, button->text());
				}
			if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) /* for a look */
				other.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_map_streams_%1.png").arg(code));
		}
		language::apply(*qApp, QStringLiteral("en"));
		check(fits, qPrintable(QStringLiteral("map settings, Streams: its labels, buttons and column headers whole in English and Arabic at "
				"the dialog's least size%1").arg(notes)));
		/* a stream added: free window, no channel yet (said); a channel added; a window over a register (said) */
		dialog.addStream();
		const QString empty = dialog.streamChecks();
		dialog.addChannel();
		const QString withChannel = dialog.streamChecks();
		addr->setText(QStringLiteral("0xD000"));
		emit addr->textEdited(addr->text());
		const QString over = dialog.streamChecks();
		addr->setText(QStringLiteral("0xDB00"));
		emit addr->textEdited(addr->text());
		const QString fine = dialog.streamChecks();
		std::printf("  added: \"%s\" | with a channel: \"%s\" | over UPTIME: \"%s\" | moved: \"%s\"\n", qPrintable(empty),
				qPrintable(withChannel), qPrintable(over), qPrintable(fine));
		check(list->count() == 2 && empty.contains(QLatin1String("S2: it has no channel")) && !withChannel.contains(QLatin1String("no channel"))
						&& over.contains(QLatin1String("shares bytes with the register UPTIME")) && fine.isEmpty(),
				"map settings, Streams: a stream added (a free window, \"no channel\" said), a channel added, a window over a "
				"register said at once");
		const int steps = doc.undoStack()->count();
		dialog.accept();
		const bool applied = doc.map().streams.size() == 2 && doc.map().streams[1].addr == 0xDB00
				&& doc.map().streams[1].channels.size() == 1 && doc.undoStack()->count() == steps + 1;
		doc.undoStack()->undo();
		check(applied && doc.map().streams.size() == 1, "map settings, Streams: OK puts the streams into the map as one undo "
				"step; undone, the map has its one stream again");

		/* a math line naming a fast channel: a fast math line (F-M), computed for every record of its stream; two
		 * streams refused with the reason; the completion offers the channels */
		RegDef volts;
		volts.addr = 0xD004;
		volts.name = QStringLiteral("SUPPLY_V");
		volts.type = RegType::F32;
		volts.size = 4;
		MathLine start;
		start.name = QStringLiteral("P");
		StreamDef adc;
		adc.name = QStringLiteral("ADC");
		StreamChannel iLoad, vBus, pIn;
		iLoad.name = QStringLiteral("I_LOAD");
		iLoad.unit = QStringLiteral("A");
		vBus.name = QStringLiteral("V_BUS");
		adc.channels = { iLoad, vBus };
		StreamDef pwr;
		pwr.name = QStringLiteral("PWR");
		pIn.name = QStringLiteral("P_IN");
		pwr.channels = { pIn };
		MathLineDialog math(start, false, { volts });
		math.setFastStreams({ adc, pwr });
		math.show();
		auto *formula = math.findChild<QLineEdit *>(QStringLiteral("formula"));
		auto *state = math.findChild<QLabel *>(QStringLiteral("mathState"));
		auto *completer = math.findChild<FormulaCompleter *>();
		auto *okButton = math.findChild<QDialogButtonBox *>() ? math.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok) : nullptr;
		QString told, twoStreams, accepted;
		QStringList offered;
		bool fastOk = false, refused = false;
		if (formula && state && okButton && completer) {
			formula->setText(QStringLiteral("SUPPLY_V * adc.i_load"));
			told = QTextDocumentFragment::fromHtml(state->text()).toPlainText();
			fastOk = okButton->isEnabled();
			formula->setText(QStringLiteral("ADC.I_LOAD * PWR.P_IN"));
			twoStreams = QTextDocumentFragment::fromHtml(state->text()).toPlainText();
			refused = !okButton->isEnabled();
			formula->setText(QStringLiteral("SUPPLY_V * 2"));
			accepted = state->text();
			formula->clear();
			QTest::keyClicks(formula, QStringLiteral("I_LO"));
			offered = completer->shown();
			formula->clear();
		}
		math.hide();
		std::printf("  a math line over ADC.I_LOAD: \"%s\"; over two streams: \"%s\"; I_LO offers %s\n", qPrintable(told),
				qPrintable(twoStreams), qPrintable(offered.join(QStringLiteral(", "))));
		check(fastOk && told.contains(QStringLiteral("OK: reads SUPPLY_V, adc.i_load · computed for every record of stream ADC"))
						&& refused && twoStreams.contains(QLatin1String("two streams, two clocks: not in this version"))
						&& accepted.contains(QLatin1String("OK: reads SUPPLY_V")) && offered.contains(QLatin1String("ADC.I_LOAD")),
				"math lines: a formula over a stream's channels is taken, the editor says it is computed for every record of "
				"that stream; channels of two streams are refused with the reason; the completion offers the channels");
	}

	/* F-M: a fast math line, a formula over the channels of one stream, computed for every record of it as its blocks
	 * come (ChartTab::appendFast), at the record's own time, into a store of its own; a register in it held at its last
	 * polled value; channels of two streams refused; a record whose result is no number left out; drawn from its own
	 * summaries; its RAM in the budget; the one cap of 64 lines. Its own settings group: the live chart's math lines stay */
	void chartFastMath() {
		const QString group = QStringLiteral("fastMathTest");
		StreamDef def;
		def.name = QStringLiteral("ADC");
		def.addr = 0xDC00;
		def.size = 1024;
		def.rate = 10000;
		StreamChannel current;
		current.name = QStringLiteral("I_LOAD");
		current.unit = QStringLiteral("A");
		current.scale = 0.0005;
		StreamChannel voltage;
		voltage.name = QStringLiteral("V_BUS");
		voltage.unit = QStringLiteral("V");
		voltage.scale = 0.001;
		def.channels = { current, voltage };
		StreamDef other;
		other.name = QStringLiteral("PWR");
		other.addr = 0xDB00;
		other.size = 256;
		other.rate = 1000;
		StreamChannel power;
		power.name = QStringLiteral("P_IN");
		power.unit = QStringLiteral("W");
		other.channels = { power };
		RegDef volts;
		volts.addr = 0xD004;
		volts.name = QStringLiteral("SUPPLY_V");
		volts.unit = QStringLiteral("V");
		volts.type = RegType::F32;
		volts.size = 4;
		const RegKey voltsKey = regKey(volts);
		/* the parser: one stream's channels make a fast line, a register among them is read as held; two streams no */
		{
			MathLine product, held, plain, two;
			product.formula = QStringLiteral("ADC.I_LOAD * ADC.V_BUS");
			held.formula = QStringLiteral("adc.i_load * SUPPLY_V");
			plain.formula = QStringLiteral("SUPPLY_V * 2");
			two.formula = QStringLiteral("ADC.I_LOAD * PWR.P_IN");
			const bool p = product.compile({ volts }, { def, other }) && product.fast() && product.stream == 0
					&& product.channels == QVector<int>({ 0, 1 });
			const bool h = held.compile({ volts }, { def, other }) && held.fast() && held.channels == QVector<int>({ 0, -1 })
					&& held.inputs.value(1) == voltsKey;
			const bool r = plain.compile({ volts }, { def, other }) && !plain.fast();
			const bool refused = !two.compile({ volts }, { def, other }) && !two.fast()
					&& two.error.contains(QLatin1String("two streams, two clocks: not in this version"))
					&& two.error.contains(QLatin1String("PWR.P_IN"));
			std::printf("     (two streams: \"%s\")\n", qPrintable(two.error));
			check(p && h && r && refused, "math lines, fast: a formula over one stream's channels is a fast math line (a "
					"register in it read as held), one over registers only is not; channels of two streams are refused: "
					"\"two streams, two clocks: not in this version\"");
		}
		/* record k at 100 s + k / 10 kHz: I_LOAD 1 A of 50 Hz around 0.2 A (below 0 for a part of each period), V_BUS 12 V */
		const auto rawI = [](qint64 k) { return qint16(std::lround(2000 * std::sin(2 * M_PI * 50 * k / 10000.0)) + 400); };
		const auto rawV = [](qint64 k) { return qint16(12000 + std::lround(3 * std::sin(2 * M_PI * 50 * k / 10000.0 + 1))); };
		const auto timeOf = [](qint64 k) { return 100.0 + k / 10000.0; };
		const auto blockOf = [&](qint64 first, qint64 n) {
			QByteArray records(int(n * 4), '\0');
			for (qint64 k = 0; k < n; k++) {
				const qint16 a = rawI(first + k), b = rawV(first + k);
				records[int(4 * k)] = char(a);
				records[int(4 * k + 1)] = char(a >> 8);
				records[int(4 * k + 2)] = char(b);
				records[int(4 * k + 3)] = char(b >> 8);
			}
			return records;
		};
		const auto feedTab = [&](ChartTab &tab, qint64 first, qint64 n, bool start) {
			tab.appendFast(0, quint64(first), int(n), blockOf(first, n), start, 0, true, quint64(first + n), timeOf(first + n),
					1e-4);
		};
		const auto mathLine = [](const char *name, const char *unit, const char *formula) {
			MathLine line;
			line.name = QString::fromUtf8(name);
			line.unit = QString::fromUtf8(unit);
			line.formula = QString::fromUtf8(formula);
			return line;
		};
		/* the product record by record, a register held, a record that is no number left out, drawn */
		{
			QSettings().remove(group); /* no math lines of the chart before */
			double now = 100.4;
			ChartTab tab{ [&now] { return now; }, nullptr, group };
			tab.resize(1200, 700);
			tab.setRegisters({ volts });
			tab.setFastStreams({ def });
			tab.plotFastChannel(0, 0, true);
			tab.plotFastChannel(0, 1, true);
			const bool added = tab.addMathLine(mathLine("P", "W", "ADC.I_LOAD * ADC.V_BUS"))
					&& tab.addMathLine(mathLine("Q", "W", "ADC.I_LOAD * SUPPLY_V"))
					&& tab.addMathLine(mathLine("R", "", "sqrt(ADC.I_LOAD)"));
			feedTab(tab, 0, 1000, true); /* SUPPLY_V not polled yet: Q has nothing to hold */
			tab.frame({ { voltsKey, { QPointF(100.05, 12.5) } } });
			feedTab(tab, 1000, 1000, false);
			tab.frame({ { voltsKey, { QPointF(100.15, 13.0) } } });
			feedTab(tab, 2000, 1000, false);
			ChartView *view = tab.view();
			const fast::Store *stream = view->fastStore(0);
			const fast::Store *ps = view->fastStore(MathLines::fastStream(0)), *qs = view->fastStore(MathLines::fastStream(1)),
							  *rs = view->fastStore(MathLines::fastStream(2));
			bool product = added && stream && ps && stream->size() == 3000 && ps->size() == 3000;
			int compared = 0;
			for (qsizetype i = 0; product && i < 3000; i += 7, compared++)
				product = ps->timeAt(i) == stream->timeAt(i)
						&& ps->value(0, i) == double(float(stream->value(0, i) * stream->value(1, i)));
			std::printf("     (P = ADC.I_LOAD * ADC.V_BUS: %lld records of %lld, %d compared; Q: %lld; R: %lld, %lld gaps)\n",
					(long long) (ps ? ps->size() : -1), (long long) (stream ? stream->size() : -1), compared,
					(long long) (qs ? qs->size() : -1), (long long) (rs ? rs->size() : -1), (long long) (rs ? rs->gaps() : -1));
			check(product && compared > 400 && tab.mathLines().lines().value(0).fast() && tab.mathLinesShown() == 3
							&& tab.fastLines() == 2,
					"math lines, fast: ADC.I_LOAD * ADC.V_BUS is computed for every record of the stream, at the record's own "
					"time, equal to the product of its two channels record by record");
			bool heldRight = qs && qs->size() == 2000;
			for (qsizetype i = 0; heldRight && i < 2000; i++)
				heldRight = std::fabs(qs->timeAt(i) - stream->timeAt(1000 + i)) < 1e-9
						&& qs->value(0, i) == double(float(stream->value(0, 1000 + i) * (i < 1000 ? 12.5 : 13.0)));
			check(heldRight, "math lines, fast: a register in a fast math line (ADC.I_LOAD * SUPPLY_V) is held at its last "
					"polled value for each record; before its first poll there is nothing to hold and no record is made");
			qsizetype positive = 0;
			for (qint64 k = 0; k < 3000; k++) positive += rawI(k) >= 0;
			bool roots = rs && rs->size() == positive && rs->gaps() > 0;
			for (qsizetype i = 0; roots && i < rs->size(); i++) {
				const qsizetype at = stream->lowerBound(rs->timeAt(i) - 5e-5);
				roots = at < stream->size() && std::fabs(stream->timeAt(at) - rs->timeAt(i)) < 1e-9
						&& rs->value(0, i) == double(float(std::sqrt(stream->value(0, at))));
			}
			check(roots, "math lines, fast: a record whose result is no number (sqrt of a negative current) is left out, the "
					"line broken there as over lost records; the others at their own times");
			view->showSpan(timeOf(0), timeOf(2999));
			tab.grab();
			double lo = 1e300, hi = -1e300, binLo = 1e300, binHi = -1e300;
			for (qsizetype i = 0; ps && i < ps->size(); i++) {
				lo = std::min(lo, ps->value(0, i));
				hi = std::max(hi, ps->value(0, i));
			}
			const QVector<ChartView::BinInfo> bins = view->lastBins(ChartTab::fastMathKey(0));
			for (const ChartView::BinInfo &bin : bins) {
				binLo = std::min(binLo, bin.min);
				binHi = std::max(binHi, bin.max);
			}
			QString name;
			for (const ChartView::Info &line : view->lines())
				if (line.key == ChartTab::fastMathKey(0)) name = line.name;
			std::printf("     (drawn: %lld bins, %.6f .. %.6f W; the records %.6f .. %.6f W; \"%s\")\n", (long long) bins.size(),
					binLo, binHi, lo, hi, qPrintable(name));
			check(bins.size() > 100 && binLo == lo && binHi == hi && name == QStringLiteral("ƒ P"),
					"math lines, fast: the line ƒ P is drawn as a fast line, binned from its own records' summaries (its "
					"columns' min and max are its records')");
			if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* for a look: a fast math line beside its channels */
				const bool wasDark = Theme::isDark();
				tab.show();
				view->showLastValues(); /* the legend's values now, not at their pace */
				for (const bool dark : { true, false }) {
					Theme::apply(*qApp, dark);
					tab.themeChanged();
					view->showSpan(timeOf(0), timeOf(2999));
					QTest::qWait(200);
					tab.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + (dark ? QStringLiteral("_fast_math_dark.png")
																					: QStringLiteral("_fast_math_light.png")));
				}
				Theme::apply(*qApp, wasDark);
				tab.hide();
			}
			/* measured: Measure's statistics between the cursors against a plain loop over its records */
			const double a = timeOf(500) + 3e-5, b = timeOf(2500) + 6e-5;
			view->setCursors(a, b);
			const ChartView::Stats st = view->stats(ChartTab::fastMathKey(0));
			double mLo = 1e300, mHi = -1e300, area = 0, span = 0;
			qint64 n = 0;
			for (qsizetype i = 0; ps && i < ps->size(); i++) {
				const double t = ps->timeAt(i), v = ps->value(0, i);
				if (t < a || t > b) continue;
				mLo = std::min(mLo, v);
				mHi = std::max(mHi, v);
				n++;
				if (i == 0 || ps->timeAt(i - 1) < a) continue;
				area += 0.5 * (v + ps->value(0, i - 1)) * (t - ps->timeAt(i - 1));
				span += t - ps->timeAt(i - 1);
			}
			std::printf("     (measured: n %lld/%lld, min %.6f/%.6f, max %.6f/%.6f, mean %.9f/%.9f W)\n", (long long) st.n,
					(long long) n, st.min, mLo, st.max, mHi, st.mean, area / span);
			check(st.ok && st.n == n && st.min == mLo && st.max == mHi
							&& std::fabs(st.mean - area / span) <= 1e-6 * std::max(1.0, std::fabs(area / span)),
					"math lines, fast: Measure's min, max and mean of a fast math line between the cursors equal a plain loop "
					"over its records");
			/* exported: a row per record, its values beside its channels' */
			const QVector<recording::Line> exported = view->samples(timeOf(1000), timeOf(1999));
			const recording::Line *pLine = nullptr;
			for (const recording::Line &line : exported)
				if (line.name == QStringLiteral("ƒ P")) pLine = &line;
			bool rowsRight = pLine && pLine->times.size() == 1000;
			for (int k = 0; rowsRight && k < 1000; k++)
				rowsRight = std::fabs(pLine->times[k] - timeOf(1000 + k)) < 1e-9 && pLine->values[k] == ps->value(0, 1000 + k);
			QTemporaryDir folder;
			std::atomic<bool> cancel{ false };
			qint64 rows = 0;
			QString error;
			const bool written = recording::write(folder.filePath(QStringLiteral("math.csv")), exported, 0, cancel,
					[](double) {}, rows, error);
			std::printf("     (exported: %lld rows for 1000 records, %d lines)\n", (long long) rows, int(exported.size()));
			check(rowsRight && written && rows == 1000, "math lines, fast: Export to CSV takes a fast math line's records, a "
					"row per record, at its channels' times");
		}
		/* triggered: the trigger on a fast math line, its crossing found here as its records are made (the engine watches
		 * nothing then), between the two records around the level */
		{
			QSettings().remove(group); /* no math lines of the chart before */
			ChartTab tab{ [] { return 101.0; }, nullptr, group };
			tab.setFastStreams({ def });
			tab.addMathLine(mathLine("P", "W", "ADC.I_LOAD * ADC.V_BUS"));
			int engineStream = -2;
			QObject::connect(&tab, &ChartTab::fastTriggerChanged, &tab,
					[&](int stream, const fast::TriggerWatch &) { engineStream = stream; });
			const auto step = [&](qint64 first, qint64 n, bool start) { /* 0 A, then 1 A from record 300: 0 W, then 12 W */
				QByteArray records = blockOf(first, n);
				for (qint64 k = 0; k < n; k++) {
					const qint16 a = first + k < 300 ? 0 : 2000;
					records[int(4 * k)] = char(a);
					records[int(4 * k + 1)] = char(a >> 8);
				}
				tab.appendFast(0, quint64(first), int(n), records, start, 0, true, quint64(first + n), timeOf(first + n), 1e-4);
			};
			ChartView *view = tab.view();
			step(0, 100, true);
			view->setTrigger(ChartTab::fastMathKey(0), 6.0, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Single);
			step(100, 100, false);
			const bool quiet = view->triggerArmed();
			step(200, 200, false);
			const fast::Store *ps = view->fastStore(MathLines::fastStream(0));
			double expected = NAN;
			if (ps && ps->size() > 300) {
				const double pv = ps->value(0, 299), v = ps->value(0, 300);
				expected = ps->timeAt(299) + (6.0 - pv) / (v - pv) * (ps->timeAt(300) - ps->timeAt(299));
			}
			const double at = view->triggeredAt();
			std::printf("     (the trigger on ƒ P: armed before %d, fired at %.7f s, expected %.7f; the engine watches stream %d)\n",
					int(quiet), at, expected, engineStream);
			check(quiet && std::fabs(at - expected) < 1e-9 && !view->triggerArmed() && engineStream == -1,
					"math lines, fast: the trigger on a fast math line fires at the record where it crosses the level (between "
					"the two records around it), found as its records are made; the engine watches no stream for it");
		}
		/* a recording's chart: the fast math line computed from its stream's records (the file's), a register held at the
		 * value polled at or before each record (before the first poll: the first) */
		{
			QSettings().remove(group); /* no math lines of the chart before */
			ChartTab tab{ [] { return 101.0; }, nullptr, group };
			tab.setRecording(0, timeOf(0), timeOf(2999), 2048, 1);
			tab.setRegisters({ volts });
			tab.setFastStreams({ def });
			auto source = std::make_shared<fast::Store>(def);
			const QByteArray records = blockOf(0, 3000);
			source->append(0, 3000, records.constData(), true, 0);
			source->mark(3000, timeOf(3000), 1e-4);
			tab.view()->setFastStore(0, source);
			tab.addMathLine(mathLine("Q", "W", "ADC.I_LOAD * SUPPLY_V"));
			tab.frame({ { voltsKey, { QPointF(timeOf(1000), 12.5), QPointF(timeOf(2000) + 5e-5, 13.0) } } });
			tab.fillFastMath();
			const fast::Store *qs = tab.view()->fastStore(MathLines::fastStream(0));
			bool held = qs && qs->size() == 3000;
			for (qsizetype i = 0; held && i < 3000; i++)
				held = qs->timeAt(i) == source->timeAt(i)
						&& qs->value(0, i) == double(float(source->value(0, i) * (i <= 2000 ? 12.5 : 13.0)));
			std::printf("     (a recording's ƒ Q: %lld records of %lld)\n", (long long) (qs ? qs->size() : -1),
					(long long) source->size());
			check(held, "math lines, fast, in a recording's window: computed from its stream's records, a register held at "
					"the value polled at or before each record (before the first poll: the first)");
		}
		/* its RAM: the records in the chart's budget as a fast line's, trimmed to its share (16 lines: a sixteenth); the
		 * stream's own store keeps none while none of its channels is on the chart */
		{
			QSettings().remove(group); /* no math lines of the chart before */
			ChartTab tab{ [] { return 1000.0; }, nullptr, group };
			tab.resize(1200, 700);
			tab.setFastStreams({ def });
			ChartView *view = tab.view();
			view->setMemory(3600);
			view->setRamBudget(256);
			for (int k = 1; k <= 15; k++) view->addSeries(k, QStringLiteral("POLLED%1").arg(k), QStringLiteral("V"), Qt::blue);
			tab.addMathLine(mathLine("P", "W", "ADC.I_LOAD * ADC.V_BUS"));
			for (qint64 first = 0; first < 100 * 65536; first += 65536) feedTab(tab, first, 65536, first == 0);
			const fast::Store *store = view->fastStore(MathLines::fastStream(0));
			const qint64 share = 256ll * 1024 * 1024 / 16;
			std::printf("     (the fast math line's store: %.1f MB of its %lld MB share, %lld records of %d; the stream's %lld; "
					"computed in %.0f ms)\n", store ? store->bytes() / 1048576.0 : -1.0, (long long) (share >> 20),
					(long long) (store ? store->size() : -1), 100 * 65536,
					(long long) (view->fastStore(0) ? view->fastStore(0)->size() : -1), tab.fastMathNs() / 1e6);
			check(store && store->bytes() <= share + 65536 * 4 && store->bytes() >= share / 2 && store->size() < 100 * 65536
							&& view->bytesHeld() >= store->bytes() && view->bytesNeeded() > 0 && view->fastStore(0)
							&& view->fastStore(0)->size() == 0,
					"math lines, fast: its records count in the chart's RAM budget as a fast line's (one line of 16: trimmed to "
					"a sixteenth); the stream's store keeps none while none of its channels is plotted");
		}
		/* the one cap: a fast math line is one of the 64 lines; the 65th of any kind is refused */
		{
			QSettings().remove(group); /* no math lines of the chart before */
			ChartTab tab{ [] { return 1000.0; }, nullptr, group };
			tab.setFastStreams({ def });
			for (int k = 1; k <= 63; k++)
				tab.view()->addSeries(k, QStringLiteral("POLLED%1").arg(k), QStringLiteral("V"), Qt::blue);
			const bool first = tab.addMathLine(mathLine("P", "W", "ADC.I_LOAD * ADC.V_BUS"));
			const int lines = tab.lineCount();
			const bool second = tab.addMathLine(mathLine("Q", "W", "ADC.I_LOAD * 2"));
			std::printf("     (the cap: the first fast math line %d, %d lines; the second %d, %d lines; \"%s\")\n", int(first),
					lines, int(second), tab.lineCount(), qPrintable(tab.infoText()));
			check(first && lines == 64 && !second && tab.lineCount() == 64 && tab.mathLinesShown() == 1 && tab.fastLines() == 0
							&& tab.infoText().contains(QStringLiteral(" · 1 math")),
					"math lines, fast: the cap counts a fast math line (one of 64 lines, a math line in the info line); a 65th "
					"line is refused");
		}
		QSettings().remove(group);
	}

	/* Fast EVRe 5.3: a fast line measured: the statistics between the cursors from the store's summaries equal a plain
	 * loop over its records (nothing across a gap), within a frame's time over 10 million; the totals since Clear; the
	 * trigger on it (never across a gap); its records in the export */
	void chartFastMeasure() {
		StreamDef def;
		def.name = QStringLiteral("ADC");
		def.addr = 0xDC00;
		def.size = 1024;
		def.rate = 10000;
		StreamChannel current;
		current.name = QStringLiteral("I_LOAD");
		current.unit = QStringLiteral("A");
		current.scale = 0.0005;
		StreamChannel voltage;
		voltage.name = QStringLiteral("V_BUS");
		voltage.unit = QStringLiteral("V");
		voltage.scale = 0.001;
		def.channels = { current, voltage };
		const int iLoad = ChartView::fastKey(0, 0), vBus = ChartView::fastKey(0, 1);
		auto makeView = [&](QWidget &host, double now) {
			host.resize(1100, 480);
			auto *view = new ChartView(&host);
			view->setGeometry(9, 5, 1080, 470);
			view->setClock([now] { return now; }, 0);
			view->setSmooth(false);
			view->setFastStream(0, def);
			view->addSeries(iLoad, QStringLiteral("ADC.I_LOAD"), QStringLiteral("A"), QColor(255, 0, 0));
			view->addSeries(vBus, QStringLiteral("ADC.V_BUS"), QStringLiteral("V"), QColor(0, 160, 0));
			return view;
		};
		/* record k at 100 s + k / 10 kHz: I_LOAD 1 A of 50 Hz around 0.2 A, V_BUS 12 V with 3 mV of ripple */
		const auto rawI = [](qint64 k) { return qint16(std::lround(2000 * std::sin(2 * M_PI * 50 * k / 10000.0)) + 400); };
		const auto rawV = [](qint64 k) { return qint16(12000 + std::lround(3 * std::sin(2 * M_PI * 50 * k / 10000.0 + 1))); };
		const auto timeOf = [](qint64 k) { return 100.0 + k / 10000.0; };
		/* a block of n records from `first`, I_LOAD given by `current` (else the wave), and its time mark; with a scanner,
		 * the engine's part too: the trigger's crossings in it, handed to the view with the block */
		fast::TriggerScan *scanner = nullptr;
		const auto feed = [&](ChartView *view, qint64 first, qint64 n, bool start, quint64 lost,
								  const std::function<qint16(qint64)> &current = {}) {
			QByteArray records(int(n * 4), '\0');
			for (qint64 k = 0; k < n; k++) {
				const qint16 a = current ? current(first + k) : rawI(first + k), b = rawV(first + k);
				records[int(4 * k)] = char(a);
				records[int(4 * k + 1)] = char(a >> 8);
				records[int(4 * k + 2)] = char(b);
				records[int(4 * k + 3)] = char(b >> 8);
			}
			const qint64 at = view->appendFast(0, quint64(first), n, records, start, lost);
			view->markFast(0, quint64(first + n), timeOf(first + n), 1e-4);
			if (!scanner) return;
			int stream = -1;
			const fast::TriggerWatch watch = view->fastTriggerWatch(stream);
			if (watch.serial != scanner->watch().serial) scanner->set(watch); /* as the window posts it to the engine */
			fast::BlockTaken taken;
			taken.first = quint64(first);
			taken.count = int(n);
			taken.lost = lost;
			taken.newStart = start;
			QVector<fast::Crossing> crossings;
			scanner->scan(def, taken, records.constData(), { quint64(first + n), timeOf(first + n) }, 1e-4, crossings);
			view->fastCrossings(0, at, crossings);
		};
		/* the plain loop over the records kept: the trapezoids of each part without a gap */
		struct Plain {
			double min = 1e300, max = -1e300, area = 0, squares = 0, span = 0, shifted = 0, shiftedSquares = 0;
			qint64 n = 0;
		};
		const auto plain = [&](const QVector<qint64> &numbers, int channel, double t0, double t1) {
			Plain out;
			const auto value = [&](qint64 k) { return channel == 0 ? rawI(k) * 0.0005 : rawV(k) * 0.001; };
			double shift = NAN;
			for (qsizetype i = 0; i < numbers.size(); i++) {
				const qint64 k = numbers[i];
				const double t = timeOf(k);
				if (t < t0 || t > t1) continue;
				const double v = value(k);
				if (std::isnan(shift)) shift = v;
				out.min = std::min(out.min, v);
				out.max = std::max(out.max, v);
				out.n++;
				if (i == 0 || numbers[i - 1] != k - 1 || timeOf(k - 1) < t0) continue; /* a gap, or the first */
				const double p = value(k - 1), dt = t - timeOf(k - 1);
				out.area += 0.5 * (v + p) * dt;
				out.squares += 0.5 * (v * v + p * p) * dt;
				out.shifted += 0.5 * ((v - shift) + (p - shift)) * dt;
				out.shiftedSquares += 0.5 * ((v - shift) * (v - shift) + (p - shift) * (p - shift)) * dt;
				out.span += dt;
			}
			return out;
		};
		const auto near = [](double a, double b, double relative) {
			return std::fabs(a - b) <= relative * std::max(1.0, std::fabs(b));
		};
		/* the statistics between the cursors: 2 s with 0.1 s lost in the middle */
		{
			QWidget host;
			ChartView *view = makeView(host, 103.0);
			QVector<qint64> kept;
			for (qint64 first = 0; first < 20000; first += 1000) {
				if (first == 10000) continue; /* lost: the next block says so */
				feed(view, first, 1000, first == 0, first == 11000 ? 1000 : 0);
				for (qint64 k = first; k < first + 1000; k++) kept << k;
			}
			const double a = timeOf(2500) + 3e-5, b = timeOf(17800) + 6e-5;
			view->setCursors(a, b);
			bool right = true;
			QStringList report;
			for (int channel = 0; channel < 2; channel++) {
				const ChartView::Stats s = view->stats(channel == 0 ? iLoad : vBus);
				const Plain p = plain(kept, channel, a, b);
				const double mean = p.area / p.span, rms = std::sqrt(p.squares / p.span);
				const double shiftedMean = p.shifted / p.span;
				const double sd = std::sqrt(std::max(0.0, p.shiftedSquares / p.span - shiftedMean * shiftedMean));
				const auto at = [&](double t) {
					const qint64 k = qint64(std::floor((t - 100.0) * 10000.0));
					const double f = (t - timeOf(k)) * 10000.0;
					const double v0 = channel == 0 ? rawI(k) * 0.0005 : rawV(k) * 0.001;
					const double v1 = channel == 0 ? rawI(k + 1) * 0.0005 : rawV(k + 1) * 0.001;
					return v0 + (v1 - v0) * f;
				};
				right = right && s.ok && s.n == p.n && s.min == p.min && s.max == p.max && near(s.p2p, p.max - p.min, 1e-12)
						&& near(s.integral, p.area, 1e-6) && near(s.mean, mean, 1e-6) && near(s.rms, rms, 1e-6)
						&& std::fabs(s.std - sd) <= 1e-3 * sd && near(s.atA, at(a), 1e-6) && near(s.atB, at(b), 1e-6);
				report << QStringLiteral("%1: n %2/%3 mean %4/%5 rms %6/%7 std %8/%9 area %10/%11 A %12/%13")
								  .arg(channel).arg(s.n).arg(p.n).arg(s.mean, 0, 'g', 10).arg(mean, 0, 'g', 10)
								  .arg(s.rms, 0, 'g', 10).arg(rms, 0, 'g', 10).arg(s.std, 0, 'g', 6).arg(sd, 0, 'g', 6)
								  .arg(s.integral, 0, 'g', 10).arg(p.area, 0, 'g', 10).arg(s.atA, 0, 'g', 8).arg(at(a), 0, 'g', 8);
			}
			std::printf("     (%s)\n", qPrintable(report.join(QStringLiteral("; "))));
			check(right, "chart, fast lines measured: between the cursors min, max, mean, RMS, std (12 V with 3 mV of ripple), "
					"peak to peak, area and the values at A and B equal a plain loop over the records, nothing across the gap");
			view->setCursors(timeOf(10500), timeOf(15000));
			check(std::isnan(view->stats(iLoad).atA) && std::isfinite(view->stats(iLoad).atB),
					"chart, fast lines measured: a cursor in the gap reads nothing there (no value is made up)");
		}
		/* 1000 s at 10 kHz: the statistics over all of it within a frame's time, from the summaries */
		{
			QWidget host;
			ChartView *view = makeView(host, 1101.0);
			view->setMemory(1100);
			for (qint64 first = 0; first < 10000000; first += 500000) feed(view, first, 500000, first == 0, 0);
			view->setCursors(timeOf(1), timeOf(9999999));
			QElapsedTimer clock;
			clock.start();
			const QVector<ChartView::Stats> all = view->stats({ iLoad, vBus }, false);
			const double ms = clock.nsecsElapsed() / 1e6;
			std::printf("     (10 million records each, two lines: measured in %.3f ms, mean %.6f A)\n", ms,
					all.value(0).mean);
			check(all.size() == 2 && all[0].ok && all[0].n == 9999999 && near(all[0].mean, 0.2, 1e-3) && ms < 16,
					"chart, fast lines measured: 10 million records each, two lines, measured within a frame's time (16 ms)");
		}
		/* the totals since Clear: every record summed as it comes, a memory of 1 s trims them away, Clear starts over */
		{
			QWidget host;
			ChartView *view = makeView(host, 111.0);
			view->setMemory(1);
			QVector<qint64> all;
			for (qint64 first = 0; first < 100000; first += 1000) {
				if (first == 50000) continue;
				feed(view, first, 1000, first == 0, first == 51000 ? 1000 : 0);
				for (qint64 k = first; k < first + 1000; k++) all << k;
			}
			const Plain p = plain(all, 0, 0, 1e9);
			const double total = view->total(iLoad);
			std::printf("     (totals: %.9f A·s summed, %.9f by a plain loop; %lld records kept of %lld)\n", total, p.area,
					(long long) view->pointsKept(iLoad), (long long) all.size());
			const bool summed = near(total, p.area, 1e-6) && view->pointsKept(iLoad) < all.size() / 2
					&& std::fabs(view->totalsSince() - timeOf(0)) < 1e-9;
			view->clearData();
			feed(view, 100000, 1000, false, 0);
			const double after = view->total(iLoad);
			QVector<qint64> again;
			for (qint64 k = 100000; k < 101000; k++) again << k;
			check(summed && near(after, plain(again, 0, 0, 1e9).area, 1e-6) && std::isfinite(after),
					"chart, fast lines measured: the total since Clear sums every record as it comes (the memory's trims lose "
					"nothing, the gap not bridged); Clear starts it again");
		}
		/* the trigger on a fast line, found as the engine finds it (each block as it comes): Rising through 0.5 A, after
		 * 0 A, 50 records lost, then 1 A: the gap is no crossing; the step at record 300 is, its time between records 299
		 * and 300 */
		{
			QWidget host;
			ChartView *view = makeView(host, 101.0);
			fast::TriggerScan scan;
			scanner = &scan;
			const auto step = [](qint64 k) { return qint16(k < 100 ? 0 : k < 250 ? 2000 : k < 300 ? 0 : 2000); };
			feed(view, 0, 100, true, 0, step);
			view->setTrigger(iLoad, 0.5, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Single);
			feed(view, 150, 100, false, 50, step);   /* after the gap: 1 A, no crossing across it */
			const bool quietAcrossGap = view->triggerArmed();
			feed(view, 250, 100, false, 0, step);    /* 0 A from 250, 1 A from 300 */
			const double at = view->triggeredAt();
			const double expected = timeOf(299) + 0.5 * 1e-4; /* 0.5 A of 0 -> 1 A: half way */
			std::printf("     (the trigger: armed across the gap %d, fired at %.7f s, expected %.7f; the record 250 is a fall)\n",
					int(quietAcrossGap), at, expected);
			check(quietAcrossGap && std::fabs(at - expected) < 1e-9 && !view->triggerArmed(),
					"chart, fast lines: the trigger on a fast line fires between the two records around the level, never "
					"across a gap");
			scanner = nullptr;
		}
		/* the export: each record a row, both channels of the stream in it */
		{
			QWidget host;
			ChartView *view = makeView(host, 101.0);
			feed(view, 0, 5000, true, 0);
			const QVector<recording::Line> lines = view->samples(timeOf(1000), timeOf(1999));
			bool same = lines.size() == 2 && lines[0].times.size() == 1000 && lines[1].times == lines[0].times;
			for (int k = 0; same && k < 1000; k++)
				same = std::fabs(lines[0].times[k] - timeOf(1000 + k)) < 1e-9 && lines[0].values[k] == rawI(1000 + k) * 0.0005
						&& lines[1].values[k] == rawV(1000 + k) * 0.001;
			QTemporaryDir folder;
			const QString file = folder.filePath(QStringLiteral("fast.csv"));
			std::atomic<bool> cancel{ false };
			qint64 rows = 0;
			QString error;
			const bool written = recording::write(file, lines, 0, cancel, [](double) {}, rows, error);
			std::printf("     (the export: %lld rows for 1000 records of two channels)\n", (long long) rows);
			check(same && written && rows == 1000, "chart, fast lines: Export to CSV takes the records, each a row with both "
					"channels of the stream");
		}
		/* the spectrum of a fast line with a gap: its longest part without one (even steps), the window's title says so;
		 * the histogram takes every record */
		{
			double now = 103;
			ChartTab tab{ [&now] { return now; } };
			tab.resize(1200, 700);
			tab.setFastStreams({ def });
			tab.plotFastChannel(0, 0, true);
			ChartView *view = tab.view();
			const auto feedTab = [&](qint64 first, qint64 n, bool start, quint64 lost) {
				QByteArray records(int(n * 4), '\0');
				for (qint64 k = 0; k < n; k++) {
					const qint16 a = rawI(first + k), b = rawV(first + k);
					records[int(4 * k)] = char(a);
					records[int(4 * k + 1)] = char(a >> 8);
					records[int(4 * k + 2)] = char(b);
					records[int(4 * k + 3)] = char(b >> 8);
				}
				tab.appendFast(0, quint64(first), int(n), records, start, lost, true, quint64(first + n), timeOf(first + n), 1e-4);
			};
			feedTab(0, 5000, true, 0);         /* 0.5 s */
			feedTab(6000, 14000, false, 1000); /* 0.1 s lost, then 1.4 s */
			view->setCursors(timeOf(100), timeOf(19900));
			QString spectrumTitle, histogramTitle;
			qint64 histogramTotal = 0;
			int segment = 0;
			if (AnalysisWindow *spectrum = tab.openAnalysis(AnalysisWindow::Kind::Spectrum, ChartView::fastKey(0, 0))) {
				spectrumTitle = spectrum->windowTitle();
				segment = spectrum->spectrum().segment;
				spectrum->close();
			}
			if (AnalysisWindow *histogram = tab.openAnalysis(AnalysisWindow::Kind::Histogram, ChartView::fastKey(0, 0))) {
				histogramTitle = histogram->windowTitle();
				histogramTotal = histogram->histogram().total;
				histogram->close();
			}
			std::printf("     (\"%s\", segments of %d; \"%s\", %lld samples)\n", qPrintable(spectrumTitle), segment,
					qPrintable(histogramTitle), (long long) histogramTotal);
			check(spectrumTitle.contains(QStringLiteral("1.98 s: 1.39 s of it without a gap")) && segment > 0
							&& !histogramTitle.contains(QLatin1String("gap")) && histogramTotal == 4900 + 13901,
					"chart, fast lines: a spectrum over a gap takes the longest part without one, its window's title says "
					"which; the histogram takes every record");
		}
	}
	/* Fast EVRe, part 5.2: fast lines on the chart from a stream's store, fed here as the window feeds them (records and
	 * time marks), on charts of their own: a spike of one record in a long run at every zoom, single records at their
	 * own times, a gap not bridged, the time labels below a millisecond, the RAM shared with the polled lines, the
	 * lanes, the legend and the crosshair, and the card's picture against the CPU's */
	void chartFastLines() {
		StreamDef def;
		def.name = QStringLiteral("ADC");
		def.addr = 0xDC00;
		def.size = 1024;
		def.rate = 10000;
		StreamChannel current;
		current.name = QStringLiteral("I_LOAD");
		current.unit = QStringLiteral("A");
		current.scale = 0.0005;
		StreamChannel voltage;
		voltage.name = QStringLiteral("V_BUS");
		voltage.unit = QStringLiteral("V");
		voltage.scale = 0.001;
		def.channels = { current, voltage };
		const int iLoad = ChartView::fastKey(0, 0), vBus = ChartView::fastKey(0, 1);
		auto makeView = [&](QWidget &host, double now) {
			host.resize(1100, 480);
			auto *view = new ChartView(&host);
			view->setGeometry(9, 5, 1080, 470);
			view->setClock([now] { return now; }, 0);
			view->setSmooth(false);
			view->setDrawThreads(1);
			view->setFastStream(0, def);
			view->addSeries(iLoad, QStringLiteral("ADC.I_LOAD"), QStringLiteral("A"), QColor(255, 0, 0));
			return view;
		};
		/* n records from `first` at 10 kHz from t0 (record k at t0 + k / 10 kHz): I_LOAD a small wave (raw +-100),
		 * V_BUS 12 V; spike: the record whose I_LOAD is 15 A */
		auto feed = [&](ChartView *view, quint64 first, qsizetype n, double t0, qint64 spike = -1, quint64 lost = 0,
							 bool start = true) {
			QByteArray records(int(n * 4), '\0');
			for (qsizetype k = 0; k < n; k++) {
				const qint64 number = qint64(first) + k;
				const qint16 a = number == spike ? qint16(30000) : qint16(std::lround(100 * std::sin(number * 0.01)));
				const qint16 b = 12000;
				records[int(4 * k)] = char(a);
				records[int(4 * k + 1)] = char(a >> 8);
				records[int(4 * k + 2)] = char(b);
				records[int(4 * k + 3)] = char(b >> 8);
			}
			view->appendFast(0, first, n, records, start, lost);
			view->markFast(0, first + quint64(n), t0 + double(first + quint64(n)) / 10000.0, 1e-4);
		};
		/* the spike: one record of 15 A in 1000 s of 10 kHz (10 million records), held at windows from all of it to 1 ms */
		{
			QWidget host;
			ChartView *view = makeView(host, 1100.0);
			view->setMemory(1000);
			const qint64 spike = 7777777;
			for (qint64 first = 0; first < 10000000; first += 1000000)
				feed(view, quint64(first), 1000000, 100.0, spike, 0, first == 0);
			const double at = 100.0 + spike / 10000.0;
			QStringList missed;
			for (double window : { 1000.0, 100.0, 10.0, 1.0, 0.01, 0.001 }) {
				view->setWindow(window);
				view->showSpan(at - window / 2, at + window / 2);
				host.grab();
				bool shown = false;
				for (const ChartView::BinInfo &bin : view->lastBins(iLoad))
					if (bin.max == 15.0 && bin.t0 <= at + 1e-9 && bin.t1 >= at - 1e-9) shown = true;
				if (!shown || view->yHi() < 15.0) missed << QString::number(window);
			}
			const fast::Store *store = view->fastStore(0);
			if (!missed.isEmpty()) std::printf("  the spike not shown at %s s\n", qPrintable(missed.join(QStringLiteral(", "))));
			check(missed.isEmpty() && store && store->size() == 10000000 && view->pointsKept(iLoad) == 10000000,
					"chart, fast lines: a spike of one record in 10 million (1000 s at 10 kHz) shows at every zoom, from all "
					"of it to 1 ms, in its column's max and the Y range");
			/* the columns kept from frame to frame: a 10 s view moved by 2.5 columns bins the columns at its ends
			 * only, and its bins are the ones a binning from nothing gives */
			{
				view->setWindow(10.0);
				view->showSpan(at - 5.0, at + 5.0);
				host.grab(); /* the first frame of this view: binned whole */
				const qint64 before = view->fastColumnsBinned();
				const double column = 10.0 / view->lastPlot().width();
				view->showSpan(at - 5.0 + 2.5 * column, at + 5.0 + 2.5 * column);
				host.grab();
				const qint64 binned = view->fastColumnsBinned() - before;
				const QVector<ChartView::BinInfo> kept = view->lastBins(iLoad), fresh = view->freshBins(iLoad);
				bool same = kept.size() == fresh.size() && !kept.isEmpty();
				for (int k = 0; same && k < kept.size(); k++)
					same = kept[k].t0 == fresh[k].t0 && kept[k].t1 == fresh[k].t1 && kept[k].min == fresh[k].min
							&& kept[k].max == fresh[k].max && kept[k].count == fresh[k].count && kept[k].gap == fresh[k].gap;
				if (binned > 12 || !same) {
					std::printf("  (the moved view binned %lld columns of two lines; %d bins kept, %d fresh, the same: %d)\n",
							(long long) binned, int(kept.size()), int(fresh.size()), same);
					for (int k = 0; k < kept.size() && k < fresh.size(); k++)
						if (kept[k].t0 != fresh[k].t0 || kept[k].t1 != fresh[k].t1 || kept[k].min != fresh[k].min
								|| kept[k].max != fresh[k].max || kept[k].count != fresh[k].count || kept[k].gap != fresh[k].gap) {
							std::printf("   bin %d: kept t %.9f..%.9f %g..%g n %d gap %d | fresh t %.9f..%.9f %g..%g n %d gap %d\n", k,
									kept[k].t0, kept[k].t1, kept[k].min, kept[k].max, kept[k].count, int(kept[k].gap), fresh[k].t0,
									fresh[k].t1, fresh[k].min, fresh[k].max, fresh[k].count, int(fresh[k].gap));
							break;
						}
				}
				check(binned > 0 && binned <= 12 && same, "chart, fast lines: a view moved by a few columns keeps the columns it "
						"shares with the frame before and bins only its ends (at most 12 columns for two lines of 1000), "
						"and the bins are those of a binning from nothing");
			}
			/* single records at their own times: 1 ms holds 10 */
			view->setWindow(0.001);
			view->showSpan(at - 0.0005, at + 0.0005);
			host.grab();
			const QVector<ChartView::BinInfo> bins = view->lastBins(iLoad);
			bool own = bins.size() >= 10;
			for (const ChartView::BinInfo &bin : bins) {
				const double k = (bin.t0 - 100.0) * 10000.0;
				own = own && bin.count == 1 && std::fabs(k - std::round(k)) < 1e-6;
			}
			check(own, "chart, fast lines: in a view of 1 ms each record is a bin of its own, at its own time");
			/* the time labels below a millisecond with clock times (Display, Time grid; divisions by default there): microseconds */
			view->setTimeGrid(ChartView::TimeGrid::Clock);
			view->setWindow(5e-5);
			view->showSpan(at - 2.5e-5, at + 2.5e-5);
			host.grab();
			const QStringList labels = view->timeLabels();
			const QRegularExpression micro(QStringLiteral("^\\d\\d:\\d\\d:\\d\\d\\.\\d{6}$"));
			bool labelled = labels.size() >= 3 && QSet<QString>(labels.begin(), labels.end()).size() == labels.size();
			for (const QString &label : labels) labelled = labelled && micro.match(label).hasMatch();
			check(labelled && std::fabs(view->window() - 5e-5) < 1e-9, qPrintable(QStringLiteral("chart, fast lines: a view of 50 us (the "
					"shortest is 10 us), its time labels in microseconds: %1").arg(labels.join(QStringLiteral(", ")))));
			/* the labels below a millisecond have room: none cut at the chart's edge, a gap between two */
			view->setWindow(2e-3);
			view->showSpan(at - 1.3e-3, at + 0.7e-3);
			host.grab();
			const QStringList wide = view->timeLabels();
			QFont small = QGuiApplication::font(); /* the chart's labels' */
			small.setPointSizeF(8.5);
			const QFontMetricsF metrics(small);
			const auto seconds = [](const QString &label) { return label.mid(6).toDouble(); }; /* "ss.fffff" */
			const double perSecond = view->lastPlot().width() / view->window();
			bool room = wide.size() >= 3;
			for (qsizetype k = 1; k < wide.size(); k++)
				room = room && (seconds(wide[k]) - seconds(wide[k - 1])) * perSecond
								>= metrics.horizontalAdvance(wide[k]) + 20;
			check(room, qPrintable(QStringLiteral("chart, fast lines: a view of 2 ms: its labels whole, a gap between two: %1")
										   .arg(wide.join(QStringLiteral(", ")))));
		}
		/* a gap: 50 ms of records lost in the middle of 200 ms; nothing drawn across it */
		{
			QWidget host;
			ChartView *view = makeView(host, 10.0);
			feed(view, 0, 1000, 9.8);
			feed(view, 1500, 500, 9.8, -1, 500, false);
			view->setYManual(-1, 1);
			view->setWindow(0.2);
			view->showSpan(9.8, 10.0);
			const QImage picture = host.grab().toImage();
			int gaps = 0;
			for (const ChartView::BinInfo &bin : view->lastBins(iLoad)) gaps += bin.gap;
			const QRectF plot = view->lastPlot();
			const double dpr = picture.devicePixelRatio();
			const auto red = [&](double x0, double x1) {
				int n = 0;
				for (int x = int(std::ceil((9 + x0) * dpr)); x < int((9 + x1) * dpr); x++)
					for (int y = int((5 + plot.top()) * dpr); y < int((5 + plot.bottom()) * dpr); y++) {
						const QColor c = picture.pixelColor(x, y);
						if (c.red() > 180 && c.green() < 90 && c.blue() < 90) n++;
					}
				return n;
			};
			const double xa = plot.left() + plot.width() * 0.5, xb = plot.left() + plot.width() * 0.75;
			const int inGap = red(xa + 3, xb - 3), before = red(plot.left() + 10, xa - 10);
			std::printf("     (the gap: %d bins after one, %d of its pixels red, %d before it)\n", gaps, inGap, before);
			check(gaps == 1 && inGap == 0 && before > 50, "chart, fast lines: 500 records lost: the bin after them says so, "
					"and nothing is drawn across the gap");
			/* the gap's tooltip: how many records are missing there, nothing beside it */
			const double ym = plot.center().y();
			const QString there = view->toolTipAt(QPointF((xa + xb) / 2, ym)), beside = view->toolTipAt(QPointF(xa - 40, ym));
			std::printf("     (the gap's tooltip: \"%s\")\n", qPrintable(there));
			check(there.contains(QStringLiteral("500")) && there.contains(QStringLiteral("lost")) && beside.isEmpty(),
					"chart, fast lines: the gap's tooltip says how many records were lost there");
		}
		/* the RAM shared: 256 MB for a polled line and a fast one of 32 channels (128 bytes a record): each line half */
		{
			StreamDef wide = def;
			wide.channels.clear();
			for (int c = 0; c < 32; c++) {
				StreamChannel channel;
				channel.name = QStringLiteral("C%1").arg(c);
				channel.type = RegType::I32;
				wide.channels << channel;
			}
			QWidget host;
			host.resize(1100, 480);
			auto *view = new ChartView(&host);
			view->setGeometry(9, 5, 1080, 470);
			view->setClock([] { return 1000.0; }, 0);
			view->setMemory(3600);
			view->setRamBudget(256);
			view->setFastStream(0, wide);
			view->addSeries(1, QStringLiteral("POLLED"), QStringLiteral("V"), Qt::blue);
			view->addSeries(ChartView::fastKey(0, 0), QStringLiteral("ADC.C0"), QString(), Qt::red);
			QByteArray records(65536 * 128, '\x01');
			for (int b = 0; b < 32; b++) {
				view->appendFast(0, quint64(b) * 65536, 65536, records, b == 0, 0);
				view->markFast(0, quint64(b + 1) * 65536, 100.0 + (b + 1) * 65536 / 1e5, 1e-5);
			}
			const fast::Store *store = view->fastStore(0);
			const qint64 share = 256ll * 1024 * 1024 / 2;
			std::printf("     (the fast line's store: %lld MB of its %lld MB share, %lld records; a polled line %lld samples)\n",
					(long long) (store->bytes() >> 20), (long long) (share >> 20), (long long) store->size(),
					(long long) view->pointsPerLine());
			check(store->bytes() <= share + 65536 * 128 && store->bytes() >= share / 2 && view->memoryFull()
							&& view->pointsPerLine() == qsizetype(share / ChartView::BYTES_PER_SAMPLE)
							&& view->bytesHeld() >= store->bytes(),
					"chart, fast lines: the RAM shared: a fast line is one of the lines the budget is divided by, its store "
					"trimmed to its share (memory full), the polled line's share the other half");
			/* the budget reached in words that say what it is, in the warn colour (both themes): not "memory full",
			 * which read as data lost while a recording ran on; with a recording, that its file keeps everything */
			const bool wasDark = Theme::isDark();
			bool words = true;
			for (const bool dark : { true, false }) {
				Theme::apply(*qApp, dark);
				for (const bool recording : { false, true }) {
					view->setRecordingOn(recording);
					const QImage shot = host.grab().toImage();
					const QString text = view->memoryStripText();
					const QString tip = view->memoryStripTip();
					const QColor warn = Theme::colors().warn;
					/* the words drawn: pixels near the warn colour on the strip's empty part (the picture's scale applied) */
					const qreal dpr = shot.devicePixelRatio();
					const QRect strip = QRectF(QPointF(view->mapTo(&host, QPoint(0, 0))) + QPointF(80, view->height() - 38),
							QSizeF(400, 30)).toRect();
					int near = 0;
					for (int y = int(strip.top() * dpr); y < int(strip.bottom() * dpr) && y < shot.height(); y++)
						for (int x = int(strip.left() * dpr); x < int(strip.right() * dpr) && x < shot.width(); x++) {
							const QColor c = shot.pixelColor(x, y);
							near += std::abs(c.red() - warn.red()) + std::abs(c.green() - warn.green())
											+ std::abs(c.blue() - warn.blue()) < 60;
						}
					const bool ok = text.startsWith(QStringLiteral("RAM budget reached: keeping the last "))
							&& text.contains(QStringLiteral(" of 60.0 min"))
							&& text.endsWith(QStringLiteral(" · the recording keeps everything")) == recording
							&& view->memoryStripTextColor() == warn && near >= 20
							&& tip.contains(QStringLiteral("RAM budget reached: the chart keeps its samples within the RAM"))
							&& tip.contains(recording ? QStringLiteral("The recording running keeps every sample")
													  : QStringLiteral("A recording keeps every sample"));
					if (!ok || (dark && recording))
						std::printf("     (%s, %s: the strip says \"%s\" in %s, %d warn pixels)\n", dark ? "dark" : "light",
								recording ? "recording" : "not recording", qPrintable(text),
								qPrintable(view->memoryStripTextColor().name()), near);
					words = words && ok;
					if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) /* for a look */
						shot.save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_ram_budget_%1_%2.png")
										  .arg(dark ? QStringLiteral("dark") : QStringLiteral("light"),
												  recording ? QStringLiteral("recording") : QStringLiteral("idle")));
				}
			}
			view->setRecordingOn(false);
			Theme::apply(*qApp, wasDark);
			check(words, "chart, the RAM budget reached: the strip says \"RAM budget reached: keeping the last X of Y\" "
					"(\"· the recording keeps everything\" while a recording runs) in the warn colour, dark and light; its "
					"tooltip says what the budget does and that a recording's file keeps every sample");
		}
		/* lanes, the legend and the crosshair: a polled line in V, I_LOAD and V_BUS */
		{
			QWidget host;
			ChartView *view = makeView(host, 10.0);
			view->addSeries(vBus, QStringLiteral("ADC.V_BUS"), QStringLiteral("V"), QColor(0, 160, 0));
			view->addSeries(5, QStringLiteral("POLLED"), QStringLiteral("V"), Qt::blue);
			feed(view, 0, 2000, 9.8);
			for (int i = 0; i < 20; i++) view->append(5, 9.8 + i * 0.01, 11.0);
			view->setWindow(0.2);
			view->setLanes(true);
			view->frame();
			host.show();
			(void) QTest::qWaitForWindowExposed(&host);
			const QRectF plot = view->lastPlot().isEmpty() ? QRectF(80, 60, 900, 300) : view->lastPlot();
			{ /* the mouse over the plot's middle, then a frame painted by the CPU (a grab), which makes the box: on
			   * Windows the card draws the plot and a repaint of the widget makes no box */
				const QPointF at = plot.center();
				QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
				QApplication::sendEvent(view, &move);
				(void) host.grab();
			}
			int voltLane = -1, ampLane = -1;
			for (int lane = 0; lane < view->laneCount(); lane++) {
				if (view->laneLabel(lane) == QLatin1String("V")) voltLane = lane;
				if (view->laneLabel(lane) == QLatin1String("A")) ampLane = lane;
			}
			const bool laned = view->laneCount() == 2 && voltLane >= 0 && ampLane >= 0
					&& view->laneLines(voltLane).contains(vBus) && view->laneLines(voltLane).contains(5)
					&& view->laneLines(ampLane).contains(iLoad);
			const bool legend = view->legendValue(vBus) == QLatin1String("12.0") && !view->legendValue(iLoad).isEmpty();
			const bool hair = view->readoutSize().isValid() && view->readoutBuilds() > 0;
			std::printf("     (lanes %d, legend \"%s\" \"%s\", the crosshair's box %dx%d)\n", view->laneCount(),
					qPrintable(view->legendValue(vBus)), qPrintable(view->legendValue(iLoad)), int(view->readoutSize().width()),
					int(view->readoutSize().height()));
			check(laned && legend && hair, "chart, fast lines: in the lanes by their units beside a polled line, in the "
					"legend with their newest values, read by the crosshair");
		}
		/* both drawing paths: the card's picture of fast lines against the CPU's (Windows) */
		const QVector<GpuLines::Adapter> adapters = GpuLines::adapters();
		if (adapters.isEmpty()) {
			check(true, "chart, fast lines on a GPU: no adapter on this machine (Direct3D 11 on Windows only): the CPU "
					"draws, skipped");
		} else {
			QWidget host;
			ChartView *view = makeView(host, 10.0);
			view->addSeries(vBus, QStringLiteral("ADC.V_BUS"), QStringLiteral("V"), QColor(0, 160, 0));
			feed(view, 0, 60000, 4.0);
			feed(view, 61000, 30000, 4.0, -1, 1000, false); /* a gap too */
			view->setWindow(6);
			view->showSpan(4.0, 10.0);
			host.show();
			(void) QTest::qWaitForWindowExposed(&host);
			view->setDrawing(adapters.first().dedicated ? ChartView::Drawing::Dedicated : ChartView::Drawing::Internal);
			(void) QTest::qWaitFor([&] { return !view->openingGpu(); }, 10000);
			for (int k = 0; k < 3; k++) {
				view->repaint();
				QApplication::processEvents();
			}
			QRect at;
			const QImage gpu = view->gpuPicture(&at).convertToFormat(QImage::Format_RGB32);
			const QImage cpu = host.grab().toImage().convertToFormat(QImage::Format_RGB32).copy(at);
			const double alike = blocksAlike(gpu, cpu, 24);
			if (!qEnvironmentVariableIsEmpty("EVRE_TEST_PICTURES")) {
				gpu.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/fast_card.png"));
				cpu.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/fast_cpu.png"));
			}
			std::printf("     (fast lines on %s: %.2f%% of the blocks like the CPU's)\n", qPrintable(view->drawingName()),
					alike * 100);
			check(view->plotOnCard() && alike >= 0.93, "chart, fast lines on a GPU: the card's picture of two fast lines "
					"with a gap, block by block the CPU's");
		}
	}
	/* the crosshair's box: made again at the values' pace or when the mouse moves, not at every frame (64 values
	 * laid out at every frame took 7 ms at 4K); its size follows the lines read, not their digits (it moved left and
	 * right with the widest value of the moment) */
	void readoutSteady() {
		QWidget host;
		host.resize(900, 420);
		auto *view = new ChartView(&host);
		view->setGeometry(5, 5, 880, 400);
		view->setClock([] { return 100.0; }, 0);
		view->setSmooth(false);
		view->setMemory(60);
		view->setWindow(60);
		view->setValuesPerSecond(1);
		view->addSeries(0, QStringLiteral("SUPPLY_V"), QStringLiteral("V"), QColor(0x25, 0x63, 0xEB));
		for (int i = 0; i <= 600; i++) view->append(0, 40.0 + i * 0.1, i < 300 ? 1.0 : -12345.6);
		auto hover = [&](double fraction) {
			const QPointF at(view->width() * fraction, view->height() / 2.0);
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
			(void) host.grab();
		};
		hover(0.3); /* over the 1.0 */
		const QSizeF small = view->readoutSize();
		const int builds = view->readoutBuilds(), legends = view->legendBuilds();
		for (int i = 0; i < 5; i++) (void) host.grab(); /* frames with the mouse still: the same box, the same legend */
		const bool kept = view->readoutBuilds() == builds;
		const bool legendKept = view->legendBuilds() == legends && legends > 0;
		QTest::qWait(60); /* past READOUT_FOLLOW_MS */
		hover(0.85); /* over the -12345.6 */
		const QSizeF large = view->readoutSize();
		if (!(kept && !small.isEmpty() && small == large && view->readoutBuilds() == builds + 1))
			std::printf("     (readout: kept %d, %gx%g then %gx%g, builds %d then %d)\n", kept, small.width(), small.height(),
					large.width(), large.height(), builds, view->readoutBuilds());
		check(kept && !small.isEmpty() && small == large && view->readoutBuilds() == builds + 1,
				"chart, the crosshair's box: made again when the mouse moves, not at every frame; the same size for 1 and "
				"-12345.6");
		check(legendKept, "chart, the legend: a picture made again when its values or lines change, not at every frame");

		/* its chips measured once while the lines stay, not at every frame and every mouse move (64 names measured each
		 * time held the chart near 52 frames a second); a line added: measured again */
		const int measured = view->legendMeasures();
		for (int i = 0; i < 5; i++) hover(0.3 + 0.05 * i);
		const bool measuredOnce = view->legendMeasures() == measured && measured > 0;
		view->addSeries(1, QStringLiteral("SUPPLY_I"), QStringLiteral("A"), QColor(0xD9, 0x77, 0x06));
		(void) host.grab();
		const bool remeasured = view->legendMeasures() == measured + 1;
		view->removeSeries(1);
		(void) host.grab();
		check(measuredOnce && remeasured, "chart, the legend: its chips measured once while the lines stay (not at "
				"every frame or mouse move), again when a line comes");

		/* the mouse moving: the box made again at most every 50 ms, not at every frame (64 values at 4K held the chart
		 * near 46 frames a second); where the mouse stops, its values once that time is past */
		QTest::qWait(60);
		const int before = view->readoutBuilds();
		QElapsedTimer moving;
		moving.start();
		for (int i = 0; i < 6; i++) hover(0.5 + 0.01 * i);
		const qint64 movedMs = moving.elapsed();
		const int whileMoving = view->readoutBuilds() - before;
		QTest::qWait(60);
		(void) host.grab(); /* made where the mouse stopped, unless its last move made it there */
		const int atStop = view->readoutBuilds() - before - whileMoving;
		(void) host.grab();
		const bool steady = view->readoutBuilds() - before - whileMoving - atStop == 0;
		const bool paced = whileMoving >= 1 && whileMoving <= 1 + movedMs / 50 && atStop <= 1 && steady;
		if (!paced)
			std::printf("     (6 moves in %lld ms: the box made %d times, then %d where the mouse stopped)\n",
					(long long) movedMs, whileMoving, atStop);
		check(paced, "chart, the crosshair's box: while the mouse moves, made again at most every 50 ms, and where it "
				"stops");

		view->setHoverValues(false);
		hover(0.5);
		const bool gone = view->readoutSize().isEmpty();
		view->setHoverValues(true);
		hover(0.4);
		const bool again = view->readoutSize() == small;
		check(gone && again, "chart, Hover values off: no box of values beside the mouse; on again, the box as before");
	}

	/* The view's bins kept from frame to frame: a chart fed in ten steps, drawn after each, draws what one fed at once
	 * does. The lines on a GPU (when this machine has one): the same picture as on the CPU, block by block. The
	 * Drawing list: Auto, the adapters, CPU; CPU picked is saved and the info line says it. */
	void chartBinsAndGpu() {
		constexpr int LINES = 12, HZ = 500;
		auto makeView = [](QWidget &host) {
			host.resize(1100, 480);
			auto *view = new ChartView(&host);
			view->setGeometry(9, 5, 1080, 470);
			view->setClock([] { return 3600.0; }, 0);
			view->setSmooth(false);
			view->setMemory(3600);
			view->setWindow(60);
			view->setDrawThreads(1);
			for (int k = 0; k < LINES; k++)
				view->addSeries(k, QStringLiteral("b%1").arg(k), QString(), QColor::fromHsv(k * 360 / LINES, 200, 230));
			return view;
		};
		auto sample = [](int k, int i) {
			const double t = 3540.0 + double(i) / HZ;
			return std::sin(t * (0.7 + 0.13 * k)) * (k + 1) + std::sin(t * 31.0) * 0.3;
		};
		QWidget stepsHost, onceHost;
		ChartView *steps = makeView(stepsHost), *once = makeView(onceHost);
		for (int step = 0; step < 10; step++) {
			for (int k = 0; k < LINES; k++)
				for (int i = step * 6 * HZ; i < (step + 1) * 6 * HZ; i++) steps->append(k, 3540.0 + double(i) / HZ, sample(k, i));
			stepsHost.grab(); /* a frame: the complete columns are kept */
		}
		for (int k = 0; k < LINES; k++)
			for (int i = 0; i < 60 * HZ; i++) once->append(k, 3540.0 + double(i) / HZ, sample(k, i));
		const QImage a = stepsHost.grab().toImage().convertToFormat(QImage::Format_RGB32);
		const QImage b = onceHost.grab().toImage().convertToFormat(QImage::Format_RGB32);
		const double kept = blocksAlike(a, b, 3);
		std::printf("     (kept bins vs binned at once: %.2f%% of the blocks alike)\n", kept * 100);
		check(kept >= 0.995, "chart, the view's bins kept from frame to frame: fed in ten steps, the picture of one fed at once");

		/* the crosshair's box with 64 lines: in columns that fit the plot's height */
		const int perColumn = ChartView::readoutRowsPerColumn(700);
		const int columns = (64 + perColumn - 1) / perColumn;
		check(perColumn >= 30 && (perColumn + 1) * 18 + 18 <= 700 && columns == 2,
				"chart, the crosshair's values with 64 lines: two columns in a 700 px plot, none past its bottom");

		/* the GPU: the first adapter, its frame in the window's layer against the CPU's picture of the same pixels */
		const QVector<GpuLines::Adapter> adapters = GpuLines::adapters();
		if (adapters.isEmpty()) {
			for (const char *what : { "opened on a thread", "its frame", "the cursors' tags and bar", "a note's tag",
					 "the trigger's level and tag", "the trigger's dashed level line", "Log Y", "the time grid's divisions", "lanes", "lanes scrolled and folded", "lanes resized", "a picture of the chart",
					 "another tab and back",
					 "the mouse", "the last line off" })
				check(true, qPrintable(QStringLiteral("chart on a GPU, %1: no adapter on this machine (Direct3D 11 on Windows only): "
						"the CPU draws, skipped").arg(QLatin1String(what))));
		} else {
			onceHost.show();
			(void) QTest::qWaitForWindowExposed(&onceHost);
			/* the card opened on a thread of its own: making its device wakes it (0.8 s held the window's thread when
			 * Drawing was switched); the CPU draws meanwhile, then the card takes over and says so */
			bool changed = false;
			const QMetaObject::Connection said = QObject::connect(once, &ChartView::drawingChanged, [&changed] {
				changed = true;
			});
			QElapsedTimer opening;
			opening.start();
			once->setDrawing(adapters.first().dedicated ? ChartView::Drawing::Dedicated : ChartView::Drawing::Internal);
			const double setMs = opening.nsecsElapsed() / 1e6;
			const bool meanwhile = once->openingGpu() && !once->drawsOnGpu()
					&& once->drawingName().startsWith(QLatin1String("CPU, opening the GPU: "));
			const bool opened = QTest::qWaitFor([&] { return !once->openingGpu(); }, 10000) && once->drawsOnGpu() && changed;
			QObject::disconnect(said);
			std::printf("     (the card asked for: back in %.1f ms, the card drawing %lld ms after)\n", setMs,
					(long long) opening.elapsed());
			check(setMs < 60 && meanwhile && opened, "chart on a GPU: the card opened on a thread of its own (the "
					"window's thread not held while it wakes), the CPU drawing meanwhile; then the card takes over and says so");
			once->setCursors(3570.0, 3571.5); /* the tags and the bar between them drawn by the card too */
			once->addNote(3585.0, QStringLiteral("a note")); /* its tag at the plot's bottom, a picture on the card */
			for (int k = 0; k < 3; k++) { /* two frames with the card's picture under its layer, then the layer */
				once->repaint();
				QApplication::processEvents();
			}
			QRect at;
			const QImage gpu = once->gpuPicture(&at).convertToFormat(QImage::Format_RGB32);
			const QImage cpu = onceHost.grab().toImage().convertToFormat(QImage::Format_RGB32);
			const bool onGpu = once->drawsOnGpu() && once->drawingName().startsWith(QLatin1String("GPU: ")) && once->plotOnCard()
					&& !once->testAttribute(Qt::WA_NativeWindow) && once->findChildren<QWidget *>().isEmpty();
			const double alike = blocksAlike(gpu, cpu.copy(at), 24);
			std::printf("     (%s: %.2f%% of the blocks like the CPU's, %dx%d)\n", qPrintable(once->drawingName()), alike * 100,
					gpu.width(), gpu.height());
			check(onGpu && alike >= 0.93, "chart on a GPU: the plot a layer of the window (no window of its own: the chart "
					"stays one of Qt's), drawn by the card named, its frame the CPU's picture, block by block");
			/* the cursors' tags and the bar between them: the layer's top 16 px (they sit 2 px above the plot, on the
			 * layer's edge), the card's against the CPU's */
			const int stripRows = int(std::ceil(16 * once->devicePixelRatioF()));
			const QImage gpuStrip = gpu.copy(0, 0, gpu.width(), stripRows);
			const QImage cpuStrip = cpu.copy(at).copy(0, 0, gpu.width(), stripRows);
			const double stripAlike = blocksAlike(gpuStrip, cpuStrip, 24);
			if (!qEnvironmentVariableIsEmpty("EVRE_TEST_PICTURES")) {
				gpuStrip.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/bar_card.png"));
				cpuStrip.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/bar_cpu.png"));
			}
			std::printf("     (the cursors' strip: %.2f%% of the blocks like the CPU's; the bar \"%s\")\n", stripAlike * 100,
					qPrintable(once->spanBarText()));
			check(!once->spanBarText().isEmpty() && stripAlike >= 0.93, "chart on a GPU: the cursors' tags and the bar "
					"between them, drawn by the card as the CPU draws them");
			/* the note's tag: the layer's bottom 24 px (it sits 4 to 20 px above the plot's bottom, the layer 2 px past it) */
			const int noteRows = int(std::ceil(24 * once->devicePixelRatioF()));
			const QImage gpuNote = gpu.copy(0, gpu.height() - noteRows, gpu.width(), noteRows);
			const QImage cpuNote = cpu.copy(at).copy(0, gpu.height() - noteRows, gpu.width(), noteRows);
			const double noteAlike = blocksAlike(gpuNote, cpuNote, 24);
			if (!qEnvironmentVariableIsEmpty("EVRE_TEST_PICTURES")) {
				gpuNote.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/note_card.png"));
				cpuNote.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/note_cpu.png"));
			}
			std::printf("     (the note's strip: %.2f%% of the blocks like the CPU's)\n", noteAlike * 100);
			check(once->notes().size() == 1 && noteAlike >= 0.93, "chart on a GPU: a note's tag drawn by the card as the CPU "
					"draws it");
			once->setNotes({});
			once->clearCursors();
			/* the trigger's level on b0 at 11.5, where only the two largest lines reach (at its mid-range the 12 lines
			 * cross): its dashed line, the card's against the CPU's, at the plot's right end (where its tab sat before it
			 * moved out of the plot) and the strip along the line; the tab lies outside the card's layer (the CPU's) */
			once->setTrigger(0, 11.5, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Normal);
			for (int k = 0; k < 3; k++) {
				once->repaint();
				QApplication::processEvents();
			}
			QRect atTrigger;
			const QImage gpuTrigger = once->gpuPicture(&atTrigger).convertToFormat(QImage::Format_RGB32);
			const QImage cpuTrigger = onceHost.grab().toImage().convertToFormat(QImage::Format_RGB32).copy(atTrigger);
			const QRectF tab = once->triggerLevelTag();
			const QRectF levelTag = tab.isEmpty() ? QRectF()
					: QRectF(once->lastPlot().right() - 64, once->triggerLineY() - 12, 64, 24);
			const qreal triggerDpr = once->devicePixelRatioF();
			const int tagTop = std::max(0, int(std::floor((levelTag.top() - 4 - (once->lastPlot().top() - 2)) * triggerDpr)));
			const int tagRows = std::min(gpuTrigger.height() - tagTop, int(std::ceil((levelTag.height() + 8) * triggerDpr)));
			/* the tag's own columns: the band's other blocks are the 12 lines crossing at their middle, whose edges the
			 * card rounds a little differently (92 % of the whole band's blocks alike on a Quadro T1000) */
			const int tagLeft = std::max(0, int(std::floor((levelTag.left() - 4 - (once->lastPlot().left() - 2)) * triggerDpr)));
			const int tagColumns = std::min(gpuTrigger.width() - tagLeft, int(std::ceil((levelTag.width() + 8) * triggerDpr)));
			const QRect tagArea(tagLeft, tagTop, tagColumns, tagRows);
			const double triggerAlike = levelTag.isEmpty() || tagRows <= 0 || tagColumns <= 0 ? 0
					: blocksAlike(gpuTrigger.copy(tagArea), cpuTrigger.copy(tagArea), 24);
			if (!qEnvironmentVariableIsEmpty("EVRE_TEST_PICTURES")) {
				gpuTrigger.copy(tagArea).save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/trigger_card.png"));
				cpuTrigger.copy(tagArea).save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/trigger_cpu.png"));
			}
			/* the card's layer is the plot and 2 px around it: the tab right of that, none of it under the card's picture */
			const bool tabOutside = !tab.isEmpty() && tab.left() > once->lastPlot().right() + 2;
			std::printf("     (the trigger's level \"%s\" at the plot's right end: %.2f%% of the blocks like the CPU's; its "
					"tab at x %.0f, the plot's right %.0f)\n", qPrintable(once->triggerTagText()), triggerAlike * 100, tab.left(),
					once->lastPlot().right());
			check(once->plotOnCard() && !levelTag.isEmpty() && triggerAlike >= 0.93 && tabOutside, "chart on a GPU: the "
					"trigger's level line at the plot's right end drawn by the card as the CPU draws it; its tab right of the "
					"card's layer (the CPU's on both paths)");
			/* the dashed line: 8 px along it, from the plot's left to the tag */
			const double lineY = once->triggerLineY();
			const int lineTop = std::max(0, int(std::floor((lineY - 4 - (once->lastPlot().top() - 2)) * triggerDpr)));
			const QRect lineArea(0, lineTop, std::max(0, tagLeft), std::min(gpuTrigger.height() - lineTop, int(std::ceil(8 * triggerDpr))));
			const double lineAlike = !std::isfinite(lineY) || lineArea.width() < 100 || lineArea.height() < 8 ? 0
					: blocksAlike(gpuTrigger.copy(lineArea), cpuTrigger.copy(lineArea), 24);
			if (!qEnvironmentVariableIsEmpty("EVRE_TEST_PICTURES")) {
				gpuTrigger.copy(lineArea).save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/trigger_line_card.png"));
				cpuTrigger.copy(lineArea).save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/trigger_line_cpu.png"));
			}
			std::printf("     (the trigger's dashed line at y %.1f: %.2f%% of the blocks like the CPU's)\n", lineY, lineAlike * 100);
			check(once->plotOnCard() && lineAlike >= 0.93, "chart on a GPU: the trigger's dashed level line drawn by the card "
					"as the CPU draws it, the strip along it compared");
			once->stopTrigger();
			once->setLive(true); /* Normal held the view when it was armed */
			/* Log Y: the card's segments and grid from the same Axes as the CPU's lines (decades, the faint 2..9) */
			once->setYLog(true);
			for (int k = 0; k < 3; k++) {
				once->repaint();
				QApplication::processEvents();
			}
			QRect atLog;
			const QImage gpuLog = once->gpuPicture(&atLog).convertToFormat(QImage::Format_RGB32);
			const QImage cpuLog = onceHost.grab().toImage().convertToFormat(QImage::Format_RGB32).copy(atLog);
			const double logAlike = blocksAlike(gpuLog, cpuLog, 24);
			if (!qEnvironmentVariableIsEmpty("EVRE_TEST_PICTURES")) {
				gpuLog.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/log_card.png"));
				cpuLog.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/log_cpu.png"));
			}
			std::printf("     (Log Y: %.2f%% of the blocks like the CPU's)\n", logAlike * 100);
			check(once->yLog() && once->plotOnCard() && logAlike >= 0.93, "chart on a GPU: Log Y drawn by the card as the "
					"CPU draws it, block by block");
			once->setYLog(false);
			/* the time grid's divisions: the card's lines at the CPU's places (each tenth of the plot) */
			once->setTimeGrid(ChartView::TimeGrid::Divisions);
			for (int k = 0; k < 3; k++) {
				once->repaint();
				QApplication::processEvents();
			}
			QRect atGrid;
			const QImage gpuGrid = once->gpuPicture(&atGrid).convertToFormat(QImage::Format_RGB32);
			const QImage cpuGrid = onceHost.grab().toImage().convertToFormat(QImage::Format_RGB32).copy(atGrid);
			const double gridAlike = blocksAlike(gpuGrid, cpuGrid, 24);
			if (!qEnvironmentVariableIsEmpty("EVRE_TEST_PICTURES")) {
				gpuGrid.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/divisions_card.png"));
				cpuGrid.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/divisions_cpu.png"));
			}
			std::printf("     (the time grid's divisions: %.2f%% of the blocks like the CPU's)\n", gridAlike * 100);
			check(once->divisionsShown() && once->timeGridX().size() == 9 && once->plotOnCard() && gridAlike >= 0.93,
					"chart on a GPU: the time grid's divisions drawn by the card as the CPU draws them, block by block");
			once->setTimeGrid(ChartView::TimeGrid::Auto);
			/* lanes: a line in volts beside the lines with no unit, two lanes; the card's segments from each lane's Axes */
			once->addSeries(LINES, QStringLiteral("volts"), QStringLiteral("V"), QColor(0xE0, 0x80, 0x20));
			for (int i = 0; i < 60 * HZ; i++) once->append(LINES, 3540.0 + double(i) / HZ, 12.0 + std::sin(i * 0.01));
			once->setLanes(true);
			for (int k = 0; k < 3; k++) {
				once->repaint();
				QApplication::processEvents();
			}
			QRect atLanes;
			const QImage gpuLanes = once->gpuPicture(&atLanes).convertToFormat(QImage::Format_RGB32);
			const QImage cpuLanes = onceHost.grab().toImage().convertToFormat(QImage::Format_RGB32).copy(atLanes);
			const double lanesAlike = blocksAlike(gpuLanes, cpuLanes, 24);
			if (!qEnvironmentVariableIsEmpty("EVRE_TEST_PICTURES")) {
				gpuLanes.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/lanes_card.png"));
				cpuLanes.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/lanes_cpu.png"));
			}
			std::printf("     (lanes: %.2f%% of the blocks like the CPU's)\n", lanesAlike * 100);
			/* 0.90, not 0.93: the lines squeezed into half the height put line edges in many more blocks, and the edges'
			 * antialiasing rounds a little differently on the card (91.8 % on a Quadro T1000; no lane, line or label
			 * out of place) */
			check(once->lanes() && once->plotOnCard() && lanesAlike >= 0.90, "chart on a GPU: lanes (two units) drawn by the "
					"card as the CPU draws them, block by block");
			/* lanes that do not fit: eight more units, scrolled half a lane (the first two cut by the plot's top), the
			 * third folded: its strip a picture over the layer, cut lanes cut on the card as on the CPU. One slow wave
			 * for all, each at its own phase: a wave of a few samples per cycle is a dense zig-zag, which the card and
			 * the CPU draw differently with or without lanes, and it is the lanes' places this compares */
			for (int u = 1; u <= 8; u++) {
				once->addSeries(LINES + u, QStringLiteral("u%1").arg(u), QStringLiteral("u%1").arg(u),
						Theme::colors().series[u % Theme::colors().series.size()]);
				for (int i = 0; i < 60 * HZ; i += 10) once->append(LINES + u, 3540.0 + double(i) / HZ, u + std::sin(i * 0.01 + u));
			}
			once->setLaneScroll(ChartView::LANE_MIN_H / 2);
			once->setLaneFolded(2, true);
			for (int k = 0; k < 3; k++) {
				once->repaint();
				QApplication::processEvents();
			}
			QRect atFit;
			const QImage gpuFit = once->gpuPicture(&atFit).convertToFormat(QImage::Format_RGB32);
			const QImage cpuFit = onceHost.grab().toImage().convertToFormat(QImage::Format_RGB32).copy(atFit);
			const double fitAlike = blocksAlike(gpuFit, cpuFit, 24);
			if (!qEnvironmentVariableIsEmpty("EVRE_TEST_PICTURES")) {
				gpuFit.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/lanes_fit_card.png"));
				cpuFit.save(qEnvironmentVariable("EVRE_TEST_PICTURES") + QStringLiteral("/lanes_fit_cpu.png"));
			}
			std::printf("     (lanes scrolled and folded: %.2f%% of the blocks like the CPU's)\n", fitAlike * 100);
			check(once->plotOnCard() && once->laneScroll() > 0 && once->laneFolded(2) && !once->laneScrollBarRect().isEmpty()
					&& fitAlike >= 0.90, "chart on a GPU: lanes scrolled half a lane, one folded, drawn by the card as the "
					"CPU draws them, block by block (the strip too)");
			once->setLaneFolded(2, false);
			/* lanes resized by their borders: the first two taller, the third lower */
			for (int u = 3; u <= 8; u++) once->removeSeries(LINES + u);
			once->setLaneScroll(0);
			once->setLaneHeights({ QStringLiteral("\t1.4"), QStringLiteral("u1\t0.7") });
			for (int k = 0; k < 3; k++) {
				once->repaint();
				QApplication::processEvents();
			}
			QRect atSized;
			const QImage gpuSized = once->gpuPicture(&atSized).convertToFormat(QImage::Format_RGB32);
			const QImage cpuSized = onceHost.grab().toImage().convertToFormat(QImage::Format_RGB32).copy(atSized);
			const double sizedAlike = blocksAlike(gpuSized, cpuSized, 24);
			std::printf("     (lanes resized: %.2f%% of the blocks like the CPU's)\n", sizedAlike * 100);
			check(once->plotOnCard() && once->laneRect(0).height() > once->laneRect(1).height() && sizedAlike >= 0.90,
					"chart on a GPU: lanes resized by their borders drawn by the card as the CPU draws them");
			once->setLaneHeights({});
			for (int u = 1; u <= 2; u++) once->removeSeries(LINES + u);
			once->setLanes(false);
			once->removeSeries(LINES);
			for (int k = 0; k < 3; k++) {
				once->repaint();
				QApplication::processEvents();
			}
			const double grabbed =blocksAlike(onceHost.grab().toImage().convertToFormat(QImage::Format_RGB32), b, 3);
			if (grabbed < 0.995) std::printf("     (a picture of the chart on a card vs on the CPU: %.2f%%)\n", grabbed * 100);
			check(grabbed >= 0.995, "chart on a GPU: a picture of the chart (grab) has the plot, drawn by the CPU");

			/* another tab and back: the layer stays until the window has painted what is there now (taken away at once,
			 * the window's old pixels showed until then), and comes back after the chart's second frame, its picture
			 * painted under it (shown at once, it showed its frame of when the chart was left) */
			once->hide();
			const bool stays = once->plotOnCard();
			QApplication::processEvents();
			const bool gone = !once->plotOnCard();
			once->show();
			once->repaint();
			QApplication::processEvents();
			const bool waits = !once->plotOnCard(); /* one frame on the window: the layer not yet */
			once->repaint();
			QApplication::processEvents();
			const bool back = once->plotOnCard();
			if (!(stays && gone && waits && back))
				std::printf("     (another tab and back: stays %d, gone %d, waits %d, back %d)\n", stays, gone, waits, back);
			check(stays && gone && waits && back, "chart on a GPU, another tab and back: the layer goes once the window has "
					"painted what replaces it, and comes back after the chart's second frame, not before");

			/* the mouse over the plot is the chart's (no window of its own in the way): the crosshair's box is made, the
			 * wheel zooms the time; on the CPU the layer goes */
			const int builds = once->readoutBuilds();
			const QPointF inside(once->width() * 0.7, once->height() / 2.0);
			QMouseEvent move(QEvent::MouseMove, inside, once->mapToGlobal(inside), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(once, &move);
			once->repaint();
			const double window = once->window();
			QWheelEvent wheel(inside, once->mapToGlobal(inside), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
					Qt::NoScrollPhase, false);
			QApplication::sendEvent(once, &wheel);
			const bool mouse = once->readoutBuilds() > builds && once->window() < window;
			if (!mouse) std::printf("     (mouse: box made %d -> %d, window %.1f -> %.1f s)\n", builds, once->readoutBuilds(),
					window, once->window());
			once->setDrawing(ChartView::Drawing::Cpu);
			check(mouse && !once->plotOnCard(), "chart on a GPU: the mouse over the plot moves the crosshair, the wheel "
					"zooms; on the CPU the layer goes");

			/* the last line off the chart: the layer goes once the window itself has the CPU's frame, its plot too (a
			 * frame painted around the plot alone, as while the layer was there, left the old lines in the window,
			 * which showed for a frame where the layer had been). Two frames, then the layer: the window holds the
			 * lines painted under it, as in the app, where nothing paints the plot's part while the layer is there */
			once->setDrawing(adapters.first().dedicated ? ChartView::Drawing::Dedicated : ChartView::Drawing::Internal);
			(void) QTest::qWaitFor([&] { return !once->openingGpu(); }, 10000);
			for (int k = 0; k < 2; k++) {
				once->repaint();
				QApplication::processEvents();
			}
			const bool layered = once->plotOnCard();
			once->clearSeries();
			for (int k = 0; k < 20 && once->plotOnCard(); k++) QTest::qWait(10);
			QTest::qWait(50);
			const QImage own = onceHost.screen()->grabWindow(onceHost.winId()).toImage().convertToFormat(QImage::Format_RGB32);
			const QImage empty = onceHost.grab().toImage().convertToFormat(QImage::Format_RGB32);
			const double ownAlike = own.size() == empty.size() ? blocksAlike(own.copy(at), empty.copy(at), 24) : 0;
			if (!(layered && !once->plotOnCard() && ownAlike >= 0.97))
				std::printf("     (last line off: layer before %d, after %d, the window's plot %.2f%% like the CPU's)\n", layered,
						once->plotOnCard(), ownAlike * 100);
			check(layered && !once->plotOnCard() && ownAlike >= 0.97, "chart on a GPU, the last line off: the layer goes "
					"once the window has the CPU's frame, its plot too (not the old lines)");
			once->setDrawing(ChartView::Drawing::Cpu);
			onceHost.hide();
		}

		/* Drawing, in the Chart tab's Display menu: Auto, the adapters by name, CPU (the choices of one group) */
		auto *display = window_.findChild<QPushButton *>(QStringLiteral("chartDisplay"));
		auto *chartTab = window_.findChild<ChartTab *>();
		QList<QAction *> choices;
		if (display && display->menu())
			for (QAction *action : display->menu()->actions())
				if (action->actionGroup() && !action->objectName().startsWith(QLatin1String("chartTimeGrid")))
					choices << action; /* the Drawing group's, not the Time grid's */
		const bool listed = chartTab && choices.size() == adapters.size() + 2
				&& choices.first()->text().startsWith(QLatin1String("Auto")) && choices.last()->text() == QLatin1String("CPU");
		if (listed) choices.last()->trigger();
		const QString info = chartTab ? chartTab->infoText() : QString();
		const bool cpu = listed && QSettings().value(QStringLiteral("chart/drawing")).toInt() == int(ChartView::Drawing::Cpu)
				&& info.endsWith(QStringLiteral(" · CPU")) && choices.last()->isChecked()
				&& display->toolTip().contains(QStringLiteral("drawn by the CPU."));
		if (listed) choices.first()->trigger();
		if (auto *chartView = window_.findChild<ChartView *>()) /* the card opens on a thread of its own */
			(void) QTest::qWaitFor([&] { return !chartView->openingGpu(); }, 10000);
		const QString autoState = chartTab ? chartTab->displayState() : QString(); /* Auto with what it chose */
		const bool autoSaid = listed && choices.first()->isChecked() && autoState.contains(adapters.isEmpty()
				|| !adapters.first().dedicated ? QStringLiteral("drawn by the CPU") : QStringLiteral("drawn by the GPU: "));
		if (!cpu) std::printf("     (Drawing: %d choices for %d adapters, info \"%s\")\n", int(choices.size()),
				int(adapters.size()), qPrintable(info));
		if (!autoSaid) std::printf("     (Display on Auto: \"%s\")\n", qPrintable(autoState));
		check(listed && cpu && autoSaid, "chart, Display menu, Drawing: Auto, every adapter by name, CPU; CPU picked is "
				"ticked, saved, in the button's tooltip, the info line ends \"CPU\"; on Auto the tooltip names who draws");
	}

	/* Normalise and Smooth, in the Display menu with Drawing (in the row they widened it past a 1280-wide window):
	 * Normalise takes the Y controls away, Smooth is saved; both in the button's tooltip; the button keeps its text */
	void displayMenu() {
		auto *display = window_.findChild<QPushButton *>(QStringLiteral("chartDisplay"));
		auto *normalise = window_.findChild<QAction *>(QStringLiteral("chartNormalise"));
		auto *smooth = window_.findChild<QAction *>(QStringLiteral("chartSmooth"));
		auto *chartTab = window_.findChild<ChartTab *>();
		if (!display || !normalise || !smooth || !chartTab || !display->menu()) {
			check(false, "chart, Display menu: the button, Normalise and Smooth found");
			return;
		}
		const QList<QAction *> items = display->menu()->actions();
		const bool inMenu = items.contains(normalise) && items.contains(smooth);
		auto *yMin = chartTab->findChild<QLineEdit *>();
		const bool smoothWas = smooth->isChecked();
		normalise->trigger();
		const bool normalised = normalise->isChecked() && yMin && !yMin->isEnabled()
				&& display->toolTip().contains(QStringLiteral("Normalise on"));
		normalise->trigger();
		const bool back = !normalise->isChecked() && yMin && yMin->isEnabled()
				&& display->toolTip().contains(QStringLiteral("Normalise off"));
		smooth->trigger();
		const bool smoothSaved = QSettings().value(QStringLiteral("chart/smooth")).toBool() == !smoothWas
				&& display->toolTip().contains(smoothWas ? QStringLiteral("Smooth off") : QStringLiteral("Smooth on"));
		smooth->trigger(); /* as it was */
		auto *hoverValues = window_.findChild<QAction *>(QStringLiteral("chartHoverValues"));
		bool hoverSaved = false;
		if (hoverValues && items.contains(hoverValues)) {
			const bool was = hoverValues->isChecked();
			hoverValues->trigger();
			hoverSaved = QSettings().value(QStringLiteral("chart/hoverValues")).toBool() == !was
					&& window_.findChild<ChartView *>()->hoverValues() == !was
					&& display->toolTip().contains(was ? QStringLiteral("Hover values off") : QStringLiteral("Hover values on"));
			hoverValues->trigger(); /* as it was */
		}
		check(hoverSaved, "chart, Display menu: Hover values switches the box of values, is saved and said in the tooltip");
		const bool sameText = display->text() == QLatin1String("Display");
		if (!(inMenu && normalised && back && smoothSaved && sameText))
			std::printf("     (Display: in menu %d, normalised %d, back %d, smooth saved %d, text \"%s\")\n", inMenu,
					normalised, back, smoothSaved, qPrintable(display->text()));
		check(inMenu && normalised && back && smoothSaved && sameText, "chart, Display menu: Normalise greys the Y range "
				"and back, Smooth is saved, both said in the tooltip; the button's text stays \"Display\"");

		/* the marks of a menu: the theme's pictures at 4x (Fusion's own was small and blurred at 225 %), a box with a
		 * tick for a check, a tick for the choice of several; the choices under a title the style shows */
		const QString sheet = qApp->styleSheet();
		const QRegularExpressionMatch choice =
				QRegularExpression(QStringLiteral("QMenu::indicator:exclusive:checked \\{ image: url\\(\"([^\"]+)\"\\)"))
						.match(sheet);
		const bool marks = choice.hasMatch() && QImage(choice.captured(1)).size() == QSize(64, 64)
				&& sheet.contains(QLatin1String("QMenu::indicator:non-exclusive:checked { background:"));
		bool titled = false;
		for (QAction *action : display->menu()->actions())
			if (auto *widgetAction = qobject_cast<QWidgetAction *>(action))
				if (auto *title = qobject_cast<QLabel *>(widgetAction->defaultWidget()))
					titled = titled || (title->objectName() == QLatin1String("menuTitle")
							&& title->text() == QLatin1String("Drawing"));
		check(marks && titled, "chart, Display menu: its marks drawn by the theme at 4x (sharp at any scaling), the "
				"Drawing choices under a title");

		/* the popups open without Qt's animations (the system's setting turned them on): a drop-down flashed as its slide
		 * went before it was drawn, and the fades copy the screen without the plot a card draws */
		bool animated = false;
		for (const Qt::UIEffect effect : { Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip,
					 Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox })
			animated = animated || QApplication::isEffectEnabled(effect);
		check(!animated, "the theme: menus, drop-downs and tooltips open without animations");
	}

	/* The Help: every page opens with its heading and text, every number put in (no %NAME% left), and the command
	 * line page lists the options the Studio takes (--bus, --tab with the Map editor) */
	void helpPages() {
		HelpDialog help;
		auto *topics = help.findChild<QListWidget *>(QStringLiteral("helpTopics"));
		auto *page = help.findChild<QTextBrowser *>();
		bool filled = topics && page && topics->count() >= 12;
		QString commandLine, empty;
		for (int i = 0; filled && i < topics->count(); i++) {
			topics->setCurrentRow(i);
			const QString text = page->toPlainText();
			if (text.size() < 200 || text.contains(QRegularExpression(QStringLiteral("%[A-Z_]+%")))
					|| page->toHtml().indexOf(QLatin1String("<h2")) < 0)
				empty += topics->item(i)->text() + QLatin1Char(' ');
			if (topics->item(i)->text() == QLatin1String("Command line")) commandLine = text;
		}
		const bool options = commandLine.contains(QLatin1String("--bus bus.json"))
				&& commandLine.contains(QLatin1String("--tab registers|chart|monitor|map"));
		if (!empty.isEmpty()) std::printf("     (help pages without their text: %s)\n", qPrintable(empty));
		check(filled && empty.isEmpty() && options, "Help: every page opens with its heading and text, its numbers "
				"put in; the command line page lists --bus and the Map editor's --tab");
		/* Trigger v2: the Chart page says how it is armed and what the modes, the hold-off, the tab and the flag do */
		QString chartPage, keysPage;
		for (int i = 0; filled && i < topics->count(); i++) {
			topics->setCurrentRow(i);
			if (topics->item(i)->text() == QLatin1String("Chart & recording")) chartPage = page->toPlainText();
			if (topics->item(i)->text() == QLatin1String("Keys & mouse")) keysPage = page->toPlainText();
		}
		for (const QString &piece : { QStringLiteral("T ▼ flag above the chart"), QStringLiteral("to its tab right"),
					 QStringLiteral("T▸ marker"), QStringLiteral("0.4 A ↑"), QStringLiteral("Short windows lock by themselves"),
					 QStringLiteral("Display → Lock short windows"), QStringLiteral("the tab's arrow") })
			if (!chartPage.contains(piece) && !keysPage.contains(piece)) std::printf("     (the Help lacks \"%s\")\n", qPrintable(piece));
		check(chartPage.contains(QLatin1String("Trigger on this line")) && chartPage.contains(QLatin1String("hold-off"))
						&& chartPage.contains(QLatin1String("stays held until the next one, however long"))
						&& chartPage.contains(QLatin1String("Hold / Live is Stop / Run"))
						&& chartPage.contains(QStringLiteral("T ▼ flag above the chart"))
						&& chartPage.contains(QLatin1String("to its tab right")) && chartPage.contains(QStringLiteral("0.4 A ↑"))
						&& chartPage.contains(QStringLiteral("from its T▸ marker left of the chart"))
						&& chartPage.contains(QLatin1String("Short windows lock by themselves"))
						&& chartPage.contains(QStringLiteral("Display → Lock short windows"))
						&& chartPage.contains(QLatin1String("its arrow over the crossing"))
						&& chartPage.contains(QLatin1String("Force")) && chartPage.contains(QLatin1String("Find level"))
						&& chartPage.contains(QLatin1String("double-click it for 50 % again"))
						&& chartPage.contains(QLatin1String("above range")) && keysPage.contains(QLatin1String("the tab's arrow"))
						&& keysPage.contains(QStringLiteral("T ▼ flag above the chart"))
						&& keysPage.contains(QStringLiteral("T▸ marker (left of the chart)")),
				"Help: the Chart page's Trigger says how it is armed (Trigger on this line), the level's tab and a level off "
				"scale, its T▸ marker, Auto, Normal and Single, Force and Find level, Run and Stop, the hold-off, the flag over the "
				"crossing (50 %, a double-click), the short windows' lock; Keys & mouse lists the marker, the tab and the flag");
	}

	/* the measurements of many lines (60, on a Chart tab of its own, 10 s of 500 Hz samples each): a refresh of the
	 * table takes milliseconds, not the window's thread (each cell changed made its column measure every row) */
	void measuresManyLines() {
		ChartTab tab([] { return 100.0; });
		tab.resize(1400, 800);
		constexpr int LINES = 60, HZ = 500;
		MathLines::Samples samples;
		for (int k = 0; k < LINES; k++) {
			RegDef def;
			def.addr = uint16_t(0xD000 + 2 * k);
			def.name = QStringLiteral("M%1").arg(k);
			def.unit = QStringLiteral("V");
			tab.plotRegister(def, true);
			QVector<QPointF> &points = samples[regKey(def)];
			for (int i = 0; i < 10 * HZ; i++) points << QPointF(90.0 + double(i) / HZ, std::sin(i * 0.01 * (k + 1)) * k);
		}
		tab.frame(samples);
		auto *measure = tab.findChild<QPushButton *>(QStringLiteral("measure"));
		auto *table = tab.findChild<QTableWidget *>(QStringLiteral("measures"));
		if (measure) measure->setChecked(true); /* measured at once */
		measured(tab.view());
		QElapsedTimer timer;
		timer.start();
		tab.setShown(true); /* measured again: the table refreshed, every value the same */
		measured(tab.view());
		for (int round = 0; round < 3; round++) { /* new values in every cell */
			MathLines::Samples more;
			for (int k = 0; k < LINES; k++)
				more[regKey(0, uint16_t(0xD000 + 2 * k))] << QPointF(100.0 + round * 0.002, 1000.0 * (round + 1) + k);
			tab.frame(more);
			tab.setShown(true);
			measured(tab.view());
		}
		const double ms = timer.nsecsElapsed() / 1e6 / 4;
		tab.setRegisterLimit(64);
		const QString info = tab.infoText();
		std::printf("     (the measurements of %d lines: %.1f ms a refresh)\n", LINES, ms);
		check(measure && table && table->rowCount() == LINES && ms < 60,
				"chart, measurements of 60 lines: a refresh of the table takes milliseconds (columns fitted once, not at "
				"every cell)");
		if (!info.startsWith(QStringLiteral("60/64 plotted · "))) std::printf("     (info line: \"%s\")\n", qPrintable(info));
		if (measure) measure->setChecked(false); /* the setting back as the other steps expect it */
		check(info.startsWith(QStringLiteral("60/64 plotted · "))
						&& (info.contains(QStringLiteral(" fps · ")) || info.contains(QStringLiteral(" idle · "))),
				"chart, the info line: the registers on the chart of the limit first (\"60/64 plotted\"), then "
				"the frames (idle: none painted in the last second)");
	}

	/* The frame budget, 600 frames of 60 Hz worked out (no clock): cheap frames all painted; frames a little over
	 * 60 % of the refresh skip one now and then (waiting after each frame over 10 ms halved the rate: 35 a second for
	 * 11.5 ms frames); heavy ones every other refresh (60 % of the thread at most); one very slow frame (a theme
	 * switch) holds back a frame or two, not seconds */
	void frameBudget() {
		auto run = [](double paintMs, double slowMs) {
			ChartView::FrameBudget budget;
			int painted = 0;
			for (int i = 0; i < 600; i++) {
				if (!budget.due(1000.0 / 60)) continue;
				painted++;
				budget.spent(i == 10 && slowMs > 0 ? slowMs : paintMs);
			}
			return painted;
		};
		const int cheap = run(5, 0), over = run(11, 0), heavy = run(20, 0), slow = run(5, 400), slowAll = run(41, 0);
		std::printf("     (frames painted of 600 at 60 Hz: 5 ms %d, 11 ms %d, 20 ms %d, 5 ms with one of 400 ms %d, 41 ms "
				"%d)\n", cheap, over, heavy, slow, slowAll);
		check(cheap == 600 && over >= 530 && over <= 565 && heavy >= 290 && heavy <= 310 && slow >= 597
						&& slowAll >= 135 && slowAll <= 160,
				"chart, the frame budget: frames a little over 60 % of a refresh skip one now and then (11 ms: about 55 a "
				"second, not 35), heavy ones every other refresh, slow ones all along 60 % of the thread too, one slow "
				"frame holds back a frame or two");
	}

	/* The mouse and the cursors paced: while frames come, a mouse move is painted by the next frame, not at once (a
	 * frame each made 35 frames a second of 60 with 64 lines); a cursor dragged measures at once, then at most every
	 * 100 ms, the last place always; Cursors off takes A and B away; the lines measured on threads as one by one */
	void chartFollowsFrames() {
		ChartTab tab([] { return 100.0; });
		tab.resize(1200, 700);
		constexpr int LINES = 8, HZ = 1000;
		MathLines::Samples samples;
		QVector<int> keys;
		for (int k = 0; k < LINES; k++) {
			RegDef def;
			def.addr = uint16_t(0xD000 + 2 * k);
			def.name = QStringLiteral("F%1").arg(k);
			def.unit = QStringLiteral("V");
			tab.plotRegister(def, true);
			keys << int(regKey(def));
			QVector<QPointF> &points = samples[regKey(def)];
			for (int i = 0; i < 9 * HZ; i++) points << QPointF(90.0 + double(i) / HZ, std::sin(i * 0.003 * (k + 1)) * (k + 1));
		}
		auto *view = tab.findChild<ChartView *>();
		auto *measure = tab.findChild<QPushButton *>(QStringLiteral("measure"));
		auto *cursors = tab.findChild<QPushButton *>(QStringLiteral("cursors"));
		auto *table = tab.findChild<QTableWidget *>(QStringLiteral("measures"));
		if (!view || !measure || !cursors || !table) {
			check(false, "chart paced by the frames: the chart, Measure, Cursors and the table found");
			return;
		}
		tab.show();
		(void) QTest::qWaitForWindowExposed(&tab);
		tab.frame(samples); /* frames come from now on */
		QApplication::processEvents();

		/* the mouse: 30 moves, then a frame */
		const int before = view->paints();
		for (int i = 0; i < 30; i++) {
			const QPointF at(view->width() * (0.3 + 0.01 * i), view->height() / 2.0);
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
			QApplication::processEvents();
		}
		QTest::qWait(150); /* the next frame due by the frame budget, after the first frame's paint (slow on xvfb) */
		const int betweenFrames = view->paints() - before;
		tab.frame({});
		QApplication::processEvents();
		const int atFrame = view->paints() - before - betweenFrames;
		if (betweenFrames != 0 || atFrame != 1)
			std::printf("     (30 mouse moves: %d frames painted between frames, %d at the frame)\n", betweenFrames, atFrame);
		check(betweenFrames == 0 && atFrame == 1, "chart: the mouse's moves painted by the next frame, none in between "
				"(the frames at the display's rate and within the frame budget)");

		/* a cursor dragged: measured at once, then at most every 100 ms, and at its last place */
		cursors->setChecked(true); /* Measure on too */
		view->setCursors(92.0, 97.0);
		QTest::qWait(150);
		const int measured = tab.measureUpdates(), fullBefore = tab.measureFullUpdates();
		QElapsedTimer dragTime;
		dragTime.start();
		const QPointF grab(view->width() * 0.2, view->height() / 2.0);
		QMouseEvent press(QEvent::MouseButtonPress, grab, view->mapToGlobal(grab), Qt::LeftButton, Qt::LeftButton,
				Qt::NoModifier);
		QApplication::sendEvent(view, &press);
		for (int i = 0; i < 40; i++) {
			const QPointF at(view->width() * (0.2 + 0.005 * i), view->height() / 2.0);
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
			QTest::qWait(20); /* a drag of most of a second */
		}
		const int fullWhileDragged = tab.measureFullUpdates() - fullBefore;
		QMouseEvent release(QEvent::MouseButtonRelease, grab, view->mapToGlobal(grab), Qt::LeftButton, Qt::NoButton,
				Qt::NoModifier);
		QApplication::sendEvent(view, &release);
		const qint64 dragMs = dragTime.elapsed();
		const int during = tab.measureUpdates() - measured;
		QTest::qWait(150);
		QStringList shown;
		for (int row = 0; row < table->rowCount(); row++)
			for (int column = 0; column < table->columnCount(); column++)
				shown << (table->item(row, column) ? table->item(row, column)->text() : QString());
		tab.setShown(true); /* measured again now: the same, if the last place was measured */
		QStringList now;
		for (int row = 0; row < table->rowCount(); row++)
			for (int column = 0; column < table->columnCount(); column++)
				now << (table->item(row, column) ? table->item(row, column)->text() : QString());
		const bool paced = during >= 1 && during <= 2 + dragMs / 100 + dragMs / 250; /* the timer's too */
		if (!paced || shown != now)
			std::printf("     (a cursor dragged 40 moves in %lld ms: measured %d times; the last place %s)\n",
					(long long) dragMs, during, shown == now ? "measured" : "NOT measured");
		check(paced && shown == now, "chart, Measure: a cursor dragged is measured at once, then at most every 100 ms "
				"(not at every mouse move), and at the place it was left");
		const bool lightWhileDragged = fullWhileDragged <= 1 + dragMs / 250;
		if (!lightWhileDragged)
			std::printf("     (a drag of %lld ms: all of the table measured %d times)\n", (long long) dragMs, fullWhileDragged);
		check(lightWhileDragged, "chart, Measure: while a cursor is dragged only A, B and B - A follow it; the rest once "
				"it is let go (and at the timer's pace)");

		/* the lines measured on the chart's threads: what each measured alone gives */
		const QVector<ChartView::Stats> together = view->stats(keys);
		bool same = together.size() == keys.size();
		for (int i = 0; same && i < keys.size(); i++) {
			const ChartView::Stats alone = view->stats(keys[i]);
			same = alone.ok == together[i].ok && alone.min == together[i].min && alone.max == together[i].max
					&& alone.mean == together[i].mean && alone.rms == together[i].rms
					&& alone.integral == together[i].integral && alone.n == together[i].n;
		}
		check(same && together.size() == LINES, "chart, Measure: the lines measured on threads as each alone");

		/* Cursors off: A and B go, as with Clear cursors */
		cursors->setChecked(false);
		check(std::isnan(view->cursorA()) && std::isnan(view->cursorB()), "chart: Cursors off takes cursors A and B "
				"off the chart");
		measure->setChecked(false); /* the setting back as the other steps expect it */
		tab.hide();
	}

	/* The bar between the cursors' tags: the time between them as durationText writes it, whichever comes first; a span
	 * too narrow for the text puts it beside the right tag; a cursor off the view ends the bar at the plot's edge.
	 * Painted by grab() (the CPU), the view held so that the cursors stay where they are from one picture to the next. */
	void cursorSpanBar() {
		const bool texts = durationText(123e-6) == QStringLiteral("123 µs")
				&& durationText(3.525e-3) == QLatin1String("3.525 ms") && durationText(12.35) == QLatin1String("12.35 s")
				&& durationText(83.4) == QLatin1String("1 min 23.4 s") && durationText(7500) == QLatin1String("2 h 05 min")
				&& durationText(999.96e-6) == QLatin1String("1 ms") && durationText(-0.5) == QLatin1String("500 ms");
		check(texts, "chart, A-B bar: the time written as 123 µs, 3.525 ms, 12.35 s, 1 min 23.4 s, 2 h 05 min (999.96 µs "
				"is 1 ms; the sign dropped)");

		ChartTab tab([] { return 100.0; });
		tab.resize(1200, 700);
		RegDef def;
		def.addr = 0xD000;
		def.name = QStringLiteral("BAR");
		def.unit = QStringLiteral("V");
		tab.plotRegister(def, true);
		MathLines::Samples samples;
		for (int i = 0; i < 1000; i++) samples[regKey(def)] << QPointF(90.0 + i * 0.01, std::sin(i * 0.01));
		auto *view = tab.findChild<ChartView *>();
		if (!view) {
			check(false, "chart, A-B bar: the chart found");
			return;
		}
		view->setWindow(10);
		tab.show();
		(void) QTest::qWaitForWindowExposed(&tab);
		tab.frame(samples);
		QApplication::processEvents();
		(void) view->grab();
		view->setLive(false); /* the view where it was painted */
		const auto paint = [&](double a, double b) {
			view->setCursors(a, b);
			(void) view->grab();
		};
		const auto near = [](double x, double want) { return std::fabs(x - want) < 0.5; };

		paint(NAN, NAN);
		const bool noneWithout = view->spanBarText().isEmpty();
		paint(92.0, NAN);
		const bool noneWithA = view->spanBarText().isEmpty();

		/* B before A: the time between them, in the bar between the tags (each 9 px from its cursor, 2 px apart) */
		paint(96.4567, 92.0);
		const QRectF wide = view->spanBarRect(), wideText = view->spanBarTextRect();
		const bool inside = view->spanBarText() == durationText(4.4567) && view->spanBarText() == QLatin1String("4.457 s")
				&& wide.width() > wideText.width() && wide.contains(wideText) && near(wide.center().x(), wideText.center().x());
		const double pxPerSecond = (wide.width() + 2 * 11) / 4.4567;
		const auto x = [&](double t) { return wide.left() - 11 + (t - 92.0) * pxPerSecond; };
		if (!inside)
			std::printf("     (A 96.4567, B 92: \"%s\", bar %.1f..%.1f, text %.1f..%.1f)\n", qPrintable(view->spanBarText()),
					wide.left(), wide.right(), wideText.left(), wideText.right());
		check(noneWithout && noneWithA && inside, "chart, A-B bar: none without both cursors; the time between them "
				"|B - A| (durationText) centred in the bar between the tags, B before A too");

		/* 0.3 s: the span between the tags narrower than the text: no sliver of a bar, the text's tag after the right
		 * tag; 20 ms: the same */
		paint(94.0, 94.3);
		const QRectF shortBar = view->spanBarRect(), shortText = view->spanBarTextRect();
		const bool beside = view->spanBarText() == durationText(0.3) && shortBar.isEmpty()
				&& x(94.3) - x(94.0) - 22 >= 2 && near(shortText.left(), x(94.3) + 9 + 2)
				&& near(shortText.top(), wideText.top());
		paint(95.0, 95.02);
		const bool besideNoBar = view->spanBarText() == durationText(0.02) && view->spanBarRect().isEmpty()
				&& near(view->spanBarTextRect().left(), x(95.02) + 11);
		if (!beside || !besideNoBar)
			std::printf("     (0.3 s: bar %.1f..%.1f, text at %.1f (%.1f wanted); 20 ms: text at %.1f (%.1f wanted))\n",
					shortBar.left(), shortBar.right(), shortText.left(), x(94.3) + 11, view->spanBarTextRect().left(),
					x(95.02) + 11);
		check(beside && besideNoBar, "chart, A-B bar: a span too narrow for its text puts the text beside the right tag, "
				"and no bar between the tags (no sliver)");

		/* both off the view, one each side: the whole plot; one off: that end at the plot's edge; both off one side:
		 * no bar */
		paint(50.0, 150.0);
		const QRectF whole = view->spanBarRect();
		const bool across = view->spanBarText() == durationText(100) && near(whole.width(), 10 * pxPerSecond)
				&& whole.left() > 0 && whole.right() < view->width(); /* the window's 10 s: the plot from edge to edge */
		paint(50.0, 96.0);
		const QRectF leftOff = view->spanBarRect();
		paint(93.0, 150.0);
		const QRectF rightOff = view->spanBarRect();
		const bool edges = near(leftOff.left(), whole.left()) && near(leftOff.right(), x(96.0) - 11)
				&& near(rightOff.right(), whole.right()) && near(rightOff.left(), x(93.0) + 11)
				&& view->spanBarTextRect().top() == wideText.top();
		paint(50.0, 60.0);
		const bool offOneSide = view->spanBarText().isEmpty();
		view->clearCursors();
		(void) view->grab();
		const bool cleared = view->spanBarText().isEmpty();
		if (!across || !edges)
			std::printf("     (A-B across: %.1f..%.1f, %.1f px wanted; A off: %.1f..%.1f; B off: %.1f..%.1f)\n", whole.left(),
					whole.right(), 10 * pxPerSecond, leftOff.left(), leftOff.right(), rightOff.left(), rightOff.right());
		check(across && edges && offOneSide && cleared, "chart, A-B bar: a cursor off the view ends the bar at the plot's "
				"edge (both off, one each side: all of the plot); both off one side, or cleared: no bar");
		tab.hide();
	}

	/* a chart tab of its own, a clock that stands still (moved by the test), one line of `unit` named `name` */
	/* the full measurements, on the chart's threads, in and in the table (ChartTab::updateMeasures) */
	static void measured(ChartView *view) {
		(void) QTest::qWaitFor([view] { return !view->measuring(); }, 5000);
	}

	struct LoneChart {
		double now = 100;
		ChartTab tab{ [this] { return now; } };
		RegDef def;
		ChartView *view = nullptr;
		LoneChart(const QString &name, const QString &unit) {
			tab.resize(1200, 700);
			def.addr = 0xD000;
			def.name = name;
			def.unit = unit;
			tab.plotRegister(def, true);
			view = tab.findChild<ChartView *>();
		}
		int key() const { return int(regKey(def)); }
		QTableWidget *table() const { return tab.findChild<QTableWidget *>(QStringLiteral("measures")); }
		QString cell(int column) const {
			const QTableWidgetItem *item = table() ? table()->item(0, column) : nullptr;
			return item ? item->text() : QString();
		}
	};

	/* Std dev and peak to peak: two columns after RMS, from shifted sums (12 V with 1 mV of ripple reads 0.707 mV, not
	 * the rounding of 144 V^2); a right-click on the header shows or hides columns, kept (chart/measureColumns) */
	void chartReadouts() {
		QSettings().remove(QStringLiteral("chart/measureColumns"));
		LoneChart chart(QStringLiteral("RIPPLE"), QStringLiteral("V"));
		MathLines::Samples samples;
		for (int i = 0; i <= 2000; i++) /* 1 kHz, 50 Hz ripple of 1 mV on 12 V */
			samples[regKey(chart.def)] << QPointF(90.0 + i / 1000.0, 12.0 + 0.001 * std::sin(2 * M_PI * 50 * i / 1000.0));
		chart.tab.frame(samples);
		chart.view->setCursors(90.5, 91.5);
		const ChartView::Stats s = chart.view->stats(chart.key());
		const double want = 0.001 / std::sqrt(2.0);
		std::printf("     (12 V with 1 mV of ripple: std %.6g V, peak to peak %.6g V)\n", s.std, s.p2p);
		check(s.ok && std::fabs(s.std - want) < 0.01 * want && std::fabs(s.p2p - 0.002) < 1e-9,
				"chart, Measure: std dev of 12 V with 1 mV of ripple is 0.707 mV (shifted sums), peak to peak 2 mV");
		auto *measure = chart.tab.findChild<QPushButton *>(QStringLiteral("measure"));
		QTableWidget *table = chart.table();
		if (!measure || !table) {
			check(false, "chart, Measure: the button and the table found");
			return;
		}
		measure->setChecked(true);
		measured(chart.view);
		const bool columns = table->horizontalHeaderItem(ChartTab::ColRms)->text() == QLatin1String("RMS")
				&& table->horizontalHeaderItem(ChartTab::ColStd)->text() == QLatin1String("Std dev")
				&& table->horizontalHeaderItem(ChartTab::ColP2p)->text() == QLatin1String("Peak-peak")
				&& chart.cell(ChartTab::ColStd).startsWith(QLatin1String("0.0007071")) && chart.cell(ChartTab::ColStd).endsWith(QLatin1String(" V"))
				&& chart.cell(ChartTab::ColP2p) == QLatin1String("0.0020 V");
		if (!columns)
			std::printf("     (std \"%s\", peak to peak \"%s\")\n", qPrintable(chart.cell(ChartTab::ColStd)),
					qPrintable(chart.cell(ChartTab::ColP2p)));
		bool allShown = true;
		for (int c = 0; c < table->columnCount(); c++) allShown = allShown && !table->isColumnHidden(c);
		check(columns && allShown, "chart, Measure: Std dev and Peak-peak in the table after RMS; every column shown by "
				"default");

		/* the header's right-click: a tick per column but the line's */
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		QHeaderView *header = table->horizontalHeader();
		emit header->customContextMenuRequested(QPoint(20, 5));
		auto *menu = chart.tab.findChild<QMenu *>(QStringLiteral("measureColumns"));
		const bool popped = menu && QTest::qWaitFor([&] { return menu->isVisible(); }, 2000)
				&& menu->actions().size() == ChartTab::MEASURE_COLUMNS - 1;
		QAction *stdTick = nullptr;
		if (menu)
			for (QAction *action : menu->actions())
				if (action->text() == QLatin1String("Std dev")) stdTick = action;
		if (stdTick) stdTick->trigger();
		if (menu) menu->close();
		const bool hidden = stdTick && table->isColumnHidden(ChartTab::ColStd) && !table->isColumnHidden(ChartTab::ColP2p)
				&& QSettings().value(QStringLiteral("chart/measureColumns")).toStringList() == QStringList{ QStringLiteral("std") };
		bool kept = false;
		{
			ChartTab again([] { return 0.0; });
			auto *other = again.findChild<QTableWidget *>(QStringLiteral("measures"));
			kept = other && other->isColumnHidden(ChartTab::ColStd) && !other->isColumnHidden(ChartTab::ColRms);
		}
		if (stdTick) stdTick->trigger();
		const bool back = !table->isColumnHidden(ChartTab::ColStd)
				&& QSettings().value(QStringLiteral("chart/measureColumns")).toStringList().isEmpty();
		check(popped && hidden && kept && back, "chart, Measure: a right-click on the header lists the columns; Std dev "
				"unticked hides it, kept for the next start (chart/measureColumns), ticked shows it again");
		measure->setChecked(false);
		chart.tab.hide();
	}

	/* Totals since Clear: from every sample as it is appended, so the memory's trims lose nothing; a gap over 1 s is
	 * not bridged; a line taken off and put back keeps its total; Clear starts them again */
	void chartTotals() {
		LoneChart chart(QStringLiteral("AMPS"), QStringLiteral("A"));
		chart.view->setMemory(1);
		const auto feed = [&](double from, int count) {
			MathLines::Samples samples;
			for (int i = 0; i < count; i++) samples[regKey(chart.def)] << QPointF(from + i / 1000.0, 2.0);
			chart.tab.frame(samples);
		};
		feed(100.0, 10000); /* 10 s of 2 A at 1 kHz, the memory 1 s */
		const double first = chart.view->total(chart.key());
		const bool trimmed = chart.view->pointsKept(chart.key()) < 10000;
		feed(113.0, 1000); /* after a gap of 3 s */
		const double gapped = chart.view->total(chart.key());
		std::printf("     (2 A: %.6f A·s over 9.999 s, %.6f with a second after a gap of 3 s; %lld samples kept)\n", first,
				gapped, (long long) chart.view->pointsKept(chart.key()));
		check(trimmed && std::fabs(first - 19.998) < 1e-6 && std::fabs(gapped - (19.998 + 1.998)) < 1e-6
						&& chart.view->totalsSince() == 100.0,
				"chart, totals since Clear: every sample summed as it came (the memory trimmed meanwhile), a gap over 1 s "
				"not bridged");
		chart.tab.plotRegister(chart.def, false);
		chart.tab.plotRegister(chart.def, true);
		const bool keptOff = std::fabs(chart.view->total(chart.key()) - gapped) < 1e-12;

		auto *measure = chart.tab.findChild<QPushButton *>(QStringLiteral("measure"));
		auto *info = chart.tab.findChild<QLabel *>(QStringLiteral("measureInfo"));
		feed(115.0, 10);
		chart.now = 100.0 + 3600 + 12 * 60;
		if (measure) measure->setChecked(true);
		measured(chart.view);
		const QString total = chart.cell(ChartTab::ColTotal);
		const QString range = info ? info->text() : QString();
		const QString since = QDateTime::fromMSecsSinceEpoch(chart.view->epochMs() + 100000).toString(QStringLiteral("HH:mm:ss"));
		const bool shown = chart.table() && chart.table()->horizontalHeaderItem(ChartTab::ColTotal)->text() == QLatin1String("Since Clear")
				&& total.endsWith(QLatin1String(" Ah")) && total.startsWith(QLatin1String("0.0061"))
				&& range.endsWith(QStringLiteral(" · totals since %1 (1 h 12 min)").arg(since));
		if (!shown) std::printf("     (Since Clear \"%s\"; \"%s\")\n", qPrintable(total), qPrintable(range));
		check(keptOff && shown, "chart, totals: a line taken off and put back keeps its total; the Since Clear column in "
				"Ah, the measure line \"totals since <clock> (1 h 12 min)\"");
		if (measure) measure->setChecked(false);
		measured(chart.view); /* the samples that came meanwhile in */

		auto *clear = chart.tab.findChild<QPushButton *>(QStringLiteral("chartClear"));
		if (clear) clear->click();
		const bool cleared = std::isnan(chart.view->total(chart.key())) && std::isnan(chart.view->totalsSince());
		feed(200.0, 2);
		check(clear && cleared && chart.view->totalsSince() == 200.0 && std::fabs(chart.view->total(chart.key()) - 0.002) < 1e-12,
				"chart, totals: Clear starts them again, from the next sample");
	}

	/* Log Y: decades on the axis with SI prefixes and equal heights, values <= 0 on the bottom edge, Auto over the
	 * positive values at most 9 decades, Manual above 0 only; Log and Normalise exclude each other; kept */
	void chartLogScale() {
		const bool labels = chartLogLabel(1e-6) == QStringLiteral("1 µ") && chartLogLabel(1e-5) == QStringLiteral("10 µ")
				&& chartLogLabel(0.1) == QLatin1String("100 m") && chartLogLabel(1) == QLatin1String("1")
				&& chartLogLabel(1e4) == QLatin1String("10000") && chartLogLabel(1e5) == QLatin1String("100 k")
				&& chartLogLabel(2e6) == QLatin1String("2 M") && chartLogLabel(3e-9) == QLatin1String("3 n");
		check(labels, "chart, Log: decade labels 1 µ, 10 µ, 100 m, 1, 10000, 100 k, 2 M, 3 n");
		QSettings().remove(QStringLiteral("chart/yLog"));
		LoneChart chart(QStringLiteral("WIDE"), QStringLiteral("A"));
		MathLines::Samples samples;
		for (int i = 0; i <= 600; i++) /* 1 m .. 1 k, and a few at 0 and below */
			samples[regKey(chart.def)] << QPointF(90.0 + i / 100.0, i % 100 == 0 ? -1.0 : std::pow(10.0, -3 + 6.0 * (i % 97) / 96));
		chart.tab.frame(samples);
		chart.view->setWindow(10);
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		auto *mode = chart.tab.findChild<QComboBox *>(QStringLiteral("yMode"));
		auto *yMin = chart.tab.findChild<QLineEdit *>(QStringLiteral("yMin"));
		auto *yMax = chart.tab.findChild<QLineEdit *>(QStringLiteral("yMax"));
		if (!mode || !yMin || !yMax || mode->count() != 3) {
			check(false, "chart, Log: the Y range list holds Auto, Manual, Log");
			return;
		}
		emit mode->activated(ChartTab::YLog);
		(void) chart.view->grab();
		const QStringList shown = chart.view->valueLabels();
		const QRectF plot = chart.view->lastPlot();
		const double d1 = chart.view->yOfValue(1e-2) - chart.view->yOfValue(1e-1);
		const double d2 = chart.view->yOfValue(10) - chart.view->yOfValue(100);
		const bool decades = shown == QStringList{ QStringLiteral("1 m"), QStringLiteral("10 m"), QStringLiteral("100 m"),
				QStringLiteral("1"), QStringLiteral("10"), QStringLiteral("100"), QStringLiteral("1000") }
				&& d1 > 20 && std::fabs(d1 - d2) < 1e-6;
		const bool bottom = chart.view->yOfValue(0) == plot.bottom() && chart.view->yOfValue(-1) == plot.bottom();
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) /* a picture of the Log scale, for a look */
			chart.view->grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_log.png"));
		if (!decades) std::printf("     (Log labels: %s; decades %.2f and %.2f px)\n", qPrintable(shown.join(QStringLiteral(", "))), d1, d2);
		check(chart.view->yLog() && mode->currentIndex() == ChartTab::YLog && decades && bottom
						&& QSettings().value(QStringLiteral("chart/yLog")).toBool(),
				"chart, Log: a line at each decade 1 m .. 1000 with its label, every decade as tall, values <= 0 on the "
				"bottom edge; saved (chart/yLog)");

		/* a line down to 1e-15: Auto keeps 9 decades under the top (with its margins) */
		MathLines::Samples tiny;
		tiny[regKey(chart.def)] << QPointF(99.5, 1e-15);
		chart.tab.frame(tiny);
		(void) chart.view->grab();
		const double decadesShown = std::log10(chart.view->yHi() / chart.view->yLo());
		check(chart.view->yAuto() && decadesShown <= 9 * (1 + 2 * 0.08) + 1e-6 && chart.view->yHi() > 1000,
				"chart, Log: Auto spans the positive values shown, at most 9 decades (with its margins)");

		/* Manual in Log: above 0 only */
		yMin->setText(QStringLiteral("-1"));
		yMax->setText(QStringLiteral("100"));
		emit yMin->editingFinished();
		const bool refused = chart.view->yAuto() && chart.view->yLog();
		yMin->setText(QStringLiteral("0.01"));
		yMax->setText(QStringLiteral("100"));
		emit yMin->editingFinished();
		const bool taken = !chart.view->yAuto() && chart.view->yLog() && chart.view->yLo() == 0.01 && chart.view->yHi() == 100
				&& mode->currentIndex() == ChartTab::YLog;
		check(refused && taken, "chart, Log: a typed min of 0 or less is refused, 0.01 .. 100 is taken (Log, manual)");

		/* Normalise turns Log off; Log turns Normalise off */
		auto *normalise = chart.tab.findChild<QAction *>(QStringLiteral("chartNormalise"));
		if (normalise) normalise->setChecked(true);
		const bool logOff = normalise && !chart.view->yLog() && mode->currentIndex() != ChartTab::YLog && mode->isEnabled();
		emit mode->activated(ChartTab::YLog);
		const bool normaliseOff = normalise && !normalise->isChecked() && chart.view->yLog();
		check(logOff && normaliseOff, "chart, Log and Normalise exclude each other: either one chosen turns the other off");
		emit mode->activated(ChartTab::YAuto);
		check(!chart.view->yLog() && chart.view->yAuto() && !QSettings().value(QStringLiteral("chart/yLog")).toBool(),
				"chart, Log: Auto again is linear");
		chart.tab.hide();
	}

	/* The info line: when it does not fit, whole parts go (the paint time, then "plotted", then the delay), never
	 * letters cut; all of it in the tooltip */
	/* the chart filled up to the cap: the registers free (but keepFree) plotted, up to 60 lines (their rows in
	 * `plotted`), then math lines, a register's bits taken as fields ("REG.capN"; a math line past the 64th kept is
	 * not drawn, MathLines::MAX_DRAWN, so they alone may not reach it); how many fields added */
	static BitField capField(int i) {
		BitField f;
		f.name = QStringLiteral("cap%1").arg(i);
		f.lsb = i % 8;
		return f;
	}
	int fillWithFields(ChartTab *chartTab, const RegDef &def, QVector<int> &plotted, int keepFree = -1) {
		for (int r = 0; r < model_->rows().size() && chartTab->lineCount() < 60; r++) {
			const RegisterModel::Row &row = model_->rows()[r];
			if (r == keepFree || row.plot || !row.def.canPlot() || row.unavailable) continue;
			if (model_->setPlot(r, true)) plotted << r;
		}
		int fields = 0;
		while (chartTab->lineCount() < RegisterModel::MAX_PLOTTED && fields < 80) chartTab->plotField(def, capField(fields++));
		QApplication::processEvents();
		return fields;
	}
	/* a math line's action in the Math button's menu (Shown, Remove), by the line's name */
	static QAction *mathAction(QPushButton *math, const QString &line, const QString &text) {
		for (QAction *action : math->menu()->actions()) {
			if (!action->menu() || !action->text().startsWith(line + QStringLiteral(" = "))) continue;
			for (QAction *sub : action->menu()->actions())
				if (sub->text() == text) return sub;
		}
		return nullptr;
	}
	void removeFields(QPushButton *math, const RegDef &def, int fields, const QVector<int> &plotted) {
		for (int r : plotted) model_->setPlot(r, false);
		for (int i = 0; i <= fields; i++)
			if (QAction *remove = mathAction(math, QStringLiteral("%1.cap%2").arg(def.name).arg(i), QStringLiteral("Remove")))
				remove->trigger();
		QApplication::processEvents();
	}

	/* One cap of 64 for every line (O-5): registers, math and fast lines together (a fast channel's tick: in
	 * fastStreams, where a map has a stream). The chart filled with math lines: a register's Plot, a 65th math line (a
	 * field, New math line..., Shown) are each refused with the same words in the status bar; the info line counts
	 * them together ("64/64 plotted · N math"); a line off makes room for one of any kind */
	void chartOneCap() {
		auto *chartTab = window_.findChild<ChartTab *>();
		auto *math = chartTab ? chartTab->findChild<QPushButton *>(QStringLiteral("math")) : nullptr;
		int freeRow = -1; /* a register to tick, not on the chart */
		for (int r = 0; r < model_->rows().size() && freeRow < 0; r++)
			if (model_->rows()[r].def.canPlot() && !model_->rows()[r].plot && !model_->rows()[r].unavailable) freeRow = r;
		if (!chartTab || !math || freeRow < 0 || regs_.volts.name.isEmpty()) {
			std::printf("     (chart tab %d, Math button %d, a free register %d, %s)\n", chartTab != nullptr, math != nullptr,
					freeRow, qPrintable(regs_.volts.name));
			check(false, "chart, one cap of 64 lines: the chart tab, the Math button and a free register");
			return;
		}
		const int before = chartTab->lineCount(), limitBefore = model_->plotLimit();
		const QString cap = ChartTab::lineCapText();
		const auto said = [&] { return window_.statusBar()->currentMessage(); };
		QVector<int> plotted;
		const int fields = fillWithFields(chartTab, regs_.volts, plotted, freeRow);
		const bool full = chartTab->lineCount() == RegisterModel::MAX_PLOTTED;
		const QString info = chartTab->infoText();
		const bool counted = info.startsWith(QStringLiteral("64/64 plotted · %1 math").arg(chartTab->mathLinesShown()))
				&& chartTab->mathLinesShown() > 0
				&& model_->plotLimit() == model_->plottedCount();
		/* a register's Plot */
		window_.statusBar()->clearMessage();
		const bool registerRefused = !model_->setPlot(freeRow, true) && !model_->rows()[freeRow].plot && said() == cap;
		/* a 65th math line: a field, New math line... (refused before its dialog) */
		window_.statusBar()->clearMessage();
		chartTab->plotField(regs_.volts, capField(fields));
		const bool fieldRefused = chartTab->lineCount() == RegisterModel::MAX_PLOTTED && said() == cap
				&& lineKey(chartTab->view(), QStringLiteral("ƒ %1.cap%2").arg(regs_.volts.name).arg(fields)) < 0;
		window_.statusBar()->clearMessage();
		bool asked = false;
		QTimer::singleShot(300, [&] {
			if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
				asked = true;
				dialog->reject();
			}
		});
		QAction *newLine = nullptr;
		for (QAction *action : math->menu()->actions())
			if (action->text().startsWith(QStringLiteral("New math line"))) newLine = action;
		if (newLine) newLine->trigger();
		QTest::qWait(400);
		const bool newRefused = newLine && !asked && said() == cap && chartTab->lineCount() == RegisterModel::MAX_PLOTTED;
		/* a math line's Shown: one off makes room (a register takes it), on again it is refused */
		const QString first = QStringLiteral("%1.cap0").arg(regs_.volts.name);
		QAction *shown = mathAction(math, first, QStringLiteral("Shown"));
		if (shown) shown->trigger(); /* off */
		QApplication::processEvents();
		const bool roomMade = chartTab->lineCount() == RegisterModel::MAX_PLOTTED - 1 && model_->setPlot(freeRow, true)
				&& chartTab->lineCount() == RegisterModel::MAX_PLOTTED;
		window_.statusBar()->clearMessage();
		shown = mathAction(math, first, QStringLiteral("Shown"));
		if (shown) shown->trigger(); /* on: refused, the tick taken back */
		QApplication::processEvents();
		shown = mathAction(math, first, QStringLiteral("Shown"));
		const bool shownRefused = shown && !shown->isChecked() && said() == cap
				&& chartTab->lineCount() == RegisterModel::MAX_PLOTTED;
		model_->setPlot(freeRow, false);
		/* the cap's math lines removed: the chart and the registers' limit as before */
		removeFields(math, regs_.volts, fields, plotted);
		window_.statusBar()->clearMessage();
		const bool back = chartTab->lineCount() == before && model_->plotLimit() == limitBefore;
		if (!(full && counted && registerRefused && fieldRefused && newRefused && roomMade && shownRefused && back))
			std::printf("     (one cap: full %d (%d fields), info \"%s\" %d (limit %d, plotted %d), register %d, field %d, new %d "
					"(asked %d), room %d, shown %d, back %d (%d of %d lines, limit %d of %d))\n", full, fields, qPrintable(info),
					counted, model_->plotLimit(), model_->plottedCount(), registerRefused, fieldRefused, newRefused, asked,
					roomMade, shownRefused, back, chartTab->lineCount(), before, model_->plotLimit(), limitBefore);
		check(full && counted && registerRefused && fieldRefused && newRefused,
				"chart, one cap of 64 lines for every kind: with the chart full of math lines a register's Plot, a field and "
				"New math line are refused with the same words; the info line counts every line (\"64/64 plotted · N math\")");
		check(roomMade && shownRefused && back,
				"chart, one cap of 64 lines: a math line hidden makes room for a register; shown again past the cap it is "
				"refused (its tick taken back); the lines removed, the registers' limit as before");
	}

	/* The memory strip's box at a short window (O-6): 10 ms of a minute is a sliver no mouse can take, so a handle
	 * 12 px wide is drawn on the view; the mouse over it a pointing hand, the handle lit, a tooltip; taken and dragged
	 * it moves the view by as much as the mouse (no jump when taken); a click elsewhere on the strip still takes the
	 * view there; the wheel over the strip moves it a window earlier or later */
	static void memoryStripHandle() {
		QWidget host;
		host.resize(1100, 480);
		auto *view = new ChartView(&host);
		view->setGeometry(9, 5, 1080, 470);
		double now = 100;
		view->setClock([&now] { return now; }, 0);
		view->setMemory(60);
		view->addSeries(1, QStringLiteral("R"), QStringLiteral("V"), Qt::blue);
		for (int i = 0; i <= 60000; i++) view->append(1, 40 + i * 0.001, std::sin(i * 0.01));
		view->frame();
		view->showSpan(70, 70.01);
		host.show();
		(void) QTest::qWaitForWindowExposed(&host);
		(void) host.grab();
		double t0, t1;
		const auto end = [&] {
			(void) host.grab();
			view->viewSpan(t0, t1);
			return t1;
		};
		const QRectF handle = view->memoryHandleRect();
		/* the strip spans the plot: its width from the time under two x (the view a minute ago on 60 s of memory) */
		const double plotW = view->window() * 100 / (view->timeAt(100) - view->timeAt(0));
		const double secondsPerPx = view->memory() / plotW;
		const double viewX = handle.center().x();
		const bool wide = std::fabs(handle.width() - ChartView::MEMORY_HANDLE_W) < 0.01;
		/* the mouse over it */
		const auto moveTo = [view](QPointF at, Qt::MouseButtons buttons) {
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, buttons, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
		};
		moveTo(handle.center(), Qt::NoButton);
		const QString tip = view->toolTipAt(handle.center());
		const bool hover = view->memoryHandleHovered() && view->cursor().shape() == Qt::PointingHandCursor
				&& tip.startsWith(QStringLiteral("The view: drag it along the memory"));
		/* taken (no jump) and dragged 100 px right: 100 px of the strip later */
		const double before = end();
		const QPoint at = handle.center().toPoint();
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, at);
		const double taken = end();
		moveTo(QPointF(at.x() + 100, at.y()), Qt::LeftButton);
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(at.x() + 100, at.y()));
		const double dragged = end();
		const bool drags = std::fabs(taken - before) < secondsPerPx && std::fabs(dragged - before - 100 * secondsPerPx) < 2 * secondsPerPx;
		/* a click 300 px left of it: the view there */
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, QPoint(at.x() - 200, at.y()));
		const double jumped = end();
		const bool jumps = std::fabs(jumped - (dragged - 300 * secondsPerPx)) < 2 * secondsPerPx;
		/* the wheel over the strip: down a window later, up a window earlier */
		const auto wheel = [view](QPointF where, int notches) {
			QWheelEvent e(where, view->mapToGlobal(where), QPoint(), QPoint(0, 120 * notches), Qt::NoButton, Qt::NoModifier,
					Qt::NoScrollPhase, false);
			QApplication::sendEvent(view, &e);
		};
		const QPointF onStrip(view->memoryHandleRect().center().x() - 150, view->memoryHandleRect().center().y());
		wheel(onStrip, -1);
		const double later = end();
		wheel(onStrip, 2);
		const double earlier = end();
		const bool wheels = std::fabs(later - jumped - 0.01) < 1e-6 && std::fabs(earlier - later + 0.02) < 1e-6
				&& std::fabs(view->window() - 0.01) < 1e-9 && !view->live();
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* for a look: the handle lit at 10 ms */
			moveTo(view->memoryHandleRect().center(), Qt::NoButton);
			host.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_strip_handle.png"));
		}
		if (!wide || !hover || !drags || !jumps || !wheels)
			std::printf("     (the handle %g px at x %g (%d), hover %d tip \"%s\"; %g s a px: taken %+g, dragged %+g; jumped %+g; "
					"wheel %+g, %+g; window %.17g, live %d)\n", handle.width(), viewX, int(wide), int(hover), qPrintable(tip),
					secondsPerPx, taken - before, dragged - before, jumped - dragged, later - jumped, earlier - later,
					view->window(), int(view->live()));
		check(wide && hover && drags && jumps && wheels,
				"chart, the memory strip at a 10 ms window: a handle 12 px wide on the view (the mouse over it a pointing hand, "
				"lit, a tooltip); dragged it moves the view by as much as the mouse, no jump when taken; a click elsewhere "
				"takes the view there; the wheel over the strip a window later or earlier");
	}

	/* Long memory for fast lines (P6, SCOPE_PLAN.md section 10): older records kept as their summaries only. The store:
	 * dropRecords lets the oldest pieces' records go and keeps their outline (a min and a max record per 256 and per
	 * 4096 records), so the min and max over old data are those of the records before they went (a range widened to
	 * whole 256s), their values and sums read NaN, their times stay. The chart: past its share the records of the
	 * oldest piece go, a piece at a time, the summaries kept (nothing dropped from the front, not "memory full"); the
	 * arithmetic of 100 min of two i16 channels at 1 MS/s in 512 MB; a RAM cut and the free memory's limit with the
	 * tiers, no append, frame or paint over 20 ms */
	static void chartFastTiers() {
		StreamDef def;
		def.name = QStringLiteral("ADC");
		for (const char *name : { "I", "V" }) {
			StreamChannel channel;
			channel.name = QString::fromLatin1(name);
			channel.type = RegType::I16;
			def.channels << channel;
		}
		def.channels[0].scale = 0.01;
		def.channels[1].scale = -0.5; /* a falling scale: the lowest raw is the highest value */
		def.channels[1].offset = 3;
		constexpr qsizetype PIECE = fast::Store::PIECE;
		const auto fill = [](QByteArray &records, quint64 first, qsizetype n) {
			records.resize(n * 4);
			auto *raw = reinterpret_cast<qint16 *>(records.data());
			for (qsizetype i = 0; i < n; i++) {
				const quint64 a = first + quint64(i);
				raw[2 * i] = qint16(int((a * 37) % 2000) - 1000 + (a % 99991 == 5 ? 20000 : 0));
				raw[2 * i + 1] = qint16(int((a * 7919) % 3001) - 1500);
			}
		};
		/* the store alone: the min and max before and after the records went */
		{
			fast::Store store(def);
			const qsizetype n = 6 * PIECE + 1000;
			QByteArray records;
			fill(records, 0, n);
			store.append(0, n, records.constData(), true, 0);
			store.mark(0, 10.0, 1e-6);
			const QVector<QPair<qsizetype, qsizetype>> ranges{ { 0, 256 }, { 768, 10240 }, { 4096, 20480 }, { 0, 2 * PIECE },
				{ 100, 300 }, { PIECE - 5000, PIECE + 77 }, { 7, 3 * PIECE - 9 }, { 2 * PIECE + 5, 4 * PIECE + 11 },
				{ 3 * PIECE - 300, 3 * PIECE + 300 } };
			const qsizetype boundary = 3 * PIECE;
			/* what the outline gives: whole 256s before the boundary, the records after it */
			const auto widened = [&](qsizetype i0, qsizetype i1, qsizetype &w0, qsizetype &w1) {
				w0 = i0 < boundary ? i0 - i0 % 256 : i0;
				w1 = i1 <= boundary ? std::min(boundary, (i1 + 255) / 256 * 256) : i1;
			};
			QVector<double> before;
			for (const auto &r : ranges)
				for (int c = 0; c < 2; c++) {
					qsizetype w0, w1;
					widened(r.first, r.second, w0, w1);
					double lo, hi;
					store.minMax(c, w0, w1, lo, hi);
					before << lo << hi;
				}
			const double t = store.timeAt(boundary - 1), kept = store.value(1, boundary);
			const qint64 bytes = store.bytes();
			store.dropRecords(boundary + 100);
			bool same = store.recordsFrom() == boundary && store.size() == n && store.dropped() == 0;
			int k = 0;
			for (const auto &r : ranges)
				for (int c = 0; c < 2; c++) {
					double lo, hi;
					store.minMax(c, r.first, r.second, lo, hi);
					if (lo != before[k] || hi != before[k + 1]) {
						std::printf("     (range %lld..%lld channel %d: %g..%g after, %g..%g before)\n", (long long) r.first,
								(long long) r.second, c, lo, hi, before[k], before[k + 1]);
						same = false;
					}
					k += 2;
				}
			double sum, squares;
			store.sums(0, 10, 500, sum, squares);
			const bool reads = std::isnan(store.value(0, boundary - 1)) && store.value(1, boundary) == kept
					&& store.timeAt(boundary - 1) == t && std::isnan(sum) && std::isnan(squares);
			const qint64 freed = bytes - store.bytes();
			std::printf("     (the store: %lld records, the first %lld as summaries; %lld KB let go, %.4f bytes a record as "
					"summaries, %.4f whole)\n", (long long) n, (long long) store.recordsFrom(), (long long) (freed >> 10),
					store.bytesPerSummary(), store.bytesPerRecord());
			check(same && reads && freed >= 3 * PIECE * 4 && store.bytesPerSummary() <= 4.0 / 128 * 1.07,
					"chart, long memory: the store lets the oldest pieces' records go and keeps their summaries (1/128 of "
					"them): the min and max over old data are those of the records before (whole 256s), a falling scale "
					"too; their values and sums read nothing, their times stay");
		}
		/* the arithmetic: 100 min of two i16 channels at 1 MS/s in 512 MB */
		{
			const fast::Store store(def);
			const ChartView::Tiers t = ChartView::tiers(1e6, 6000, 512.0 * 1024 * 1024, store.bytesPerRecord(),
					store.bytesPerSummary());
			std::printf("     (100 min at 2 ch x 1 MS/s in 512 MB: summaries %.1f MB, keeps %.0f min, samples for the newest "
					"%.1f s)\n", t.summaryBytes / 1048576.0, t.kept / 60, t.samples);
			const ChartView::Tiers small = ChartView::tiers(1e6, 60, 512.0 * 1024 * 1024, store.bytesPerRecord(),
					store.bytesPerSummary());
			check(std::fabs(t.summaryBytes / 1048576.0 - 190) < 2 && t.kept == 6000 && t.samples > 75 && t.samples < 85
							&& small.samples == 60 && small.summaryBytes == 0,
					"chart, long memory: the arithmetic: 100 min of 2 ch x 1 MS/s in 512 MB keeps all 100 min, 190 MB of "
					"summaries and the newest 79 s whole; a minute fits whole");
		}
		/* the chart: a fast line past its share keeps the summaries, the oldest piece's records go first */
		{
			QWidget host;
			host.resize(1100, 480);
			auto *view = new ChartView(&host);
			view->setGeometry(9, 5, 1080, 470);
			double now = 100;
			view->setClock([&now] { return now; }, 0);
			view->setMemory(3600);
			view->setRamBudget(512);
			view->setFastSummaries(true);
			view->setFastStream(0, def);
			view->addSeries(ChartView::fastKey(0, 0), QStringLiteral("ADC.I"), QStringLiteral("A"), Qt::red);
			QByteArray records;
			quint64 first = 0;
			const auto block = [&] {
				fill(records, first, PIECE);
				view->appendFast(0, first, PIECE, records, first == 0, 0);
				first += PIECE;
				now = 100.0 + first * 1e-6;
				view->markFast(0, first, now, 1e-6);
			};
			const fast::Store *store = view->fastStore(0);
			QElapsedTimer filling;
			filling.start();
			while (store->recordsFrom() == 0 && filling.elapsed() < 30000) block();
			const double fillS = filling.elapsed() / 1000.0;
			const qint64 share = 512ll * 1024 * 1024;
			const qsizetype from = store->recordsFrom();
			block();
			const qsizetype went = store->recordsFrom() - from; /* a piece, two when the summaries' growth took one's room */
			const bool ring = (went == PIECE || went == 2 * PIECE) && store->dropped() == 0
					&& store->size() == qsizetype(first) && !view->memoryFull() && store->bytes() <= share
					&& store->bytes() >= share * 0.95 && std::isnan(store->value(0, from))
					&& std::isfinite(store->value(0, store->recordsFrom()));
			/* the span whole now: what the share holds after the summaries so far (it shrinks as they grow) */
			const double summary = store->bytesPerSummary();
			const double wholeNow = (double(share) - double(store->size()) * summary) / (store->bytesPerRecord() - summary) / 1e6;
			double kept = 0, samples = 0, since = 0;
			const bool tiered = view->fastTiers(kept, samples) && view->summariesBefore(since)
					&& since == store->timeAt(store->recordsFrom());
			std::printf("     (the chart: %lld records in %.1f s, the first %lld as summaries, %lld MB of 512; it keeps %.0f s, "
					"samples for the newest %.1f s (%.1f s now))\n", (long long) store->size(), fillS,
					(long long) store->recordsFrom(), (long long) (store->bytes() >> 20), kept, samples,
					now - since);
			check(ring && tiered && kept == 3600 && std::fabs(now - since - wholeNow) < 0.05 * wholeNow + 0.2
							&& samples > 0 && samples < now - since,
					"chart, long memory: a fast line past its share of the RAM keeps every record's summaries and the "
					"newest whole: the records of the oldest piece go, a piece at a time (none dropped, not \"memory full\"); "
					"the span kept whole is what the share holds after the summaries, less once the Memory is full");

			/* the RAM cut to 256 MB, then the free memory leaving 192 MB: at the next block, without a freeze */
			(void) host.grab();
			(void) view->takePerfStats();
			double appendMax = 0, frameMax = 0;
			bool down = true;
			for (int b = 0; b < 60; b++) {
				if (b == 0) view->setRamBudget(256);
				if (b == 30) view->setRamLimit(192);
				QElapsedTimer one;
				one.start();
				block();
				appendMax = std::max(appendMax, one.nsecsElapsed() / 1e6);
				if (b == 0 || b == 30) down = down && store->bytes() <= qint64(view->ramInUse()) * 1024 * 1024;
				one.restart();
				view->frame();
				frameMax = std::max(frameMax, one.nsecsElapsed() / 1e6);
				(void) host.grab();
			}
			const ChartView::PerfStats perf = view->takePerfStats();
			std::printf("     (the tiers cut: %lld MB kept of %d, the first %lld of %lld as summaries; the longest append %.1f "
					"ms, frame() %.1f ms, paint %.1f ms of %d)\n", (long long) (store->bytes() >> 20), view->ramInUse(),
					(long long) store->recordsFrom(), (long long) store->size(), appendMax, frameMax, perf.paintMax,
					perf.frames);
			check(down && store->dropped() == 0 && appendMax < 20 && frameMax < 20 && perf.paintMax < 20 && perf.frames >= 50,
					"chart, long memory: the RAM cut (512 to 256 MB) and the free memory's limit (192 MB) with the tiers: "
					"the records down to the new share at the next block, the summaries kept, no append, frame or paint "
					"over 20 ms");
			view->setRamLimit(0);
		}
	}

	/* The RAM budget cut with a filled fast store (O-7): a fast line's store of two i16 channels filled to its share of
	 * 2 GB (as many as this machine fills in 20 s), then RAM set to 256 MB: at the next block the store is down to its
	 * new share, and no block's append (its trim with it) and no paint takes over 20 ms. Letting gigabytes go took the
	 * window's thread 2 s; now it hands them to a thread of its own */
	static void chartRamCut() {
		StreamDef def;
		def.name = QStringLiteral("ADC");
		for (const char *name : { "I", "V" }) {
			StreamChannel channel;
			channel.name = QString::fromLatin1(name);
			channel.type = RegType::I16;
			def.channels << channel;
		}
		QWidget host;
		host.resize(1100, 480);
		auto *view = new ChartView(&host);
		view->setGeometry(9, 5, 1080, 470);
		double now = 100;
		view->setClock([&now] { return now; }, 0);
		view->setMemory(3600);
		view->setRamBudget(2048);
		view->setFastStream(0, def);
		view->addSeries(ChartView::fastKey(0, 0), QStringLiteral("ADC.I"), QStringLiteral("A"), Qt::red);
		constexpr qsizetype BLOCK = 65536;
		QByteArray records(BLOCK * 4, Qt::Uninitialized);
		for (qsizetype i = 0; i < BLOCK * 2; i++) reinterpret_cast<qint16 *>(records.data())[i] = qint16((i * 37) % 2000 - 1000);
		quint64 first = 0;
		const auto block = [&] {
			view->appendFast(0, first, BLOCK, records, first == 0, 0);
			first += BLOCK;
			now = 100.0 + first * 1e-6;
			view->markFast(0, first, now, 1e-6);
		};
		QElapsedTimer filling;
		filling.start();
		const fast::Store *store = view->fastStore(0);
		while (!view->memoryFull() && filling.elapsed() < 20000) block();
		const double fillS = filling.elapsed() / 1000.0;
		const qint64 filled = store->bytes();
		(void) host.grab();
		(void) view->takePerfStats();
		view->setRamBudget(256);
		/* as the window does: blocks, then a frame (the slice of the memory let go), then its paint */
		double appendMax = 0, frameMax = 0;
		const qint64 share = 256ll * 1024 * 1024;
		bool down = false;
		for (int b = 0; b < 60; b++) {
			QElapsedTimer one;
			one.start();
			block();
			appendMax = std::max(appendMax, one.nsecsElapsed() / 1e6);
			if (b == 0) down = store->bytes() <= share && store->bytes() >= share / 8 * 7 - BLOCK * 4 * 2;
			one.restart();
			view->frame();
			frameMax = std::max(frameMax, one.nsecsElapsed() / 1e6);
			(void) host.grab();
		}
		const ChartView::PerfStats perf = view->takePerfStats();
		std::printf("     (the RAM cut: %lld MB filled in %.1f s, cut to 256 MB: %lld MB kept; the longest append %.1f ms, "
				"frame() %.1f ms, paint %.1f ms of %d)\n", (long long) (filled >> 20), fillS, (long long) (store->bytes() >> 20),
				appendMax, frameMax, perf.paintMax, perf.frames);
		check(filled >= 512ll * 1024 * 1024 && down && appendMax < 20 && frameMax < 20 && perf.paintMax < 20
						&& perf.frames >= 50,
				"chart, the RAM budget cut with a filled fast store (2 GB to 256 MB): the store at its new share from the next "
				"block on; no append, frame or paint over 20 ms (the memory let go a slice a frame)");
	}

	/* The RAM against the free memory (O-13): the budget is a cap, not a reservation. With less free than it, the chart
	 * keeps within what it holds and the free memory less a reserve (a free memory given, which comes back as the chart
	 * lets go, as a computer's does): a fast line filled to its share of 512 MB, then 256 MB left to it by the free
	 * memory: the store down to that at the next block, no append, frame or paint over 20 ms (P7's cut); the note "only
	 * ... free: keeps about ..." in the warn colour, the RAM box's tooltip and the memory strip's say why. The free
	 * memory back: the RAM set again, nothing more trimmed */
	void chartRamFree() {
		/* the effective budget: what is held and free less the reserve, within the floor and the RAM set */
		const qint64 reserve = ChartTab::ramReserveMB();
		const bool formula = ChartTab::effectiveRamMB(16384, 1000, 2048 + reserve) == 3048
				&& ChartTab::effectiveRamMB(2048, 1000, 8192 + reserve) == 2048
				&& ChartTab::effectiveRamMB(2048, 0, 10) == ChartTab::RAM_FLOOR_MB
				&& ChartTab::effectiveRamMB(2048, 0, -1) == 2048 && reserve >= 1024;
		bool over = false;
		/* 3 GB needed for 2 min, 1 GB left by the free memory: about 40 s */
		const QString only = ChartTab::ramNeedText(qint64(3) * 1024 * 1024 * 1024, 2048, 120, over, 1024, 2150);
		std::printf("     (the reserve %lld MB; \"%s\")\n", (long long) reserve, qPrintable(only));
		check(formula && over && only == QLatin1String("only 2.1 GB free: keeps about 40 s"),
				"chart, the RAM against the free memory: what the chart holds and the free memory less a reserve (1 GB, "
				"a tenth of the memory when more), 64 MB at least, the RAM set at most; the note \"only 2.1 GB free: "
				"keeps about 40 s\"");

		StreamDef def;
		def.name = QStringLiteral("ADC");
		for (const char *name : { "I", "V" }) {
			StreamChannel channel;
			channel.name = QString::fromLatin1(name);
			channel.type = RegType::I16;
			def.channels << channel;
		}
		double now = 100;
		ChartTab tab{ [&now] { return now; } };
		tab.resize(1100, 560); /* the chart about as large as the RAM cut's (a paint's time follows its pixels) */
		tab.show();
		(void) QTest::qWaitForWindowExposed(&tab);
		tab.setShown(true);
		ChartView *view = tab.view();
		view->setMemory(3600);
		view->setRamBudget(512);
		view->setFastStream(0, def);
		view->addSeries(ChartView::fastKey(0, 0), QStringLiteral("ADC.I"), QStringLiteral("A"), Qt::red);
		constexpr qsizetype BLOCK = 65536;
		QByteArray records(BLOCK * 4, Qt::Uninitialized);
		for (qsizetype i = 0; i < BLOCK * 2; i++) reinterpret_cast<qint16 *>(records.data())[i] = qint16((i * 37) % 2000 - 1000);
		quint64 first = 0;
		const auto block = [&] {
			view->appendFast(0, first, BLOCK, records, first == 0, 0);
			first += BLOCK;
			now = 100.0 + first * 1e-6;
			view->markFast(0, first, now, 1e-6);
		};
		QElapsedTimer filling;
		filling.start();
		const fast::Store *store = view->fastStore(0);
		while (!view->memoryFull() && filling.elapsed() < 20000) block();
		const qint64 filled = store->bytes();
		(void) view->grab();
		(void) view->takePerfStats();

		/* 256 MB left to the chart: the free memory given so that what it holds and the free less the reserve is that */
		constexpr qint64 MiB = 1024 * 1024;
		constexpr int LEFT = 256;
		const qint64 heldMB = (view->bytesHeld() + view->bytesReleasing()) / MiB;
		tab.setTestFreeMemory(LEFT + reserve - heldMB);
		const bool limited = std::abs(view->ramInUse() - LEFT) <= 2 && view->ramLimit() > 0 && view->ramBudget() == 512;
		double appendMax = 0, frameMax = 0;
		bool down = false;
		for (int b = 0; b < 60; b++) {
			QElapsedTimer one;
			one.start();
			block();
			appendMax = std::max(appendMax, one.nsecsElapsed() / 1e6);
			if (b == 0) down = store->bytes() <= qint64(view->ramInUse()) * MiB;
			one.restart();
			view->frame();
			frameMax = std::max(frameMax, one.nsecsElapsed() / 1e6);
			(void) view->grab();
			if (b % 20 == 19) tab.watchFreeMemory(); /* the readings go on: what was let go came back as free */
		}
		const ChartView::PerfStats perf = view->takePerfStats();
		const bool stillLimited = std::abs(view->ramInUse() - LEFT) <= 4;
		tab.refreshStatus();
		auto *note = tab.findChild<QLabel *>(QStringLiteral("ramNeed"));
		auto *ram = tab.findChild<QComboBox *>(QStringLiteral("chartRam"));
		const QString noteText = note ? note->text() : QString();
		const bool warned = note && noteText.startsWith(QLatin1String("only "))
				&& noteText.contains(QLatin1String(" free: keeps about ")) && note->property("warn").toBool()
				&& note->palette().color(note->foregroundRole()) == Theme::colors().warn;
		const QString tip = ram ? ram->toolTip() : QString();
		const bool ramTip = tip.contains(QLatin1String("Free now: ")) && tip.contains(QLatin1String("of the 512 MB set"));
		const bool stripTip = view->memoryStripTip().contains(QLatin1String("The free memory limits the budget now"));
		std::printf("     (the free memory, a chart of %d x %d: %lld MB filled, %d MB left to it: %lld MB kept; the longest "
				"append %.1f ms, frame() %.1f ms, paint %.1f ms of %d; \"%s\")\n", view->width(), view->height(),
				(long long) (filled >> 20), view->ramInUse(),
				(long long) (store->bytes() >> 20), appendMax, frameMax, perf.paintMax, perf.frames, qPrintable(noteText));
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* the note in its warn state, in both themes */
			if (ram) ram->setEditText(QStringLiteral("512 MB")); /* the budget set on the view, shown as the box would */
			for (const bool dark : { false, true }) {
				Theme::apply(*qApp, dark);
				tab.refreshStatus();
				tab.grab(QRect(0, 0, tab.width(), 120)).save(qEnvironmentVariable("EVRE_TEST_SHOT")
						+ (dark ? QStringLiteral("_ram_free_dark.png") : QStringLiteral("_ram_free_light.png")));
			}
		}
		check(filled >= 256 * MiB && limited && down && stillLimited && appendMax < 20 && frameMax < 20 && perf.paintMax < 20
						&& perf.frames >= 50 && warned && ramTip && stripTip,
				"chart, less memory free than the RAM set: the chart keeps within what it holds and the free less the "
				"reserve (512 MB set, 256 MB left), trimmed at the next block with no append, frame or paint over 20 ms; "
				"the note \"only ... free: keeps about ...\" in the warn colour, the RAM box's and the memory strip's "
				"tooltips say so");

		/* the free memory back: the RAM set again, and nothing more let go while the line grows to it */
		tab.setTestFreeMemory(16384 + reserve);
		const qint64 dropped = store->dropped();
		for (int b = 0; b < 20; b++) {
			block();
			view->frame();
		}
		tab.refreshStatus();
		const QString back = note ? note->text() : QString();
		const QString backTip = ram ? ram->toolTip() : QString();
		check(view->ramLimit() == 0 && view->ramInUse() == 512 && store->dropped() == dropped
						&& back.startsWith(QLatin1String("needs ")) && backTip.contains(QLatin1String("Free now: "))
						&& backTip.contains(QLatin1String("With less free")),
				"chart, the free memory back: the RAM set again (512 MB), nothing more let go as the line grows; the note "
				"\"needs ...\" again, the RAM box's tooltip the free memory without a limit");
		tab.setTestFreeMemory(-1);
		tab.hide();
	}

	void chartInfoLine() {
		LoneChart chart(QStringLiteral("INFO"), QStringLiteral("V"));
		auto *label = chart.tab.findChild<QLabel *>(QStringLiteral("chartInfo"));
		if (!label) {
			check(false, "chart, the info line found");
			return;
		}
		/* the info line and the measure line in the muted colour, as every mutedLabel (their own names, for tests, keep it) */
		bool muted = true;
		for (const char *name : { "chartInfo", "measureInfo" }) {
			auto *line = chart.tab.findChild<QLabel *>(QLatin1String(name));
			if (line) line->ensurePolished();
			muted = muted && line && line->palette().color(line->foregroundRole()) == Theme::colors().muted;
		}
		check(muted, "chart: the info line and the measure line in the muted colour");
		if (auto *smooth = chart.tab.findChild<QAction *>(QStringLiteral("chartSmooth"))) smooth->setChecked(true); /* a delay */
		const QString full = chart.tab.infoText();
		QString noPaint = full, noPlotted, noDelay;
		noPaint.remove(QRegularExpression(QStringLiteral(" · [0-9]+\\.[0-9] ms")));
		noPlotted = noPaint;
		noPlotted.remove(QStringLiteral(" plotted"));
		noDelay = noPlotted;
		noDelay.remove(QRegularExpression(QStringLiteral(" · delay [0-9]+ ms")));
		const QFontMetrics metrics = label->fontMetrics();
		const auto at = [&](const QString &text, int less) { return chart.tab.infoText(metrics.horizontalAdvance(text) - less); };
		const bool steps = full != noPaint && noPaint != noPlotted && noPlotted != noDelay && at(full, 0) == full
				&& at(full, 1) == noPaint && at(noPaint, 0) == noPaint && at(noPaint, 1) == noPlotted
				&& at(noPlotted, 1) == noDelay && noDelay.startsWith(QStringLiteral("1/64 · "));
		bool whole = true;
		for (int width = 0; width < metrics.horizontalAdvance(full) + 4; width += 3) {
			const QString text = chart.tab.infoText(width);
			whole = whole && (text.isEmpty() || metrics.horizontalAdvance(text) <= width) && !text.contains(QChar(0x2026));
		}
		if (!steps) std::printf("     (\"%s\" -> \"%s\" -> \"%s\" -> \"%s\")\n", qPrintable(full),
				qPrintable(at(full, 1)), qPrintable(at(noPaint, 1)), qPrintable(at(noPlotted, 1)));
		chart.tab.setShown(true);
		chart.tab.refreshStatus();
		if (!whole || !label->toolTip().startsWith(chart.tab.infoText()))
			std::printf("     (whole parts: %s; the tooltip: \"%s\")\n", whole ? "yes" : "no", qPrintable(label->toolTip()));
		check(steps && whole && label->toolTip().startsWith(chart.tab.infoText()),
				"chart, the info line: narrow, whole parts go (the paint time, \"plotted\", the delay), no letter cut; "
				"all of it in the tooltip");
	}

	/* The translations as kept (translations/<name>.ts): every message finished and not empty, Arabic's plural forms all
	 * six, and each translation with the English's %1 placeholders, %n, %CODE% and %NAME% markers and HTML tags */
	void translationFiles() {
		const QDir dir(QStringLiteral(EVRE_TRANSLATIONS_DIR));
		const QStringList files = dir.entryList({ QStringLiteral("*.ts") }, QDir::Files);
		int messages = 0, unfinished = 0, emptied = 0, badForms = 0;
		QStringList mismatched;
		/* what must come through: placeholders and markers as counted, tags as written */
		const auto tokens = [](const QString &text) {
			static const QRegularExpression token(QStringLiteral("%[1-9][0-9]*|%[A-Z_]+%|<[^<>]+>"));
			QStringList found;
			for (auto it = token.globalMatch(text); it.hasNext();) found << it.next().captured();
			found.sort();
			return found;
		};
		for (const QString &name : files) {
			QFile file(dir.filePath(name));
			if (!file.open(QIODevice::ReadOnly)) continue;
			QXmlStreamReader xml(&file);
			const int forms = name.contains(QLatin1String("_ar")) ? 6 : 2;
			QString source, translation;
			QStringList numerus;
			bool isNumerus = false, finished = true;
			while (!xml.atEnd()) {
				xml.readNext();
				if (xml.isStartElement() && xml.name() == QLatin1String("message")) {
					isNumerus = xml.attributes().value(QLatin1String("numerus")) == QLatin1String("yes");
					numerus.clear();
					translation.clear();
					finished = true;
				} else if (xml.isStartElement() && xml.name() == QLatin1String("source")) {
					source = xml.readElementText();
				} else if (xml.isStartElement() && xml.name() == QLatin1String("translation")) {
					finished = xml.attributes().value(QLatin1String("type")).isEmpty();
					if (!isNumerus) translation = xml.readElementText();
				} else if (xml.isStartElement() && xml.name() == QLatin1String("numerusform")) {
					numerus << xml.readElementText();
				} else if (xml.isEndElement() && xml.name() == QLatin1String("message")) {
					messages++;
					if (!finished) unfinished++;
					const QStringList texts = isNumerus ? numerus : QStringList{ translation };
					if (isNumerus && numerus.size() != forms) badForms++;
					QStringList wanted = tokens(source);
					const bool plural = source.contains(QLatin1String("%n"));
					bool someN = false;
					for (const QString &text : texts) {
						if (text.trimmed().isEmpty()) emptied++;
						someN = someN || text.contains(QLatin1String("%n"));
						if (tokens(text) != wanted) mismatched << QStringLiteral("%1: \"%2\"").arg(name, source.left(60));
					}
					if (plural && !someN) mismatched << QStringLiteral("%1 (no %n): \"%2\"").arg(name, source.left(60));
				}
			}
		}
		for (const QString &m : std::as_const(mismatched)) std::printf("     (not as the English: %s)\n", qPrintable(m));
		std::printf("     (%lld translation files, %d messages)\n", (long long) files.size(), messages);
		check(!files.isEmpty() && messages > 1000 && unfinished == 0 && emptied == 0 && badForms == 0,
				"translations: every message of every .ts translated and finished, Arabic's plurals in all six forms");
		check(!files.isEmpty() && mismatched.isEmpty(), "translations: each keeps the English's %1 placeholders, %n, the "
				"%CODE% and %NAME% markers and the HTML tags");
	}

	/* The languages: the sidebar's choice (applied at the next start, Restart now meanwhile); Arabic right to left with
	 * the chart, the bit view and the Monitor left to right, Western digits, its plurals, the Help in Arabic; the main
	 * window at most 1280 px wide in each; English again after */
	void languages() {
		translationFiles();
		auto *choice = window_.findChild<QComboBox *>(QStringLiteral("language"));
		auto *restart = window_.findChild<QPushButton *>(QStringLiteral("restartNow"));
		QSettings().remove(QStringLiteral("ui/language"));
		bool sidebar = choice && restart && choice->count() == 3 && choice->itemText(2) == QStringLiteral("العربية")
				&& choice->itemData(0).toString() == QLatin1String("system") && restart->isHidden();
		if (choice) {
			choice->setCurrentIndex(2);
			emit choice->activated(2);
			sidebar = sidebar && QSettings().value(QStringLiteral("ui/language")).toString() == QLatin1String("ar")
					&& !restart->isHidden();
			choice->setCurrentIndex(1);
			emit choice->activated(1);
			sidebar = sidebar && restart->isHidden();
		}
		QSettings().remove(QStringLiteral("ui/language"));
		check(sidebar, "language: System, English, العربية at the bottom of the sidebar (ui/language), applied at the next "
				"start: Restart now shows while the choice is not the language running");

		/* the columns a text's ink spans in a box of a picture (logical px; -1: none): what differs from the box's corner */
		const auto ink = [](const QImage &picture, const QRectF &box, double &left, double &right) {
			const double dpr = picture.devicePixelRatio();
			const QRect r = QRectF(box.topLeft() * dpr, box.size() * dpr).toRect().intersected(picture.rect());
			left = right = -1;
			if (r.isEmpty()) return;
			const int ground = qGray(picture.pixel(r.topLeft()));
			for (int x = r.left(); x <= r.right(); x++)
				for (int y = r.top(); y <= r.bottom(); y++)
					if (std::abs(qGray(picture.pixel(x, y)) - ground) > 40) {
						if (left < 0) left = x / dpr;
						right = x / dpr;
						break;
					}
		};
		/* Arabic: the chart's and a histogram's value labels where they are aligned, at the right of their boxes by the
		 * plot (a painter on a widget takes the application's right to left, which put them at the window's left edge) */
		QString alignNotes;
		const auto labelsAligned = [&](MainWindow &other) {
			bool aligned = true;
			int labels = 0;
			if (auto *chart = other.findChild<ChartView *>()) {
				const QImage picture = chart->grab().toImage();
				for (const QRectF &box : chart->valueLabelRects()) {
					double left, right;
					ink(picture, box, left, right);
					if (left < 0) continue;
					labels++;
					if (right < box.right() - 6 || left < box.left() + 6) {
						aligned = false;
						alignNotes += QStringLiteral(" chart %1..%2 in %3..%4").arg(left).arg(right).arg(box.left()).arg(box.right());
					}
				}
			}
			QVector<double> times, values;
			for (int i = 0; i < 2000; i++) {
				times << i * 0.001;
				values << std::sin(i * 0.05);
			}
			AnalysisWindow histogram(AnalysisWindow::Kind::Histogram, QStringLiteral("WAVE"), QStringLiteral("V"),
					Qt::blue, QStringLiteral("the view"), times, values);
			histogram.resize(700, 450);
			histogram.show();
			(void) QTest::qWaitForWindowExposed(&histogram);
			const QImage picture = histogram.plot()->grab().toImage();
			const QRectF column(2, 30, 64 - 8, histogram.plot()->height() - 60); /* left of the plot (its LEFT, TOP) */
			double left, right;
			ink(picture, column, left, right);
			if (left < 0 || right < column.right() - 6 || left < column.left() + 6) {
				aligned = false;
				alignNotes += QStringLiteral(" histogram %1..%2 in %3..%4").arg(left).arg(right).arg(column.left())
						.arg(column.right());
			}
			return aligned && labels >= 2;
		};
		bool arabicAligned = false;
		/* each language: the main window fits 1280 px */
		const auto widest = [&](const QString &code, QString &notes) {
			language::apply(*qApp, code);
			MainWindow other;
			other.show();
			(void) QTest::qWaitForWindowExposed(&other);
			other.resize(1280, 800);
			QApplication::processEvents();
			const int minimum = other.minimumSizeHint().width();
			/* and with the trigger's row under the chart's actions shown (Display, Trigger) */
			int withTrigger = 0;
			if (auto *triggerAction = other.findChild<QAction *>(QStringLiteral("chartTrigger"))) {
				triggerAction->setChecked(true);
				QApplication::processEvents();
				withTrigger = other.minimumSizeHint().width();
				triggerAction->setChecked(false);
				QApplication::processEvents();
			}
			if (code == QLatin1String("ar") && qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* for a look */
				const QString prefix = qEnvironmentVariable("EVRE_TEST_SHOT");
				other.grab().save(prefix + QStringLiteral("_arabic.png"));
				if (auto *tabs = other.findChild<QTabWidget *>()) {
					tabs->setCurrentIndex(1);
					QApplication::processEvents();
					other.grab().save(prefix + QStringLiteral("_arabic_chart.png"));
					tabs->setCurrentIndex(0);
				}
			}
			if (code == QLatin1String("ar")) {
				auto *chart = other.findChild<ChartView *>();
				auto *bits = other.findChild<BitView *>();
				auto *sidebarCard = other.findChild<Sidebar *>();
				notes = QStringLiteral("%1%2%3%4%5").arg(qApp->layoutDirection() == Qt::RightToLeft ? 1 : 0)
						.arg(chart && chart->layoutDirection() == Qt::LeftToRight ? 1 : 0)
						.arg(bits && bits->layoutDirection() == Qt::LeftToRight ? 1 : 0)
						.arg(sidebarCard && sidebarCard->isRightToLeft() ? 1 : 0)
						.arg(other.width() == 1280 ? 1 : 0);
				arabicAligned = labelsAligned(other);
			}
			std::printf("     (%s: the main window's minimum width %d px, %d with the trigger's row)\n", qPrintable(code),
					minimum, withTrigger);
			return withTrigger > 0 ? std::max(minimum, withTrigger) : 100000;
		};
		QString arabicNotes, unused;
		const int english = widest(QStringLiteral("en"), unused);
		const int arabic = widest(QStringLiteral("ar"), arabicNotes);
		/* Arabic's texts, plurals, numbers and Help, while it is applied */
		const bool translated = QCoreApplication::translate("Sidebar", "Language") == QStringLiteral("اللغة")
				&& QCoreApplication::translate("BusPanel", "%n device(s) · %1", nullptr, 11).arg(QStringLiteral("x"))
						== QStringLiteral("11 جهازًا · x")
				&& QCoreApplication::translate("BusPanel", "%n device(s) · %1", nullptr, 4).arg(QStringLiteral("x"))
						== QStringLiteral("4 أجهزة · x");
		const bool westernDigits = QLocale().toString(1234.5) == QLatin1String("1234.5");
		QString helpTitle, helpText;
		{
			HelpDialog help;
			auto *topics = help.findChild<QListWidget *>(QStringLiteral("helpTopics"));
			auto *page = help.findChild<QTextBrowser *>();
			if (topics && page) {
				topics->setCurrentRow(0);
				helpTitle = topics->item(0)->text();
				helpText = page->toPlainText();
				if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* for a look */
					help.resize(980, 700);
					topics->setCurrentRow(4);
					help.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_arabic_help.png"));
				}
				topics->setCurrentRow(9); /* Command line: a code block */
				helpText += page->toHtml().contains(QLatin1String("%CODE%")) ? QStringLiteral("%CODE%") : QString();
			}
		}
		language::apply(*qApp, QStringLiteral("en"));
		const bool englishBack = qApp->layoutDirection() == Qt::LeftToRight
				&& QCoreApplication::translate("Sidebar", "Language") == QLatin1String("Language");
		check(english <= 1280 && arabic <= 1280, "language: the main window is at most 1280 px wide at its narrowest, in "
				"English and in Arabic, with the chart's trigger row shown too");
		check(arabicNotes == QLatin1String("11111") && translated && westernDigits, "language, Arabic: the window right to "
				"left, the chart and the bit view left to right; its texts and its plural forms (4 أجهزة, 11 جهازًا); numbers "
				"with Western digits and a decimal point");
		check(helpTitle == QStringLiteral("البدء") && helpText.startsWith(QStringLiteral("البدء"))
						&& !helpText.contains(QLatin1String("%CODE%")),
				"language, Arabic: the Help's pages through the same file (the first page in Arabic, its code blocks made)");
		if (!arabicAligned) std::printf("     (Arabic, value labels' ink:%s)\n", qPrintable(alignNotes));
		check(arabicAligned, "language, Arabic: the chart's and a histogram's value labels at the right of their boxes, by "
				"the plot, as in English (not at the window's left edge)");
		check(englishBack, "language: English applied again, left to right");
	}

	/* a QInputDialog's text typed and accepted (fillDialog's fill) */
	static void typeAndAccept(QDialog *dialog, const QString &text) {
		if (auto *input = qobject_cast<QInputDialog *>(dialog)) {
			input->setTextValue(text);
			input->accept();
		}
	}

	/* The recording's format read and written (model/recording_file.h): rows shared by samples closer than a quarter of
	 * their interval, a title's comma a semicolon, an empty cell no sample, a column of hex left out, the estimate from
	 * the head and the tail, a read from the middle, the notes beside it */
	void recordingFiles() {
		QTemporaryDir folder;
		const QString path = folder.filePath(QStringLiteral("lines.csv"));
		recording::Line volts{ QStringLiteral("VOLTS"), QStringLiteral("V"), {}, {} };
		recording::Line amps{ QStringLiteral("AMPS"), QStringLiteral("A"), {}, {} };
		recording::Line slow{ QStringLiteral("SLOW, X"), QString(), { 0.55 }, { 7.0 } };
		for (int i = 0; i < 10; i++) {
			volts.times << i * 0.1;
			volts.values << 12.0 + i;
			amps.times << i * 0.1 + 0.001; /* a millisecond later: the same row */
			amps.values << 0.5 * i;
		}
		const qint64 epoch = QDateTime(QDate(2026, 10, 5), QTime(14, 3, 12)).toMSecsSinceEpoch();
		std::atomic<bool> cancel{ false };
		qint64 rows = 0;
		QString error;
		const bool written = recording::write(path, { volts, amps, slow }, epoch, cancel, {}, rows, error);
		QFile file(path);
		QString header, first;
		if (file.open(QIODevice::ReadOnly)) {
			header = QString::fromUtf8(file.readLine()).trimmed();
			first = QString::fromUtf8(file.readLine()).trimmed();
			file.close();
		}
		recording::Data data;
		const bool read = recording::read(path, 0, cancel, {}, data, error);
		bool same = read && data.columns.size() == 3 && data.columns[1].times.size() == 10
				&& data.columns[2].name == QLatin1String("SLOW; X") && data.columns[2].times.size() == 1
				&& data.columns[0].unit == QLatin1String("V") && data.epochMs == epoch;
		for (int i = 0; same && i < 10; i++)
			same = std::fabs(data.columns[0].times[i] - i * 0.1) < 1e-6 && data.columns[0].values[i] == 12.0 + i
					&& std::fabs(data.columns[1].times[i] - i * 0.1) < 1e-6 && data.columns[1].values[i] == 0.5 * i;
		if (!same || rows != 11)
			std::printf("     (%lld rows; \"%s\" / \"%s\"; read %s)\n", (long long) rows, qPrintable(header),
					qPrintable(first), read ? "yes" : qPrintable(error));
		check(written && rows == 11 && header == QLatin1String("time_s,datetime,VOLTS [V],AMPS [A],SLOW; X")
						&& first == QLatin1String("0.000000,2026-10-05T14:03:12.000,12,0,") && same,
				"recording files: written as a recording (\"time_s,datetime,NAME [unit]\"), samples a millisecond apart "
				"share a row, a lone one has its own, an empty cell is no sample; read back the same");

		/* a byte array's hex: left out; the estimate; a read from the middle */
		const QString hex = folder.filePath(QStringLiteral("hex.csv"));
		QFile out(hex);
		if (out.open(QIODevice::WriteOnly)) {
			out.write("time_s,datetime,BUF,N [bar]\n");
			for (int i = 0; i < 1000; i++)
				out.write(QStringLiteral("%1,2026-10-05T14:03:%2.000,%3,%4\n").arg(10 + i * 0.01, 0, 'f', 6)
						.arg(12 + i / 100, 2, 10, QLatin1Char('0')).arg(i % 2 ? QStringLiteral("00ff") : QStringLiteral("0a"))
						.arg(i).toUtf8());
			out.close();
		}
		recording::Data hexData;
		recording::Estimate estimate;
		const bool skipped = recording::read(hex, 0, cancel, {}, hexData, error) && hexData.skipped == 1
				&& hexData.columns.size() == 1 && hexData.columns[0].name == QLatin1String("N")
				&& hexData.columns[0].unit == QLatin1String("bar") && hexData.columns[0].times.size() == 1000;
		const bool estimated = recording::estimate(hex, estimate, error) && std::abs(estimate.rows - 1000) <= 50
				&& estimate.firstTime == 10.0 && std::fabs(estimate.lastTime - 19.99) < 1e-9 && estimate.titles.size() == 2;
		recording::Data half;
		const bool middle = recording::read(hex, estimate.headerBytes + (estimate.bytes - estimate.headerBytes) / 2, cancel,
				{}, half, error) && !half.columns.isEmpty() && half.columns.last().times.size() > 400
				&& half.columns.last().times.size() < 600 && half.columns.last().times.last() == hexData.columns[0].times.last();
		if (!skipped || !estimated || !middle)
			std::printf("     (left out %d, %d columns; estimate %lld rows, %.3f .. %.3f; half %lld samples)\n",
					hexData.skipped, int(hexData.columns.size()), (long long) estimate.rows, estimate.firstTime,
					estimate.lastTime, half.columns.isEmpty() ? -1LL : (long long) half.columns.last().times.size());
		check(skipped && estimated && middle, "recording files: a column of hex (a byte array) left out; the estimate from "
				"the head and the tail (rows, first and last time); a read from the middle keeps the last part");

		/* the notes beside it */
		const QVector<ChartNote> notes{ { 0.25, QStringLiteral("pump on") }, { 0.75, QStringLiteral("Ü, \"quoted\"") } };
		QVector<ChartNote> back;
		const bool saved = recording::saveNotes(path, notes, epoch, error) && QFile::exists(path + QStringLiteral(".notes.json"))
				&& recording::loadNotes(path, back, error) && back == notes;
		const bool removed = recording::saveNotes(path, {}, epoch, error) && !QFile::exists(recording::notesPath(path))
				&& !recording::loadNotes(path, back, error) && error.isEmpty();
		check(saved && removed, "recording files: the notes saved beside it (<file>.notes.json) and read back; none left: "
				"the file removed");
	}

	/* the right-click on the chart: its menu; the picture copied and saved, painted by the CPU */
	void chartMenuAndPictures() {
		LoneChart chart(QStringLiteral("PIC"), QStringLiteral("V"));
		MathLines::Samples samples;
		for (int i = 0; i < 1000; i++) samples[regKey(chart.def)] << QPointF(90.0 + i * 0.01, std::sin(i * 0.02));
		chart.tab.frame(samples);
		chart.view->setWindow(10);
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		const QPoint at(chart.view->width() / 2, chart.view->height() / 2);
		QContextMenuEvent right(QContextMenuEvent::Mouse, at, chart.view->mapToGlobal(at));
		QApplication::sendEvent(chart.view, &right);
		QMenu *menu = chart.tab.chartMenu();
		QStringList texts;
		if (menu)
			for (QAction *action : menu->actions())
				if (!action->isSeparator()) texts << action->text();
		const QStringList want{ QStringLiteral("Copy picture"), QStringLiteral("Save picture…"),
			QStringLiteral("Export to CSV…"), QStringLiteral("Add note here"), QStringLiteral("Open recording…"),
			QStringLiteral("Recent recordings") };
		const bool shown = menu && QTest::qWaitFor([&] { return menu->isVisible(); }, 2000) && texts == want;
		if (!shown) std::printf("     (the chart's menu: %s)\n", qPrintable(texts.join(QStringLiteral(" | "))));
		if (menu) menu->close();
		check(shown, "chart: a right-click shows its menu: Copy picture, Save picture, Export to CSV, Add note here, Open "
				"recording, Recent recordings");
		QTemporaryDir folder;
		const QString png = folder.filePath(QStringLiteral("chart.png"));
		const bool saved = chart.tab.savePicture(png);
		const QImage image(png);
		const QSize device = (QSizeF(chart.view->size()) * chart.view->devicePixelRatioF()).toSize();
		chart.tab.copyPicture();
		const QImage copied = QApplication::clipboard()->image();
		check(saved && image.size() == device && copied.size() == device,
				"chart: Save picture writes a PNG of the chart as shown (its size in pixels), Copy picture puts it on the "
				"clipboard; both painted by the CPU");
		chart.tab.hide();
	}

	/* Export to CSV: the view, or A -> B; the notes in it beside it; a big one on a thread, with its progress and
	 * Cancel (the file then removed) */
	void chartExport() {
		LoneChart chart(QStringLiteral("EXP"), QStringLiteral("V"));
		RegDef second = chart.def;
		second.addr = 0xD002;
		second.name = QStringLiteral("EXP2");
		chart.tab.plotRegister(second, true);
		MathLines::Samples samples;
		for (int i = 0; i < 2000; i++) { /* 90 .. 99.995 s, 200 Hz, both in the same polls */
			samples[regKey(chart.def)] << QPointF(90.0 + i * 0.005, i);
			samples[regKey(second)] << QPointF(90.0 + i * 0.005, -i);
		}
		chart.tab.frame(samples);
		chart.view->setWindow(5); /* the view: 95 .. 100 */
		/* no display delay: it grows with the time between frames (a slow machine's first frame moved the view to
		 * 94.993 .. 99.993, one sample early) */
		chart.view->setSmooth(false);
		(void) chart.view->grab();
		QTemporaryDir folder;
		const auto run = [&](const QString &name, qint64 &rows, QString &error) {
			QSignalSpy done(&chart.tab, &ChartTab::exported);
			const QString file = folder.filePath(name);
			if (!chart.tab.exportCsv(file)) return QStringList();
			if (!done.wait(10000)) return QStringList();
			rows = done.first().at(1).toLongLong();
			error = done.first().at(2).toString();
			QFile in(file);
			if (!in.open(QIODevice::ReadOnly)) return QStringList();
			/* the platform's line ends, as a recording (CRLF on Windows) */
			return QString::fromUtf8(in.readAll()).split(QRegularExpression(QStringLiteral("\r?\n")), Qt::SkipEmptyParts);
		};
		qint64 rows = 0;
		QString error;
		const QStringList view = run(QStringLiteral("view.csv"), rows, error);
		const bool ofView = rows == 1000 && view.size() == 1001 && view[0] == QLatin1String("time_s,datetime,EXP [V],EXP2 [V]")
				&& view[1].startsWith(QLatin1String("95.000000,")) && view[1].endsWith(QLatin1String(",1000,-1000"))
				&& error.isEmpty();
		if (!ofView) std::printf("     (view: %lld rows, %s / %s)\n", (long long) rows, qPrintable(view.value(0)), qPrintable(view.value(1)));
		chart.view->setCursors(92.0, 91.0);
		chart.view->addNote(91.5, QStringLiteral("inside"));
		chart.view->addNote(93.0, QStringLiteral("outside"));
		const QStringList span = run(QStringLiteral("span.csv"), rows, error);
		QVector<ChartNote> notes;
		QString notesError;
		const bool ofSpan = rows == 201 && span.value(1).startsWith(QLatin1String("91.000000,"))
				&& span.last().startsWith(QLatin1String("92.000000,"))
				&& recording::loadNotes(folder.filePath(QStringLiteral("span.csv")), notes, notesError)
				&& notes == QVector<ChartNote>{ { 91.5, QStringLiteral("inside") } }
				&& RecordingWindow::recentFiles().value(0) == QFileInfo(folder.filePath(QStringLiteral("span.csv"))).absoluteFilePath();
		check(ofView && ofSpan, "chart, Export to CSV: the view's samples in the recording's format (the lines of one poll "
				"in one row); with cursors A -> B only, the notes in it beside it; listed in Recent recordings");

		/* a big one: 40 lines of 150 000 samples, on a thread; Cancel removes the file */
		LoneChart big(QStringLiteral("BIG0"), QStringLiteral("V"));
		MathLines::Samples many;
		for (int k = 0; k < 40; k++) {
			RegDef def = big.def;
			def.addr = uint16_t(0xD000 + 2 * k);
			def.name = QStringLiteral("BIG%1").arg(k);
			if (k > 0) big.tab.plotRegister(def, true);
			QVector<QPointF> &points = many[regKey(def)];
			points.reserve(150000);
			for (int i = 0; i < 150000; i++) points << QPointF(i * 0.0001, std::sin(i * 0.001 + k));
		}
		big.now = 15.0;
		big.tab.frame(many);
		big.view->setMemory(30);
		big.view->setWindow(20);
		big.tab.show();
		(void) QTest::qWaitForWindowExposed(&big.tab);
		(void) big.view->grab();
		QSignalSpy done(&big.tab, &ChartTab::exported);
		const QString file = folder.filePath(QStringLiteral("big.csv"));
		QElapsedTimer started;
		started.start();
		const bool began = big.tab.exportCsv(file);
		const qint64 returnedMs = started.elapsed();
		const bool running = big.tab.exporting() && !big.tab.exportCsv(folder.filePath(QStringLiteral("again.csv")));
		QProgressDialog *progress = nullptr;
		const bool progressShown = QTest::qWaitFor([&] {
			progress = big.tab.findChild<QProgressDialog *>(QStringLiteral("exportProgress"));
			return progress && progress->isVisible();
		}, 3000);
		const int valueSeen = progress ? progress->value() : -1;
		const bool unanimated = progress && progress->property("noAnimation").toBool(); /* STUDIO.md 27 */
		if (QPushButton *cancel = progress ? buttonWithText(*progress, QStringLiteral("Cancel")) : nullptr) cancel->click();
		const bool ended = done.wait(10000) || done.size() == 1;
		const QString why = done.isEmpty() ? QString() : done.first().at(2).toString();
		std::printf("     (6 M samples: the export started in %lld ms, its progress at %d of 1000 when cancelled; %s %s %s %s "
				"\"%s\" %s)\n", (long long) returnedMs, valueSeen, began ? "began" : "-", running ? "running" : "-",
				progressShown ? "shown" : "-", ended ? "ended" : "-", qPrintable(why), QFile::exists(file) ? "file left" : "");
		check(began && running && returnedMs < 2000 && progressShown && unanimated && ended && why == QLatin1String("cancelled")
						&& !QFile::exists(file) && !big.tab.exporting(),
				"chart, Export to CSV: a big one runs on a thread (the window answers), a progress dialog with Cancel; "
				"cancelled, its file is removed");
		big.tab.hide();
	}

	/* a note's tag starts at its time (or ends there, by the plot's right edge), within 1.5 px, in the chart's geometry
	 * now: the window may have been laid out again since a point was taken (Windows at 225 %) */
	static bool tagAtTime(const ChartView &view, const QRectF &tag, double time) {
		const double perPixel = view.timeAt(1) - view.timeAt(0);
		return std::fabs(view.timeAt(tag.left()) - time) < 1.5 * perPixel || std::fabs(view.timeAt(tag.right()) - time) < 1.5 * perPixel;
	}

	/* Notes: Add note here (its text asked), a tag at the bottom of the plot; dragged to move; double-click edits;
	 * clicked, Delete removes it */
	void chartNotes() {
		LoneChart chart(QStringLiteral("NOTE"), QStringLiteral("V"));
		MathLines::Samples samples;
		for (int i = 0; i < 1000; i++) samples[regKey(chart.def)] << QPointF(90.0 + i * 0.01, std::sin(i * 0.02));
		chart.tab.frame(samples);
		chart.view->setWindow(10);
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		(void) chart.view->grab();
		chart.view->setLive(false);
		QSignalSpy changed(&chart.tab, &ChartTab::notesChanged);
		const QPoint at(chart.view->width() / 2, chart.view->height() / 2);
		const double time = chart.view->timeAt(at.x());
		QContextMenuEvent right(QContextMenuEvent::Mouse, at, chart.view->mapToGlobal(at));
		QApplication::sendEvent(chart.view, &right);
		QAction *add = nullptr;
		if (QMenu *menu = chart.tab.chartMenu())
			for (QAction *action : menu->actions())
				if (action->text() == QLatin1String("Add note here")) add = action;
		bool unanimated = false; /* made without the window animations (STUDIO.md 27) */
		const bool asked = add && fillDialog([&](QDialog *d) {
			unanimated = d->property("noAnimation").toBool();
			typeAndAccept(d, QStringLiteral("valve open"));
		}, [&] { add->trigger(); });
		if (QMenu *menu = chart.tab.chartMenu()) menu->close();
		(void) chart.view->grab();
		const QRectF tag = chart.view->noteTag(0);
		const bool added = asked && unanimated && chart.view->notes().size() == 1 && std::fabs(chart.view->notes()[0].time - time) < 1e-9
				&& chart.view->notes()[0].text == QLatin1String("valve open") && !tag.isEmpty()
				&& !tag.isEmpty() && tagAtTime(*chart.view, tag, time) && tag.bottom() < chart.view->lastPlot().bottom()
				&& changed.size() == 1;
		if (!added)
			std::printf("     (note: menu action %s, asked %d, %lld notes, time %.6f vs %.6f, tag %.1f..%.1f x %.1f, plot bottom %.1f, "
					"%lld changes)\n", add ? "found" : "missing", asked, (long long) chart.view->notes().size(),
					chart.view->notes().isEmpty() ? 0.0 : chart.view->notes()[0].time, time, tag.left(), tag.bottom(), double(at.x()),
					chart.view->lastPlot().bottom(), (long long) changed.size());
		check(added,"chart, notes: Add note here asks its text (a dialog without the window animations); a dashed line at "
				"that time and a tag at the bottom of the plot");

		/* dragged by its tag */
		const QPoint grab = tag.center().toPoint(), to = grab + QPoint(100, 0);
		QTest::mousePress(chart.view, Qt::LeftButton, Qt::NoModifier, grab);
		QMouseEvent move(QEvent::MouseMove, QPointF(to), chart.view->mapToGlobal(QPointF(to)), Qt::NoButton, Qt::LeftButton,
				Qt::NoModifier);
		QApplication::sendEvent(chart.view, &move);
		QTest::mouseRelease(chart.view, Qt::LeftButton, Qt::NoModifier, to);
		const double moved = chart.view->notes().value(0).time;
		const bool dragged = std::fabs(moved - chart.view->timeAt(to.x())) < 1e-9 && moved > time && changed.size() == 2;
		/* double-click: edited */
		(void) chart.view->grab();
		const QPoint tagAt = chart.view->noteTag(0).center().toPoint();
		const bool edited = fillDialog([](QDialog *d) { typeAndAccept(d, QStringLiteral("valve shut")); },
				[&] { QTest::mouseDClick(chart.view, Qt::LeftButton, Qt::NoModifier, tagAt); })
				&& chart.view->notes().value(0).text == QLatin1String("valve shut");
		check(dragged && edited, "chart, notes: a tag dragged moves its note; a double-click on it edits the text");
		/* clicked, then Delete */
		QTest::mouseClick(chart.view, Qt::LeftButton, Qt::NoModifier, tagAt);
		const bool chosen = chart.view->selectedNote() == 0;
		QTest::keyClick(chart.view, Qt::Key_Delete);
		check(chosen && chart.view->notes().isEmpty(), "chart, notes: a tag clicked and Delete removes its note");
		chart.tab.hide();
	}

	/* Lanes: a plot per unit, stacked, equal heights; each lane its own Y range (Auto, Manual, Log from a right-click
	 * on its labels; Ctrl + wheel; a double-click), saved; a line drawn in its lane only; the cursors and notes across
	 * them; drawn on threads as on one */
	void chartLanes() {
		QSettings().remove(QStringLiteral("chart/lanes"));
		QSettings().remove(QStringLiteral("chart/laneY"));
		QSettings().remove(QStringLiteral("chart/lanesFolded"));
		LoneChart chart(QStringLiteral("V1"), QStringLiteral("V"));
		const QStringList units{ QStringLiteral("V"), QStringLiteral("A"), QStringLiteral("W"), QString() };
		MathLines::Samples samples;
		QVector<RegDef> defs;
		for (int k = 0; k < 8; k++) { /* two lines a unit */
			RegDef def = chart.def;
			def.addr = uint16_t(0xD000 + 2 * k);
			def.name = QStringLiteral("L%1").arg(k);
			def.unit = units[k / 2];
			if (k > 0) chart.tab.plotRegister(def, true);
			else chart.tab.plotRegister(chart.def, false), chart.tab.plotRegister(def, true);
			defs << def;
			for (int i = 0; i < 4000; i++) /* V around 12, A around 0.5, W around 6, no unit around 100 */
				samples[regKey(def)] << QPointF(90.0 + i * 0.0025, (k / 2 == 0 ? 12 : k / 2 == 1 ? 0.5 : k / 2 == 2 ? 6 : 100)
						+ std::sin(i * 0.01 + k) * (k / 2 == 1 ? 0.2 : 1));
		}
		chart.tab.frame(samples);
		chart.view->setWindow(10);
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		auto *lanes = chart.tab.findChild<QAction *>(QStringLiteral("chartLanes"));
		auto *mode = chart.tab.findChild<QComboBox *>(QStringLiteral("yMode"));
		if (!lanes || !mode) {
			check(false, "chart, Lanes: in the Display menu");
			return;
		}
		lanes->setChecked(true);
		(void) chart.view->grab();
		ChartView *view = chart.view;
		bool stacked = view->laneCount() == 4;
		for (int k = 0; stacked && k < 4; k++) {
			stacked = view->laneLabel(k) == units[k] && std::fabs(view->laneRect(k).height() - view->laneRect(0).height()) < 0.01
					&& view->laneLines(k).size() == 2;
			if (k > 0) stacked = stacked && view->laneRect(k).top() > view->laneRect(k - 1).bottom();
		}
		const bool saved = QSettings().value(QStringLiteral("chart/lanes")).toBool() && mode->isEnabled(); /* the current lane's */
		/* each its own Auto range: 12 V in the first, 0.5 A in the second */
		const bool ranges = view->laneYLo(0) < 11.5 && view->laneYHi(0) > 12.5 && view->laneYHi(0) < 15
				&& view->laneYLo(1) < 0.4 && view->laneYHi(1) < 1.0 && view->laneYOfValue(0, 12) > view->laneRect(0).top()
				&& view->laneYOfValue(0, 12) < view->laneRect(0).bottom() && view->laneYOfValue(1, 0.5) > view->laneRect(1).top()
				&& view->laneYOfValue(1, 0.5) < view->laneRect(1).bottom();
		if (!stacked || !ranges)
			std::printf("     (%d lanes; V %.3g .. %.3g, A %.3g .. %.3g)\n", view->laneCount(), view->laneYLo(0),
					view->laneYHi(0), view->laneYLo(1), view->laneYHi(1));
		check(stacked && saved && ranges, "chart, Lanes: a plot per unit in the order they came (V, A, W, none), stacked, "
				"of equal height, each with its own Auto range; saved (chart/lanes), the Y range row the current lane's");

		/* the second lane's Y range from its labels: Manual 0 .. 5; the third's Log */
		(void) chart.view->grab();
		const QPoint labels(20, int(view->laneRect(1).center().y()));
		QContextMenuEvent right(QContextMenuEvent::Mouse, labels, view->mapToGlobal(labels));
		QApplication::sendEvent(view, &right);
		QMenu *menu = chart.tab.laneMenu();
		QStringList items;
		QAction *manual = nullptr;
		if (menu)
			for (QAction *action : menu->actions()) {
				if (action->isSeparator()) continue;
				items << action->text();
				if (action->text() == QStringLiteral("Manual…")) manual = action;
			}
		const bool popped = menu && QTest::qWaitFor([&] { return menu->isVisible(); }, 2000)
				&& items == QStringList{ QStringLiteral("Auto"), QStringLiteral("Manual…"), QStringLiteral("Log"),
						QStringLiteral("All lanes: Auto"), QStringLiteral("Fold lane") }
				&& menu->actions().value(0)->text() == QLatin1String("Lane A: Y range"); /* its title, a section */
		if (!popped) std::printf("     (the lane's menu: %s)\n", qPrintable(items.join(QStringLiteral(" | "))));
		bool unanimated = false; /* made without the window animations (STUDIO.md 27) */
		const bool typed = manual && fillDialog([&](QDialog *d) {
			unanimated = d->property("noAnimation").toBool();
			auto *low = d->findChild<QLineEdit *>(QStringLiteral("laneMin"));
			auto *high = d->findChild<QLineEdit *>(QStringLiteral("laneMax"));
			auto *buttons = d->findChild<QDialogButtonBox *>();
			if (!low || !high || !buttons) return;
			low->setText(QStringLiteral("0"));
			high->setText(QStringLiteral("5"));
			buttons->button(QDialogButtonBox::Ok)->click();
		}, [&] { manual->trigger(); });
		if (menu) menu->close();
		view->setLaneYLog(2, true);
		(void) chart.view->grab();
		const bool manualSet = typed && unanimated && !view->laneYAuto(1) && view->laneYLo(1) == 0 && view->laneYHi(1) == 5
				&& view->laneYAuto(0) && view->laneYLog(2) && !view->laneYLog(1)
				&& std::fabs((view->laneYOfValue(2, 1) - view->laneYOfValue(2, 10)) - (view->laneYOfValue(2, 10) - view->laneYOfValue(2, 100))) < 1e-6;
		const QStringList kept = QSettings().value(QStringLiteral("chart/laneY")).toStringList();

		bool restored = false;
		{
			ChartTab again([] { return 100.0; });
			auto *otherView = again.findChild<ChartView *>();
			for (const RegDef &def : std::as_const(defs)) again.plotRegister(def, true);
			restored = otherView && otherView->lanes() && !otherView->laneYAuto(1) && otherView->laneYHi(1) == 5
					&& otherView->laneYLog(2);
		}
		check(popped && manualSet && kept.contains(QStringLiteral("A\t0\t0\t0\t5")) && restored,
				"chart, Lanes: a right-click on a lane's labels: Auto, Manual… (0 .. 5 typed; a dialog without the window "
				"animations), Log, All lanes: Auto, Fold lane; each lane alone; kept (chart/laneY) for the next start");

		/* Ctrl + wheel over the first lane: that lane Manual; a double-click there: Auto again */
		const QPointF inFirst(view->laneRect(0).center());
		QWheelEvent zoom(inFirst, view->mapToGlobal(inFirst), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
				Qt::NoScrollPhase, false);
		QApplication::sendEvent(view, &zoom);
		const bool zoomed = !view->laneYAuto(0) && !view->laneYAuto(1) && view->laneYHi(1) == 5 && view->laneYAuto(3);
		if (!zoomed)
			std::printf("     (after Ctrl + wheel: lanes 0, 1, 3 %s %s %s, the second's top %g)\n", view->laneYAuto(0) ? "auto" : "manual",
					view->laneYAuto(1) ? "auto" : "manual", view->laneYAuto(3) ? "auto" : "manual", view->laneYHi(1));
		QTest::mouseDClick(view, Qt::LeftButton, Qt::NoModifier, inFirst.toPoint());
		check(zoomed && view->laneYAuto(0) && !view->laneYAuto(1), "chart, Lanes: Ctrl + wheel zooms the lane under the "
				"mouse alone; a double-click there sets it to Auto");

		/* a line in its lane only: the first lane Manual 11.9 .. 12.1, its lines far past it; the gap under it empty */
		view->setLaneYManual(0, 11.9, 12.1);
		const QImage picture = view->grab().toImage();
		const qreal dpr = view->devicePixelRatioF();
		const QRectF gap(view->laneRect(0).left() + 4, view->laneRect(0).bottom() + 3, view->laneRect(0).width() - 8,
				view->laneRect(1).top() - view->laneRect(0).bottom() - 6);
		const QColor lineColor = Theme::colors().series[1]; /* L0's: the second colour (V1 took the first) */
		int stray = 0;
		for (int y = int(gap.top() * dpr); y < int(gap.bottom() * dpr); y++)
			for (int x = int(gap.left() * dpr); x < int(gap.right() * dpr); x += 2) {
				const QColor c = picture.pixelColor(x, y);
				if (std::abs(c.red() - lineColor.red()) + std::abs(c.green() - lineColor.green())
						+ std::abs(c.blue() - lineColor.blue()) < 60)
					stray++;
			}
		check(gap.height() > 2 && stray == 0, "chart, Lanes: a line past its lane's range is cut at the lane's edge, "
				"nothing of it between the lanes");
		view->setLaneYAuto(0);
		view->setLaneYAuto(1);
		view->setLaneYLog(2, false);

		/* the cursors, the A-B bar and a note across all of them; drawn on threads as on one */
		view->setLive(false); /* the same view in both pictures */
		view->setCursors(92.0, 96.0);
		view->addNote(94.0, QStringLiteral("lanes"));
		const QImage threads = view->grab().toImage();
		const QRectF tag = view->noteTag(0);
		view->setDrawThreads(1);
		const QImage one = view->grab().toImage();
		view->setDrawThreads(0);
		int differing = 0; /* over the lanes (the memory strip follows the clock's delay from one picture to the next) */
		const QRect lanesArea = QRectF(view->laneRect(0).topLeft(), view->laneRect(3).bottomRight()).adjusted(-2, -2, 2, 2)
				.toAlignedRect();
		const QRect device((QPointF(lanesArea.topLeft()) * view->devicePixelRatioF()).toPoint(),
				(QSizeF(lanesArea.size()) * view->devicePixelRatioF()).toSize());
		for (int y = device.top(); y <= device.bottom() && one.size() == threads.size(); y++)
			for (int x = device.left(); x <= device.right(); x++)
				if (one.pixel(x, y) != threads.pixel(x, y)) differing++;
		const bool across = !view->spanBarText().isEmpty() && view->spanBarRect().bottom() < view->laneRect(0).top() + 16
				&& tag.bottom() <= view->laneRect(3).bottom() && tag.top() > view->laneRect(3).top();
		std::printf("     (lanes on threads and on one: %d pixels apart)\n", differing);

		check(across && one.size() == threads.size() && differing < one.width(), "chart, Lanes: the A-B bar over the first "
				"lane, a note's tag at the bottom of the last; drawn on threads as on one");
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) /* the lanes, for a look */
			threads.save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_lanes.png"));
		view->setNotes({});
		lanes->setChecked(false);
		check(view->laneCount() == 0 && mode->isEnabled() && !QSettings().value(QStringLiteral("chart/lanes")).toBool(),
				"chart, Lanes off: one plot again, the Y range row back");
		QSettings().remove(QStringLiteral("chart/laneY"));
		chart.tab.hide();
	}

	/* Lanes that fit: a lane per unit however many (the example map's units and math lines in two more: ten), each at
	 * least LANE_MIN_H; when they do not fit, they scroll inside the plot (the wheel over the value labels, a bar in
	 * the right pad); a lane out of view is nowhere for the mouse. A click on a unit name folds its lane into a strip
	 * that lists its lines and values, a click on the strip opens it (no cursor placed); the folds kept by unit. */
	/* a Chart tab's settings for ten lanes (group): Lanes on, two math lines over the volts and the amps in W and Ω
	 * (two more units than the map's) */
	void prepareLanesSettings(const QString &group) {
		QSettings().remove(group);
		QSettings().setValue(group + QStringLiteral("/math"),
				QStringList{ QStringLiteral("P\tW\t%1 * %2\t1").arg(regs_.volts.name, regs_.amps.name),
						QStringLiteral("R\tΩ\t%1 / %2\t1").arg(regs_.volts.name, regs_.amps.name) });
		QSettings().setValue(group + QStringLiteral("/lanes"), true);
	}

	/* every numeric register of the map on the tab, and (samples) a wave for each */
	void plotMapLanes(ChartTab &tab, MathLines::Samples *samples) {
		tab.resize(1200, 700);
		tab.setRegisters(map_.regs);
		int k = 0;
		for (const RegDef &def : map_.regs) {
			if (!def.isNumeric()) continue;
			tab.plotRegister(def, true);
			if (!samples) continue;
			for (int i = 0; i < 400; i++) /* the amps never 0: R is a number */
				(*samples)[regKey(def)] << QPointF(90.0 + i * 0.025, 2 + k + std::sin(i * 0.05 + k));
			k++;
		}
	}

	void chartLanesFit() {
		const QString group = QStringLiteral("lanesFit");
		prepareLanesSettings(group);
		const auto open = [this](ChartTab &tab, MathLines::Samples *samples) { plotMapLanes(tab, samples); };
		ChartTab tab([] { return 100.0; }, nullptr, group);
		MathLines::Samples samples;
		open(tab, &samples);
		tab.frame(samples);
		ChartView *view = tab.findChild<ChartView *>();
		view->showLastValues();
		tab.show();
		(void) QTest::qWaitForWindowExposed(&tab);
		(void) view->grab();
		QSet<QString> units;
		bool own = view->lanes(), tall = true;
		for (int k = 0; k < view->laneCount(); k++) {
			own = own && !units.contains(view->laneLabel(k)) && !view->laneLabel(k).contains(QStringLiteral(" · "));
			units << view->laneLabel(k);
			tall = tall && view->laneRect(k).height() >= ChartView::LANE_MIN_H - 0.01;
		}
		const QRectF bar = view->laneScrollBarRect(); /* as tall as the plot */
		const bool overflows = !bar.isEmpty() && view->laneContentHeight() > bar.height() && view->laneScroll() == 0
				&& view->laneRect(0).top() == bar.top();
		if (view->laneCount() < 10 || !own || !tall || !overflows)
			std::printf("     (%d lanes: %s; the first %g px; the lanes %g px in a plot of %g)\n", view->laneCount(),
					qPrintable(QStringList(units.begin(), units.end()).join(QStringLiteral(", "))), view->laneRect(0).height(),
					view->laneContentHeight(), bar.height());
		check(view->laneCount() >= 10 && own && tall && overflows, "chart, Lanes that fit: a lane per unit (ten: the map's "
				"and two math lines), none shared, each at least 80 px; taller than the plot, the scroll bar shows");

		/* the wheel over the value labels: a step down; far up and far down, held at the ends; over the plot, time */
		const auto wheel = [view](QPointF at, int notches) {
			QWheelEvent e(at, view->mapToGlobal(at), QPoint(), QPoint(0, 120 * notches), Qt::NoButton, Qt::NoModifier,
					Qt::NoScrollPhase, false);
			QApplication::sendEvent(view, &e);
		};
		const QPointF labels(40, bar.center().y());
		const double most = view->laneContentHeight() - bar.height();
		wheel(labels, -1);
		const double oneStep = view->laneScroll();
		wheel(labels, 5);
		const double top = view->laneScroll();
		wheel(labels, -100);
		const double bottom = view->laneScroll();
		(void) view->grab();
		const int last = view->laneCount() - 1;
		const bool atEnd = std::fabs(view->laneRect(last).bottom() - bar.bottom()) < 0.01;
		const double window = view->window();
		wheel(QPointF(bar.center().x() - 300, bar.center().y()), 1);
		const bool timeZoom = view->window() < window && view->laneScroll() == bottom;
		view->setWindow(window);
		if (!(oneStep > 0 && oneStep < ChartView::LANE_MIN_H && top == 0 && std::fabs(bottom - most) < 0.01 && atEnd && timeZoom))
			std::printf("     (wheel: a step %g, up %g, down %g of %g; the last lane's bottom %g, the plot's %g)\n", oneStep, top,
					bottom, most, view->laneRect(last).bottom(), bar.bottom());
		check(oneStep > 0 && oneStep < ChartView::LANE_MIN_H && top == 0 && std::fabs(bottom - most) < 0.01 && atEnd
				&& timeZoom, "chart, Lanes that fit: the wheel over the value labels scrolls them by a step, held at the top "
				"and at the bottom (the last lane's bottom on the plot's); over the plot it still zooms the time");

		/* the first lane out of view: no height of the plot finds it */
		bool nowhere = view->laneRect(0).bottom() <= bar.top();
		for (double y = bar.top(); y <= bar.bottom() && nowhere; y += 1) nowhere = view->laneAtY(y) != 0;
		check(nowhere && view->laneAtY(bar.center().y()) > 0, "chart, Lanes that fit: a lane scrolled out of view has no "
				"place for the mouse (laneAtY)");

		/* the bar's handle dragged half its free travel: half way down */
		view->setLaneScroll(0);
		(void) view->grab();
		const QRectF handle = view->laneScrollHandleRect();
		const double travel = bar.height() - handle.height();
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, handle.center().toPoint());
		const QPointF to(handle.center().x(), handle.center().y() + travel / 2);
		QMouseEvent move(QEvent::MouseMove, to, view->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(view, &move);
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to.toPoint());
		const double dragged = view->laneScroll();
		/* a click under the handle: a plot height further */
		(void) view->grab();
		const QPointF below(bar.center().x(), view->laneScrollHandleRect().bottom() + 4);
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, below.toPoint());
		const double paged = view->laneScroll();
		const bool dragOk = std::fabs(dragged - most / 2) < most * 0.05
				&& std::fabs(paged - std::min(most, dragged + bar.height())) < 0.5;
		if (!dragOk) std::printf("     (the handle dragged: %g of %g; a click under it: %g)\n", dragged, most, paged);
		check(dragOk, "chart, Lanes that fit: the scroll bar's handle dragged half way scrolls half way; a click under it "
				"moves one plot height");

		/* a click on a unit name folds its lane: a strip of 22 px that lists its lines and their values */
		view->setLaneScroll(0);
		(void) view->grab();
		int volts = -1;
		for (int k = 0; k < view->laneCount(); k++)
			if (view->laneLabel(k) == regs_.volts.unit) volts = k;
		const double contentBefore = view->laneContentHeight();
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, QPoint(9, int(view->laneRect(volts).center().y())));
		(void) view->grab();
		const QString strip = view->foldedText(volts);
		const QString value = view->legendValue(int(regKey(regs_.volts)));
		bool othersOpen = true;
		for (int k = 0; k < view->laneCount(); k++)
			if (k != volts) othersOpen = othersOpen && view->laneRect(k).height() >= ChartView::LANE_MIN_H - 0.01;
		const bool folded = volts >= 0 && view->laneFolded(volts)
				&& std::fabs(view->laneRect(volts).height() - ChartView::LANE_FOLDED_H) < 0.01 && othersOpen
				&& view->laneContentHeight() < contentBefore - 50 && !value.isEmpty()
				&& strip.startsWith(regs_.volts.unit) && strip.contains(regs_.volts.name + QLatin1Char(' ') + value);
		if (!folded) std::printf("     (lane %d folded %d, %g px; its strip \"%s\", the value %s)\n", volts,
				int(view->laneFolded(volts)), view->laneRect(volts).height(), qPrintable(strip), qPrintable(value));
		check(folded, "chart, Lanes that fit: a click on a lane's unit name folds it: a 22 px strip with its unit, its "
				"lines and their values; the other lanes stay open");
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* scrolled half a lane, one folded, for a look */
			view->setLaneScroll(ChartView::LANE_MIN_H / 2);
			view->grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_lanes_fit.png"));
			view->setLaneScroll(0);
			(void) view->grab();
		}

		/* a click on the strip opens it again, and places no cursor */
		view->setCursorMode(true);
		view->clearCursors();
		const QRectF stripRect = view->laneRect(volts);
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, stripRect.center().toPoint());
		view->setCursorMode(false);
		(void) view->grab();
		const bool opened = !view->laneFolded(volts) && view->laneRect(volts).height() >= ChartView::LANE_MIN_H - 0.01
				&& !std::isfinite(view->cursorA()) && view->foldedText(volts).isEmpty();
		check(opened, "chart, Lanes that fit: a click on a folded strip opens the lane again (no cursor placed)");

		/* the fold kept: across Lanes off and on, in the settings, and in a new tab of the same settings */
		view->setLaneFolded(volts, true);
		const QStringList saved = QSettings().value(group + QStringLiteral("/lanesFolded")).toStringList();
		view->setLanes(false);
		view->setLanes(true);
		const bool across = view->laneFolded(volts) && view->laneScroll() == 0;
		bool again = false;
		{
			ChartTab other([] { return 100.0; }, nullptr, group);
			open(other, nullptr);
			ChartView *otherView = other.findChild<ChartView *>();
			again = otherView && otherView->lanes() && otherView->laneFolded(volts);
		}
		check(saved == QStringList{ regs_.volts.unit } && across && again, "chart, Lanes that fit: a fold is kept by its "
				"unit: after Lanes off and on, and in a new tab (lanesFolded)");

		/* few lanes: nothing scrolls, no bar; a fold makes the other lanes taller; the lane's menu folds and opens */
		for (const RegDef &def : map_.regs)
			if (def.isNumeric() && def.unit != regs_.volts.unit && def.unit != regs_.amps.unit) tab.plotRegister(def, false);
		for (int k = 0; k < view->laneCount(); k++) view->setLaneFolded(k, false);
		(void) view->grab();
		wheel(QPointF(40, view->laneRect(0).center().y()), -1);
		const double openHeight = view->laneRect(1).height();
		const bool still = view->laneCount() == 4 && view->laneScrollBarRect().isEmpty() && view->laneScroll() == 0;
		const auto menuAction = [&](int lane, const QPointF &at) -> QAction * {
			QContextMenuEvent right(QContextMenuEvent::Mouse, at.toPoint(), view->mapToGlobal(at.toPoint()));
			QApplication::sendEvent(view, &right);
			QMenu *menu = tab.laneMenu();
			QAction *found = nullptr;
			if (menu)
				for (QAction *action : menu->actions())
					if (action->text() == (view->laneFolded(lane) ? QStringLiteral("Open lane") : QStringLiteral("Fold lane")))
						found = action;
			return found;
		};
		QAction *fold = menuAction(0, QPointF(40, view->laneRect(0).center().y()));
		if (fold) fold->trigger();
		if (tab.laneMenu()) tab.laneMenu()->close();
		(void) view->grab();
		const bool taller = view->laneFolded(0) && view->laneRect(1).height() > openHeight + 20;
		QAction *reopen = menuAction(0, view->laneRect(0).center()); /* a right-click on the strip itself */
		if (reopen) reopen->trigger();
		if (tab.laneMenu()) tab.laneMenu()->close();
		(void) view->grab();
		const bool back = reopen && !view->laneFolded(0) && std::fabs(view->laneRect(1).height() - openHeight) < 0.01;
		if (!still || !taller || !back)
			std::printf("     (%d lanes, bar %s, scroll %g; open %g px, with one folded %g; the menu %s %s)\n",
					view->laneCount(), view->laneScrollBarRect().isEmpty() ? "none" : "shown", view->laneScroll(), openHeight,
					view->laneRect(1).height(), fold ? "folds" : "-", reopen ? "opens" : "-");
		check(still && fold && taller && back, "chart, Lanes that fit: with few lanes nothing scrolls and no bar shows; "
				"Fold lane in the lane's menu makes the others taller, Open lane (a right-click on the strip) back");
		tab.hide();
		QSettings().remove(group);
	}

	/* The fold made visible: a "▾" at the top of every open lane's unit column (a folded strip's "▸"), a click on
	 * either folds or opens; over them the pointing hand, the button highlighted and a tooltip; the hint in the state
	 * corner; Fold all / Open all lanes in the Display menu; the value labels' tooltip (the wheel's part only when the
	 * lanes scroll) */
	void chartLanesFoldButton() {
		const QString group = QStringLiteral("lanesButton");
		prepareLanesSettings(group);
		ChartTab tab([] { return 100.0; }, nullptr, group);
		MathLines::Samples samples;
		plotMapLanes(tab, &samples);
		tab.frame(samples);
		ChartView *view = tab.findChild<ChartView *>();
		view->showLastValues();
		tab.show();
		(void) QTest::qWaitForWindowExposed(&tab);
		(void) view->grab();
		const QRectF plot = view->laneScrollBarRect().adjusted(-1000, 0, 1000, 0); /* the plot's rows (not its x) */

		/* a button on every lane in view, at the top of its unit column; none for one out of view */
		bool buttons = view->laneCount() >= 10;
		for (int k = 0; k < view->laneCount() && buttons; k++) {
			const QRectF lane = view->laneRect(k), button = view->laneFoldButtonRect(k);
			const bool inView = lane.bottom() > plot.top() && lane.top() < plot.bottom();
			buttons = inView ? button.left() == 0 && button.right() <= 18 && std::fabs(button.top() - lane.top() - 1) < 0.01
					&& button.height() == 16 : button.isEmpty();
		}
		/* a button's shape at rest: not the background behind the unit column */
		const QImage rest = view->grab().toImage();
		const QColor surface = Theme::colors().surface, shape = rest.pixelColor(
				(view->laneFoldButtonRect(1).topLeft() + QPointF(4, 3)) .toPoint() * view->devicePixelRatioF());
		buttons = buttons && shape != surface;
		const QPoint button1 = view->laneFoldButtonRect(1).center().toPoint();
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, button1);
		(void) view->grab();
		const bool foldedByButton = view->laneFolded(1)
				&& std::fabs(view->laneFoldButtonRect(1).height() - ChartView::LANE_FOLDED_H) < 0.01;
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, view->laneFoldButtonRect(1).center().toPoint());
		(void) view->grab();
		check(buttons && foldedByButton && !view->laneFolded(1), "chart, Lanes: a fold button (▾) at the top of every "
				"lane's unit column in view; a click on it folds the lane, a click on the strip's ▸ opens it");

		/* the mouse over a button: the pointing hand, the button highlighted, the tooltip */
		const QRectF button2 = view->laneFoldButtonRect(2);
		/* the button's area in the picture's own pixels: grab() is in device pixels (2.25 x at 225 %) */
		const auto buttonPicture = [view, &button2] {
			const QImage whole = view->grab().toImage();
			const qreal dpr = whole.devicePixelRatio();
			const QRectF area = button2.adjusted(-1, -1, 1, 1);
			return whole.copy(QRectF(area.topLeft() * dpr, area.size() * dpr).toAlignedRect());
		};
		const QImage plain = buttonPicture();
		const auto moveTo = [view](QPointF at) {
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
		};
		moveTo(button2.center());
		const QImage lit = buttonPicture();
		const bool hand = view->cursor().shape() == Qt::PointingHandCursor && view->hoveredLane() == 2 && plain != lit;
		QHelpEvent help(QEvent::ToolTip, button2.center().toPoint(), view->mapToGlobal(button2.center().toPoint()));
		QApplication::sendEvent(view, &help);
		const bool tipShown = QTest::qWaitFor([] { return QToolTip::text() == QStringLiteral("Fold lane"); }, 2000);
		view->setLaneFolded(3, true);
		(void) view->grab();
		const bool stripTip = view->toolTipAt(view->laneRect(3).center()) == QStringLiteral("Open lane")
				&& view->toolTipAt(view->laneFoldButtonRect(3).center()) == QStringLiteral("Open lane");
		view->setLaneFolded(3, false);
		moveTo(view->laneRect(2).center()); /* over the plot */
		const bool unlit = view->hoveredLane() == -1;
		QToolTip::hideText();
		if (!hand || !tipShown || !stripTip || !unlit)
			std::printf("     (cursor %d, hovered %d, highlighted %d; tooltip \"%s\"; the strip's %d; off %d)\n",
					int(view->cursor().shape()), view->hoveredLane(), int(plain != lit), qPrintable(QToolTip::text()),
					int(stripTip), int(unlit));
		check(hand && tipShown && stripTip && unlit, "chart, Lanes: over a fold button the pointing hand, the button "
				"highlighted, the tooltip \"Fold lane\" (\"Open lane\" on a strip); away from it, no highlight");

		/* the lane's menu button ("⋯") under every open lane's fold button in view; none on a folded strip */
		bool menus = true;
		view->setLaneFolded(4, true);
		(void) view->grab();
		for (int k = 0; k < view->laneCount() && menus; k++) {
			const QRectF fold = view->laneFoldButtonRect(k), menu = view->laneMenuButtonRect(k);
			const bool inView = view->laneRect(k).top() >= plot.top() && view->laneRect(k).bottom() <= plot.bottom();
			menus = view->laneFolded(k) ? menu.isEmpty() /* a lane wholly in view has it; one cut, where there is room */
					: !inView || (menu.left() == 0 && menu.right() <= 18 && menu.height() == 16
							&& std::fabs(menu.top() - fold.bottom() - 2) < 0.01);
			if (!menus) std::printf("     (lane %d: fold %g,%g %gx%g, menu %g,%g %gx%g)\n", k, fold.x(), fold.y(),
					fold.width(), fold.height(), menu.x(), menu.y(), menu.width(), menu.height());
		}
		view->setLaneFolded(4, false);
		const QImage menuRest = view->grab().toImage();
		menus = menus && menuRest.pixelColor((view->laneMenuButtonRect(1).topLeft() + QPointF(4, 3)).toPoint()
				* view->devicePixelRatioF()) != surface;
		check(menus, "chart, Lanes: a menu button (⋯) under every open lane's fold button in view, in a button's shape; "
				"none on a folded strip");

		/* a click on it: the lane's menu (Auto, Manual…, Log, Fold lane) under the button; its Fold lane folds */
		const QRectF menu1 = view->laneMenuButtonRect(1);
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, menu1.center().toPoint());
		QMenu *laneMenu = tab.laneMenu();
		QStringList items;
		QAction *foldItem = nullptr;
		if (laneMenu)
			for (QAction *action : laneMenu->actions()) {
				if (!action->isSeparator()) items << action->text();
				if (action->text() == QStringLiteral("Fold lane")) foldItem = action;
			}
		const bool shown = laneMenu && laneMenu->isVisible() && !view->laneFolded(1)
				&& items == QStringList{ QStringLiteral("Auto"), QStringLiteral("Manual…"), QStringLiteral("Log"),
						QStringLiteral("All lanes: Auto"), QStringLiteral("Fold lane") }
				&& std::abs(laneMenu->pos().y() - view->mapToGlobal(menu1.bottomLeft().toPoint()).y()) <= 2;
		if (foldItem) foldItem->trigger();
		if (laneMenu) laneMenu->close();
		(void) view->grab();
		const bool menuFolds = view->laneFolded(1);
		view->setLaneFolded(1, false);
		(void) view->grab();
		if (!shown || !menuFolds)
			std::printf("     (the menu %s, at %d (the button's bottom %d); its items \"%s\"; folded %d)\n",
					laneMenu && laneMenu->isVisible() ? "shown" : "not shown", laneMenu ? laneMenu->pos().y() : -1,
					view->mapToGlobal(menu1.bottomLeft().toPoint()).y(), qPrintable(items.join(QStringLiteral(", "))),
					int(menuFolds));
		check(shown && menuFolds, "chart, Lanes: a click on a lane's ⋯ shows its menu under the button: Auto, Manual…, "
				"Log, All lanes: Auto, Fold lane (no fold by the click itself)");

		/* the mouse over it: the pointing hand, it highlighted (not the fold button), the tooltip */
		const QRectF menu2 = view->laneMenuButtonRect(2);
		const auto menuPicture = [view, &menu2] { /* in the picture's device pixels, as the fold button's */
			const QImage whole = view->grab().toImage();
			const qreal dpr = whole.devicePixelRatio();
			const QRectF area = menu2.adjusted(-1, -1, 1, 1);
			return whole.copy(QRectF(area.topLeft() * dpr, area.size() * dpr).toAlignedRect());
		};
		const QImage menuPlain = menuPicture();
		moveTo(menu2.center());
		const QImage menuLit = menuPicture();
		const bool menuHand = view->cursor().shape() == Qt::PointingHandCursor && view->hoveredLaneMenu() == 2
				&& view->hoveredLane() == -1 && menuPlain != menuLit;
		QHelpEvent menuHelp(QEvent::ToolTip, menu2.center().toPoint(), view->mapToGlobal(menu2.center().toPoint()));
		QApplication::sendEvent(view, &menuHelp);
		const bool menuTip = QTest::qWaitFor([] { return QToolTip::text() == QStringLiteral("Y range and lane options"); },
				2000);
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* the buttons, lane 2's ⋯ under the mouse, in both themes */
			for (const bool dark : { false, true }) {
				Theme::apply(*qApp, dark);
				view->grab().save(qEnvironmentVariable("EVRE_TEST_SHOT")
						+ (dark ? QStringLiteral("_lane_menu_dark.png") : QStringLiteral("_lane_menu_light.png")));
			}
		}
		moveTo(view->laneRect(2).center()); /* over the plot */
		const bool menuUnlit = view->hoveredLaneMenu() == -1;
		QToolTip::hideText();
		if (!menuHand || !menuTip || !menuUnlit)
			std::printf("     (cursor %d, menu hovered %d, fold hovered %d, highlighted %d; tooltip \"%s\"; off %d)\n",
					int(view->cursor().shape()), view->hoveredLaneMenu(), view->hoveredLane(), int(menuPlain != menuLit),
					qPrintable(QToolTip::text()), int(menuUnlit));
		check(menuHand && menuTip && menuUnlit, "chart, Lanes: over a lane's ⋯ the pointing hand, it highlighted, the "
				"tooltip \"Y range and lane options\"; away from it, no highlight");

		/* the scroll bar: the pointing hand, its handle brighter, a tooltip; away from it, as before */
		const QRectF handle = view->laneScrollHandleRect();
		const auto handlePicture = [view, &handle] { /* in the picture's device pixels, as the button's */
			const QImage whole = view->grab().toImage();
			const qreal dpr = whole.devicePixelRatio();
			return whole.copy(QRectF(handle.topLeft() * dpr, handle.size() * dpr).toAlignedRect());
		};
		const QImage handleRest = handlePicture();
		moveTo(handle.center());
		const QImage handleLit = handlePicture();
		const bool barHover = view->laneBarHovered() && view->cursor().shape() == Qt::PointingHandCursor
				&& handleRest != handleLit && view->toolTipAt(handle.center())
						== QStringLiteral("Scroll the lanes: drag the handle, or click above or below it for a page");
		if (!barHover)
			std::printf("     (the bar %s; hovered %d, cursor %d, the handle's pixels changed %d; tooltip \"%s\")\n",
					handle.isEmpty() ? "none" : "shown", int(view->laneBarHovered()), int(view->cursor().shape()),
					int(handleRest != handleLit), qPrintable(view->toolTipAt(handle.center())));
		moveTo(view->laneRect(2).center()); /* over the plot */
		check(barHover && !view->laneBarHovered(), "chart, Lanes: over the scroll bar the pointing hand, its handle "
				"brighter, a tooltip that says how it scrolls");

		/* the state corner says nothing for Lanes: their ▾ and ⋯ buttons show what they do */
		const bool hint = !view->stateText().contains(QStringLiteral("lanes"));
		check(hint, "chart, Lanes: the state corner says nothing for Lanes (their buttons show the fold)");

		/* Display: Fold all lanes / Open all lanes, with Lanes on, each enabled when it has something to do */
		auto *lanes = tab.findChild<QAction *>(QStringLiteral("chartLanes"));
		auto *foldAll = tab.findChild<QAction *>(QStringLiteral("chartFoldAll"));
		auto *openAll = tab.findChild<QAction *>(QStringLiteral("chartOpenAll"));
		bool all = lanes && foldAll && openAll && lanes->isChecked() && foldAll->isVisible() && openAll->isVisible()
				&& foldAll->isEnabled() && !openAll->isEnabled();
		if (all) {
			foldAll->trigger();
			(void) view->grab();
			all = view->foldedLaneCount() == view->laneCount() && !foldAll->isEnabled() && openAll->isEnabled()
					&& QSettings().value(group + QStringLiteral("/lanesFolded")).toStringList().size() == view->laneCount()
					&& view->laneScrollBarRect().isEmpty(); /* ten strips fit */
			openAll->trigger();
			(void) view->grab();
			all = all && view->foldedLaneCount() == 0 && foldAll->isEnabled() && !openAll->isEnabled()
					&& QSettings().value(group + QStringLiteral("/lanesFolded")).toStringList().isEmpty();
			lanes->setChecked(false);
			all = all && !foldAll->isVisible() && !openAll->isVisible();
			(void) view->grab();
			all = all && !view->stateText().contains(QStringLiteral("lanes"));
			lanes->setChecked(true);
		}
		if (!all && foldAll && openAll)
			std::printf("     (Fold all: %s %s; Open all: %s %s; %d of %d folded; the state \"%s\")\n",
					foldAll->isVisible() ? "shown" : "hidden", foldAll->isEnabled() ? "enabled" : "disabled",
					openAll->isVisible() ? "shown" : "hidden", openAll->isEnabled() ? "enabled" : "disabled",
					view->foldedLaneCount(), view->laneCount(), qPrintable(view->stateText()));
		check(all, "chart, Lanes: Display has Fold all lanes and Open all lanes with Lanes on (each enabled when it has "
				"something to do, the folds saved), none without");

		/* the value labels' tooltip: the wheel's part only while the lanes scroll */
		(void) view->grab();
		const QString scrolling = view->toolTipAt(QPointF(40, view->laneRect(0).center().y()));
		for (const RegDef &def : map_.regs)
			if (def.isNumeric() && def.unit != regs_.volts.unit && def.unit != regs_.amps.unit) tab.plotRegister(def, false);
		(void) view->grab();
		const QString fitting = view->toolTipAt(QPointF(40, view->laneRect(0).center().y()));
		const bool labelsTip = scrolling == QStringLiteral("Wheel: scroll the lanes · Ctrl + wheel: zoom this lane · "
				"Click: its Y range in the toolbar · Double-click: Auto · Right-click: its Y range and Fold lane")
				&& fitting == QStringLiteral("Ctrl + wheel: zoom this lane · Click: its Y range in the toolbar · "
						"Double-click: Auto · Right-click: its Y range and Fold lane");
		if (!labelsTip) std::printf("     (scrolling: \"%s\"; fitting: \"%s\")\n", qPrintable(scrolling), qPrintable(fitting));
		check(labelsTip, "chart, Lanes: the value labels' tooltip names the wheel (while the lanes scroll), Ctrl + wheel, "
				"the click, the double-click and the right-click");
		tab.hide();
		QSettings().remove(group);
	}

	/* A line between two lanes (open or folded), in the middle of the gap, from the value labels across the plot, in
	 * the colour of a control's edge (3:1 to the chart); only between lanes in view; none without Lanes */
	void chartLanesSeparators() {
		const QString group = QStringLiteral("lanesSeparators");
		prepareLanesSettings(group);
		ChartTab tab([] { return 100.0; }, nullptr, group);
		MathLines::Samples samples;
		plotMapLanes(tab, &samples);
		tab.frame(samples);
		ChartView *view = tab.findChild<ChartView *>();
		tab.show();
		(void) QTest::qWaitForWindowExposed(&tab);
		const QColor border = Theme::colors().control; /* a control's edge: 3:1 to the chart */
		/* the separators as painted: one per gap whose middle is in the plot, there; the border colour at the labels
		 * and over the plot */
		const auto where = [&](QString &why) {
			const QImage picture = view->grab().toImage();
			const qreal dpr = view->devicePixelRatioF();
			const QRectF plot = QRectF(view->laneRect(0).left(), view->laneScrollBarRect().top(), view->laneRect(0).width(),
					view->laneScrollBarRect().height());
			QVector<double> want;
			for (int k = 0; k + 1 < view->laneCount(); k++) {
				const double y = view->laneRect(k).bottom() + 5;
				if (y >= plot.top() && y <= plot.bottom()) want << y;
			}
			const QVector<double> drawn = view->laneSeparators();
			if (drawn.size() != want.size() || want.isEmpty()) {
				why = QStringLiteral("%1 drawn, %2 gaps").arg(drawn.size()).arg(want.size());
				return false;
			}
			for (int i = 0; i < want.size(); i++) {
				if (std::fabs(drawn[i] - want[i]) > 0.01) {
					why = QStringLiteral("at %1, the gap's middle %2").arg(drawn[i]).arg(want[i]);
					return false;
				}
				for (const double x : { 30.0, plot.left() + 3, plot.center().x() }) {
					bool found = false;
					for (int dy = -1; dy <= 1 && !found; dy++) {
						const QColor c = picture.pixelColor(int(x * dpr), int(std::floor(drawn[i] * dpr)) + dy);
						found = std::abs(c.red() - border.red()) + std::abs(c.green() - border.green())
								+ std::abs(c.blue() - border.blue()) <= 6;
					}
					if (!found) {
						why = QStringLiteral("not the separator's colour at x %1, y %2").arg(x).arg(drawn[i]);
						return false;
					}
				}
			}
			return true;
		};
		QString why;
		bool ok = where(why); /* ten lanes, the last ones below the plot */
		const int atTop = view->laneSeparators().size();
		view->setLaneFolded(1, true); /* a strip has them too */
		ok = ok && where(why) && view->laneSeparators().size() >= atTop;
		view->setLaneScroll(ChartView::LANE_MIN_H / 2 + 3); /* scrolled: the gaps where the lanes are now */
		ok = ok && where(why);
		view->setLaneFolded(1, false);
		view->setLanes(false);
		(void) view->grab();
		ok = ok && view->laneSeparators().isEmpty();
		if (!ok) std::printf("     (separators: %s)\n", qPrintable(why));
		check(ok, "chart, Lanes: a line in the middle of each gap between two lanes in view (a folded one's too, scrolled "
				"too), from the value labels across the plot, in a control's edge colour (3:1); none without Lanes");

		/* no text cut or run together: scrolled so the first lane is cut by the plot's top, every value label whole
		 * inside its lane's part in view (none across a gap into the next lane's); a folded strip cut by the edge
		 * writes nothing, whole it writes its lines */
		view->setLanes(true);
		view->setLaneScroll(ChartView::LANE_MIN_H / 2 + 3);
		(void) view->grab();
		const QRectF plotRows = view->laneScrollBarRect();
		bool whole = !view->valueLabelRects().isEmpty();
		for (const QRectF &label : view->valueLabelRects()) {
			bool inside = false;
			for (int k = 0; k < view->laneCount() && !inside; k++) {
				const QRectF lane = view->laneRect(k);
				const double top = std::max(lane.top(), plotRows.top()), bottom = std::min(lane.bottom(), plotRows.bottom());
				inside = label.top() >= top - 0.01 && label.bottom() <= bottom + 0.01;
			}
			whole = whole && inside;
		}
		view->setLaneFolded(0, true);
		view->setLaneScroll(10); /* the strip (22 px) half above the plot */
		(void) view->grab();
		const bool cutStrip = view->laneRect(0).top() < plotRows.top() && view->foldedText(0).isEmpty();
		view->setLaneScroll(0);
		(void) view->grab();
		const bool wholeStrip = !view->foldedText(0).isEmpty();
		view->setLaneFolded(0, false);
		if (!whole || !cutStrip || !wholeStrip)
			std::printf("     (labels whole in their lanes %d; a cut strip silent %d, a whole one written %d)\n", int(whole),
					int(cutStrip), int(wholeStrip));
		check(whole && cutStrip && wholeStrip, "chart, Lanes: no text cut by the plot's edge or run into the next lane: "
				"the value labels stay whole inside their lane's part in view, a strip cut by the edge writes nothing");
		tab.hide();
		QSettings().remove(group);
	}

	/* The state corner fits its room: with the view held, Y log manual, cursors and a trigger on, its text is shortened
	 * by whole parts in their order (Live to follow, click / drag, manual) as the chart narrows, down to the width it has
	 * at the main window's narrowest; the legend ends where it begins, so neither lies over the other; its tooltip is
	 * the whole text; with room nothing is dropped. In English and in Arabic (longer). */
	void chartStateFits() {
		/* the chart's width in the main window at its narrowest */
		const QSize before = window_.size();
		window_.resize(window_.minimumSizeHint().width(), window_.height());
		QApplication::processEvents();
		ChartView *mainChart = window_.findChild<ChartView *>();
		const int narrow = mainChart && mainChart->width() > 300 ? mainChart->width() : 700;
		window_.resize(before);
		QApplication::processEvents();
		const QString sep = QStringLiteral("  ·  ");
		struct Run {
			bool apart = true, ordered = true, roomy = false, shortened = false, tip = false;
			QStringList seen;
		};
		const auto run = [&](const QString &code) {
			language::apply(*qApp, code);
			const auto inChart = [](const char *text) { return QCoreApplication::translate("ChartView", text); };
			Run out;
			LoneChart chart(QStringLiteral("STATE0"), QStringLiteral("V"));
			MathLines::Samples samples;
			for (int k = 0; k < 8; k++) {
				RegDef def = chart.def;
				def.addr = quint16(0xD000 + 2 * k);
				def.name = QStringLiteral("STATE%1").arg(k);
				if (k > 0) chart.tab.plotRegister(def, true);
				for (int i = 0; i < 100; i++) samples[regKey(def)] << QPointF(90.0 + i * 0.1, 1 + k + std::sin(i * 0.1));
			}
			chart.tab.frame(samples);
			chart.tab.show();
			(void) QTest::qWaitForWindowExposed(&chart.tab);
			ChartView *view = chart.view;
			view->setYLog(true);
			view->setYManual(0.5, 20);
			view->setCursorMode(true);
			(void) view->grab();
			view->setLive(false);
			const int margin = chart.tab.width() - view->width();
			chart.tab.resize(1700 + margin, 700);
			(void) view->grab();
			/* the parts as the state writes them, the time held taken from the whole text */
			const QString full = view->stateFullText();
			const QString heldWhole = inChart("held: -%1 s · Live to follow");
			const QString held = full.section(sep, 0, 0), before = heldWhole.section(QLatin1String("%1"), 0, 0),
					after = heldWhole.section(QLatin1String("%1"), 1);
			const QString number = held.startsWith(before) && held.endsWith(after)
					? held.mid(before.size(), held.size() - before.size() - after.size()) : QString();
			const QStringList stages{
				QStringList{ heldWhole.arg(number), inChart("Y log, manual"), inChart("cursors: click / drag") }.join(sep),
				QStringList{ inChart("held: -%1 s").arg(number), inChart("Y log, manual"), inChart("cursors: click / drag") }.join(sep),
				QStringList{ inChart("held: -%1 s").arg(number), inChart("Y log, manual"), inChart("cursors") }.join(sep),
				QStringList{ inChart("held: -%1 s").arg(number), inChart("Y log"), inChart("cursors") }.join(sep) };
			int last = 0;
			const int narrowest = std::min(narrow, 760); /* narrower still, so every part has to go in turn */
			for (int width = 1700; width >= narrowest; width -= 10) {
				chart.tab.resize(width + margin, 700);
				(void) view->grab();
				const QString text = view->stateText();
				const int stage = int(stages.indexOf(text));
				const QRectF state = view->stateRect(), legend = view->legendViewport();
				const QString entry = QStringLiteral("%1: %2").arg(stage).arg(text);
				if (out.seen.isEmpty() || out.seen.last() != entry) out.seen << entry;
				out.ordered = out.ordered && stage >= last;
				last = std::max(last, stage);
				/* at most 40 % of the chart, or the shortest text whole (never cut) while the legend keeps a chip */
				const bool apart = !state.isEmpty() && !state.intersects(legend) && state.left() >= legend.right()
						&& (state.width() <= view->width() * 0.4 + 1 || stage == 3) && legend.width() >= 100;
				if (!apart && out.apart)
					std::printf("     (%s at %d px: the state %g..%g, the legend %g..%g)\n", qPrintable(code), width,
							state.left(), state.right(), legend.left(), legend.right());
				out.apart = out.apart && apart;
				if (width == 1700) {
					out.roomy = text == full && full == stages[0];
					out.tip = view->toolTipAt(state.center()) == full;
				}
				/* without the trigger's part (alone in the corner while the trigger is on) the text needs no more than
				 * the cursors' "click / drag" dropped at the narrowest */
				if (width - 10 < narrowest) out.shortened = stage >= 2 && view->toolTipAt(state.center()) == full;
			}
			if (!out.ordered || !out.roomy || !out.shortened)
				std::printf("     (%s, %d px at the narrowest: the whole \"%s\"; seen: \"%s\")\n", qPrintable(code), narrow,
						qPrintable(full), qPrintable(out.seen.join(QStringLiteral("\" | \""))));
			if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* at the main window's narrowest, in both themes */
				chart.tab.resize(narrow + margin, 700);
				for (const bool dark : { false, true }) {
					Theme::apply(*qApp, dark);
					view->grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_state_%1_%2.png")
							.arg(code, dark ? QStringLiteral("dark") : QStringLiteral("light")));
				}
			}
			chart.tab.hide();
			language::apply(*qApp, QStringLiteral("en"));
			return out;
		};
		const Run english = run(QStringLiteral("en"));
		std::printf("     (the chart %d px wide at the main window's narrowest; English: \"%s\")\n", narrow,
				qPrintable(english.seen.join(QStringLiteral("\" | \""))));
		check(english.apart, "chart, state corner: from 1700 px down to the main window's narrowest, with the view held, Y "
				"log manual and cursors, its text never lies over the legend (the arrows included)");
		check(english.ordered && english.roomy && english.shortened, "chart, state corner: shortened by whole parts in "
				"turn (Live to follow, click / drag, manual), the time held kept; nothing dropped with room");
		check(english.tip, "chart, state corner: its tooltip is the whole text, also when shortened");
		const Run arabic = run(QStringLiteral("ar"));
		std::printf("     (Arabic: \"%s\")\n", qPrintable(arabic.seen.join(QStringLiteral("\" | \""))));
		check(arabic.apart && arabic.ordered && arabic.roomy && arabic.shortened && arabic.tip, "chart, state corner, "
				"Arabic: the same, its longer words shortened in the same order, never over the legend");
	}


	/* P7b: every lane's Y range seen and set on its own. A lane not in Auto has a tag at the top of its value labels
	 * ("Manual" in the warn colour, "Log"); a click on it: Auto. A click on a lane's value labels (its ⋯, its tag)
	 * makes it the current lane, whose range the toolbar's Y range shows and sets (its list: "A"); Display and each
	 * lane's ⋯ have All lanes: Auto; a double-click on the labels: Auto; a manual lane comes back tagged */
	void chartLaneRanges() {
		const QString group = QStringLiteral("laneRanges");
		QSettings().remove(group);
		QSettings().setValue(group + QStringLiteral("/lanes"), true);
		QVector<RegDef> defs;
		MathLines::Samples samples;
		for (int k = 0; k < 2; k++) { /* a line in V around 12 and one in A, a sine from 3 to 17 */
			RegDef def;
			def.addr = uint16_t(0xD100 + 2 * k);
			def.name = k == 0 ? QStringLiteral("BUS_V") : QStringLiteral("LOAD_I");
			def.unit = k == 0 ? QStringLiteral("V") : QStringLiteral("A");
			defs << def;
			for (int i = 0; i < 4000; i++)
				samples[regKey(def)] << QPointF(90.0 + i * 0.0025, k == 0 ? 12 + std::sin(i * 0.01) : 10 + 7 * std::sin(i * 0.01));
		}
		const auto plot = [&](ChartTab &tab) {
			for (const RegDef &def : std::as_const(defs)) tab.plotRegister(def, true);
			tab.frame(samples);
		};
		ChartTab tab([] { return 100.0; }, nullptr, group);
		tab.resize(1200, 700);
		plot(tab);
		ChartView *view = tab.findChild<ChartView *>();
		view->setWindow(10);
		tab.show();
		(void) QTest::qWaitForWindowExposed(&tab);
		auto *mode = tab.findChild<QComboBox *>(QStringLiteral("yMode"));
		auto *low = tab.findChild<QLineEdit *>(QStringLiteral("yMin"));
		auto *high = tab.findChild<QLineEdit *>(QStringLiteral("yMax"));
		auto *allAuto = tab.findChild<QAction *>(QStringLiteral("chartAllLanesAuto"));
		auto *lanes = tab.findChild<QAction *>(QStringLiteral("chartLanes"));
		QLabel *rangeLabel = nullptr;
		for (QLabel *label : tab.findChildren<QLabel *>())
			if (label->text().startsWith(QStringLiteral("Y range"))) rangeLabel = label;
		(void) view->grab();
		if (!mode || !low || !high || !allAuto || !lanes || !rangeLabel || view->laneCount() != 2) {
			check(false, "chart, a lane's Y range: two lanes, the toolbar's Y range and All lanes: Auto found");
			return;
		}
		const QRectF plotArea(view->laneRect(0).left(), view->laneRect(0).top(), view->laneRect(0).width(),
				view->laneRect(1).bottom() - view->laneRect(0).top());

		/* the tag: none in Auto; "Manual" for a manual lane, in the warn colour, in its value labels' column at its top,
		 * no value label under it, a tooltip with its range */
		const bool noTag = view->laneRangeTagRect(0).isEmpty() && view->laneRangeTagRect(1).isEmpty()
				&& view->laneRangeTagText(1).isEmpty();
		view->setLaneYManual(1, 4.94, 17.14);
		QImage picture = view->grab().toImage();
		qreal dpr = picture.devicePixelRatio();
		const QRectF tag = view->laneRangeTagRect(1);
		const QColor warn = Theme::colors().warn;
		const auto near = [](QColor a, QColor b, int most) {
			return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue()) < most;
		};
		int warnPixels = 0;
		for (int y = int(tag.top() * dpr); y < int(tag.bottom() * dpr); y++)
			for (int x = int(tag.left() * dpr); x < int(tag.right() * dpr); x++)
				if (near(picture.pixelColor(x, y), warn, 90)) warnPixels++;
		bool clear = true; /* the labels are right-aligned in the column: none at the tag's height */
		for (const QRectF &label : view->valueLabelRects())
			if (label.top() < tag.bottom() && label.bottom() > tag.top()) clear = false;
		const QString tip = view->toolTipAt(tag.center());
		const bool tagged = noTag && view->laneRangeTagText(1) == QStringLiteral("Manual") && view->laneRangeTagRect(0).isEmpty()
				&& tag.left() >= 18 && tag.right() <= plotArea.left() && std::fabs(tag.top() - view->laneRect(1).top() - 1) < 0.01
				&& warnPixels > 4 && clear
				&& tip == QStringLiteral("This lane's Y range is manual: 4.94 to 17.1 A · Click: back to Auto");
		if (!tagged)
			std::printf("     (no tag in Auto %d; the tag \"%s\" at %g,%g %gx%g, %d warn pixels, labels clear %d; tooltip \"%s\")\n",
					int(noTag), qPrintable(view->laneRangeTagText(1)), tag.x(), tag.y(), tag.width(), tag.height(), warnPixels,
					int(clear), qPrintable(tip));
		check(tagged, "chart, a lane's Y range: a manual lane has a \"Manual\" tag in the warn colour at the top of its value "
				"labels (none under it), its tooltip its range and \"Click: back to Auto\"; none in Auto");

		/* the current lane: the first by default, the toolbar's Y range its own with its unit; a click on the other's value
		 * labels: that one, its unit name lit, the toolbar its range at once */
		/* the label follows the lanes with the info line (the window's status, twice a second): the lines came after
		 * Lanes was on */
		tab.refreshStatus();
		auto *unitBox = tab.findChild<QComboBox *>(QStringLiteral("yLane"));
		const bool first = rangeLabel->text() == QStringLiteral("Y range") && unitBox && unitBox->currentText() == QStringLiteral("V")
				&& view->currentLane() == 0
				&& mode->isEnabled() && mode->currentIndex() == 0;
		if (!first)
			std::printf("     (at first: lane %d, \"%s\", mode %d %s)\n", view->currentLane(), qPrintable(rangeLabel->text()),
					mode->currentIndex(), mode->isEnabled() ? "enabled" : "disabled");
		const QPoint labels1(40, int(view->laneRect(1).bottom() - 20));
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, labels1);
		picture = view->grab().toImage();
		const QColor accent = Theme::colors().accent;
		const auto accentIn = [&](int lane) { /* the unit name's column under the lane's buttons */
			const QRectF r = view->laneRect(lane);
			int n = 0;
			for (int y = int((r.top() + 40) * dpr); y < int(r.bottom() * dpr); y++)
				for (int x = 0; x < int(18 * dpr); x++)
					if (near(picture.pixelColor(x, y), accent, 90)) n++;
			return n;
		};
		const bool current = view->currentLane() == 1 && unitBox->currentText() == QStringLiteral("A")
				&& mode->currentIndex() == 1 && low->text() == QStringLiteral("4.94") && high->text() == QStringLiteral("17.14")
				&& accentIn(1) > 3 && accentIn(0) * 4 < accentIn(1); /* lit: its name in the accent, the other's not */
		/* the toolbar sets it: 1 .. 20 typed, then Auto chosen; the first lane untouched */
		low->setText(QStringLiteral("1"));
		high->setText(QStringLiteral("20"));
		emit high->editingFinished();
		const bool typed = !view->laneYAuto(1) && view->laneYLo(1) == 1 && view->laneYHi(1) == 20 && view->laneYAuto(0);
		mode->setCurrentIndex(0);
		emit mode->activated(0);
		(void) view->grab();
		const bool toAuto = view->laneYAuto(1) && view->laneRangeTagRect(1).isEmpty() && view->laneYAuto(0);
		/* the ⋯ button makes its lane current too */
		const QRectF menu0 = view->laneMenuButtonRect(0);
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, menu0.center().toPoint());
		if (tab.laneMenu()) tab.laneMenu()->close();
		const bool byMenu = view->currentLane() == 0 && unitBox->currentText() == QStringLiteral("V");
		if (!first || !current || !typed || !toAuto || !byMenu)
			std::printf("     (first %d; after the click: lane %d, \"%s\", mode %d, %s .. %s, lit %d/%d; typed %d; Auto %d; by ⋯ %d)\n",
					int(first), view->currentLane(), qPrintable(rangeLabel->text()), mode->currentIndex(), qPrintable(low->text()),
					qPrintable(high->text()), accentIn(1), accentIn(0), int(typed), int(toAuto), int(byMenu));
		check(first && current && typed && toAuto && byMenu, "chart, a lane's Y range: the current lane (the first by "
				"default) drives the toolbar's Y range, its unit chosen in the list; a click on another lane's value labels (or "
				"its ⋯) makes it current, its unit name lit, the toolbar its range at once; typing and Auto there set that "
				"lane alone");

		/* the tag's click: Auto (and linear); over it the pointing hand and the tag lit */
		view->setLaneYManual(1, 4.94, 17.14);
		(void) view->grab();
		const QRectF tag1 = view->laneRangeTagRect(1);
		const auto tagPicture = [&] {
			const QImage whole = view->grab().toImage();
			return whole.copy(QRectF(tag1.topLeft() * dpr, tag1.size() * dpr).toAlignedRect());
		};
		const QImage rest = tagPicture();
		QMouseEvent move(QEvent::MouseMove, tag1.center(), view->mapToGlobal(tag1.center()), Qt::NoButton, Qt::NoButton,
				Qt::NoModifier);
		QApplication::sendEvent(view, &move);
		const bool hover = view->hoveredRangeTag() == 1 && view->cursor().shape() == Qt::PointingHandCursor && tagPicture() != rest;
		view->setLaneYLog(0, true); /* the first lane Log: its tag "Log" */
		(void) view->grab();
		const bool logTag = view->laneRangeTagText(0) == QStringLiteral("Log");
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, tag1.center().toPoint());
		(void) view->grab();
		const bool clicked = view->laneYAuto(1) && !view->laneYLog(1) && view->laneRangeTagRect(1).isEmpty()
				&& view->currentLane() == 1 && mode->currentIndex() == 0;
		if (!hover || !logTag || !clicked)
			std::printf("     (hovered %d, cursor %d, lit %d; the Log tag \"%s\"; after the click: auto %d, tag %s, current %d, mode %d)\n",
					view->hoveredRangeTag(), int(view->cursor().shape()), int(tagPicture() != rest),
					qPrintable(view->laneRangeTagText(0)), int(view->laneYAuto(1)),
					view->laneRangeTagRect(1).isEmpty() ? "gone" : "shown", view->currentLane(), mode->currentIndex());
		check(hover && logTag && clicked, "chart, a lane's Y range: over the tag the pointing hand and the tag lit; a Log "
				"lane's tag says \"Log\"; a click on the tag sets that lane to Auto (linear) and makes it current");

		/* All lanes: Auto, in Display (shown with Lanes, enabled while a lane is not Auto) and in each lane's ⋯ */
		view->setLaneYManual(1, 0, 5);
		const bool enabled = allAuto->isVisible() && allAuto->isEnabled();
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, view->laneMenuButtonRect(1).center().toPoint());
		QAction *menuAll = nullptr;
		if (tab.laneMenu())
			for (QAction *action : tab.laneMenu()->actions())
				if (action->text() == QStringLiteral("All lanes: Auto")) menuAll = action;
		const bool inMenu = menuAll && menuAll->isEnabled();
		if (tab.laneMenu()) tab.laneMenu()->close();
		allAuto->trigger();
		(void) view->grab();
		const bool allDone = view->allLanesYAuto() && view->laneYAuto(0) && !view->laneYLog(0) && view->laneYAuto(1)
				&& !allAuto->isEnabled() && view->laneRangeTagRect(0).isEmpty() && view->laneRangeTagRect(1).isEmpty();
		lanes->setChecked(false);
		const bool hidden = !allAuto->isVisible() && rangeLabel->text() == QStringLiteral("Y range") && !unitBox->isVisible();
		lanes->setChecked(true);
		(void) view->grab();
		if (!enabled || !inMenu || !allDone || !hidden)
			std::printf("     (Display's entry %s %s; the ⋯ menu's %s; all Auto %d, then %s; without Lanes hidden %d, \"%s\")\n",
					allAuto->isVisible() ? "shown" : "hidden", enabled ? "enabled" : "disabled",
					menuAll ? (menuAll->isEnabled() ? "enabled" : "disabled") : "missing", int(allDone),
					allAuto->isEnabled() ? "enabled" : "disabled", int(hidden), qPrintable(rangeLabel->text()));
		check(enabled && inMenu && allDone && hidden, "chart, a lane's Y range: All lanes: Auto in Display (with Lanes, "
				"enabled while a lane is not Auto) and in each lane's ⋯ menu sets every lane to Auto");

		/* a double-click on a lane's value labels: Auto; their tooltip says so */
		view->setLaneYManual(0, 11, 13);
		(void) view->grab();
		const QPoint labels0(40, int(view->laneRect(0).bottom() - 20));
		const QString labelsTip = view->toolTipAt(labels0);
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, labels0); /* a double-click's first click (QTest sends */
		QTest::mouseDClick(view, Qt::LeftButton, Qt::NoModifier, labels0); /* the second alone) */
		const bool doubled = view->laneYAuto(0) && labelsTip.contains(QStringLiteral("Double-click: Auto"))
				&& labelsTip.contains(QStringLiteral("Click: its Y range in the toolbar"));
		if (!doubled) std::printf("     (after the double-click auto %d; the tooltip \"%s\")\n", int(view->laneYAuto(0)),
				qPrintable(labelsTip));
		check(doubled, "chart, a lane's Y range: a double-click on a lane's value labels sets it to Auto; their tooltip "
				"names the click (the toolbar) and the double-click");

		/* the lane list beside "Y range": hidden without Lanes; with them every lane by its unit as on the chart, the
		 * current one chosen; choosing another makes it current (its unit lit, the toolbar its range), a click on the
		 * chart's lane moves the list; a folded lane marked, a lane gone leaves it */
		QComboBox *laneBox = unitBox;
		const auto listed = [&] {
			QStringList items;
			for (int k = 0; laneBox && k < laneBox->count(); k++) items << laneBox->itemText(k);
			return items;
		};
		lanes->setChecked(false);
		const bool boxHidden = laneBox && !laneBox->isVisible() && rangeLabel->text() == QStringLiteral("Y range");
		lanes->setChecked(true);
		view->setLaneYManual(1, 4.94, 17.14);
		(void) view->grab();
		tab.refreshStatus();
		const bool both = laneBox && laneBox->isVisible() && listed() == QStringList({ QStringLiteral("V"), QStringLiteral("A") })
				&& laneBox->currentIndex() == view->currentLane() && view->currentLane() == 0
				&& laneBox->toolTip() == QStringLiteral("The lane these Y settings apply to · or click a lane's values on the chart");
		if (laneBox) {
			laneBox->setCurrentIndex(1);
			emit laneBox->activated(1);
		}
		picture = view->grab().toImage();
		const bool chosen = view->currentLane() == 1 && mode->currentIndex() == 1 && low->text() == QStringLiteral("4.94")
				&& high->text() == QStringLiteral("17.14") && accentIn(1) > 3 && accentIn(0) * 4 < accentIn(1);
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, QPoint(40, int(view->laneRect(0).bottom() - 20)));
		const bool follows = view->currentLane() == 0 && laneBox && laneBox->currentIndex() == 0 && mode->currentIndex() == 0;
		view->setLaneFolded(1, true);
		const bool marked = listed() == QStringList({ QStringLiteral("V"), QStringLiteral("A (folded)") });
		view->setLaneFolded(1, false);
		RegDef power; /* a third unit comes and goes */
		power.addr = 0xD104;
		power.name = QStringLiteral("LOAD_P");
		power.unit = QStringLiteral("W");
		MathLines::Samples powered;
		for (int i = 0; i < 4000; i++) powered[regKey(power)] << QPointF(90.0 + i * 0.0025, 120 + 80 * std::sin(i * 0.01));
		tab.plotRegister(power, true);
		tab.frame(powered);
		tab.refreshStatus();
		const bool added = listed() == QStringList({ QStringLiteral("V"), QStringLiteral("A"), QStringLiteral("W") });
		tab.plotRegister(power, false);
		tab.refreshStatus();
		const bool removed = listed() == QStringList({ QStringLiteral("V"), QStringLiteral("A") });
		if (!boxHidden || !both || !chosen || !follows || !marked || !added || !removed)
			std::printf("     (hidden without Lanes %d; listed \"%s\", current %d of the list, %d of the chart; chosen: lane %d, "
					"mode %d, %s .. %s, lit %d/%d; a chart click %d; folded %d; added %d; removed %d)\n", int(boxHidden),
					qPrintable(listed().join(QLatin1Char('|'))), laneBox ? laneBox->currentIndex() : -1, view->currentLane(),
					view->currentLane(), mode->currentIndex(), qPrintable(low->text()), qPrintable(high->text()), accentIn(1),
					accentIn(0), int(follows), int(marked), int(added), int(removed));
		check(boxHidden && both && chosen && follows && marked && added && removed, "chart, a lane's Y range: the lane list "
				"beside \"Y range\" (hidden without Lanes) lists every lane by its unit, the current one chosen, a folded one "
				"marked; choosing one makes it current (its unit lit, the toolbar its range); a click on the chart's lane moves "
				"it; a lane gone leaves it; its tooltip");

		/* kept: a manual lane is tagged again in a new tab (the next start) */
		view->setLaneYManual(1, 4.94, 17.14);
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* the tag, the current lane, the toolbar's unit, both themes */
			QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, QPoint(40, int(view->laneRect(1).bottom() - 20)));
			const bool wasDark = Theme::isDark();
			for (const bool dark : { false, true }) {
				Theme::apply(*qApp, dark);
				QApplication::processEvents();
				tab.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT")
						+ (dark ? QStringLiteral("_lane_range_dark.png") : QStringLiteral("_lane_range_light.png")));
			}
			Theme::apply(*qApp, wasDark);
		}
		tab.hide();
		bool again = false;
		{
			ChartTab other([] { return 100.0; }, nullptr, group);
			other.resize(1200, 700);
			plot(other);
			other.show(); /* laid out: the lanes their heights */
			(void) QTest::qWaitForWindowExposed(&other);
			auto *otherView = other.findChild<ChartView *>();
			if (otherView) {
				otherView->setWindow(10);
				(void) otherView->grab();
				again = otherView->lanes() && otherView->laneRangeTagText(1) == QStringLiteral("Manual")
						&& otherView->laneRangeTagText(0).isEmpty() && otherView->laneYHi(1) == 17.14;
				if (!again)
					std::printf("     (the new tab: lanes %d, %d of them, tags \"%s\" \"%s\", the second %g .. %g)\n",
							int(otherView->lanes()), otherView->laneCount(), qPrintable(otherView->laneRangeTagText(0)),
							qPrintable(otherView->laneRangeTagText(1)), otherView->laneYLo(1), otherView->laneYHi(1));
			}
			other.hide();
		}
		check(again, "chart, a lane's Y range: a manual lane kept (laneY) comes back tagged in a new tab");
		QSettings().remove(group);
	}

	/* A lane's border dragged: over a separator the resize cursor, the line lit, a tooltip; a drag gives the lane above
	 * what the one below gives up, neither under LANE_MIN_H; the heights kept by unit (laneHeights), a new tab finds
	 * them; a double-click on a separator: all equal again */
	void chartLaneBorders() {
		const QString group = QStringLiteral("laneBorders");
		prepareLanesSettings(group);
		const auto fourLanes = [this](ChartTab &tab) { /* V, A and the math lines' W and Ω: four lanes that fit */
			plotMapLanes(tab, nullptr);
			for (const RegDef &def : map_.regs)
				if (def.isNumeric() && def.unit != regs_.volts.unit && def.unit != regs_.amps.unit) tab.plotRegister(def, false);
		};
		ChartTab tab([] { return 100.0; }, nullptr, group);
		fourLanes(tab);
		ChartView *view = tab.findChild<ChartView *>();
		tab.show();
		(void) QTest::qWaitForWindowExposed(&tab);
		(void) view->grab();
		const QVector<double> separators = view->laneSeparators();
		if (view->laneCount() != 4 || separators.size() != 3 || !view->laneScrollBarRect().isEmpty()) {
			check(false, "chart, a lane's border: four lanes that fit, three separators");
			return;
		}
		const double x = view->laneRect(0).center().x();
		const auto moveTo = [view](QPointF at, Qt::MouseButtons buttons) {
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, buttons, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
		};
		/* over the first separator: the resize cursor, the line lit (the accent colour), the tooltip */
		const QPointF on(x, separators[0]);
		moveTo(on, Qt::NoButton);
		const QImage lit = view->grab().toImage();
		const qreal dpr = view->devicePixelRatioF();
		const QColor accent = Theme::colors().accent;
		bool accentLine = false;
		for (int dy = -1; dy <= 1 && !accentLine; dy++) {
			/* beside the mouse: the crosshair's line is at its x */
			const QColor c = lit.pixelColor(int((x + 40) * dpr), int(std::floor(separators[0] * dpr)) + dy);
			accentLine = std::abs(c.red() - accent.red()) + std::abs(c.green() - accent.green()) + std::abs(c.blue() - accent.blue()) <= 6;
		}
		const bool hover = view->cursor().shape() == Qt::SizeVerCursor && view->hoveredSeparator() == 0 && accentLine
				&& view->toolTipAt(on) == QStringLiteral("Drag: this lane's height · Double-click: equal heights");
		moveTo(view->laneRect(1).center(), Qt::NoButton);
		const bool off = view->hoveredSeparator() == -1 && view->cursor().shape() != Qt::SizeVerCursor;
		if (!hover || !off)
			std::printf("     (over the separator: cursor %d, hovered %d, lit %d, tooltip \"%s\"; off it: %d)\n",
					int(view->cursor().shape()), view->hoveredSeparator(), int(accentLine), qPrintable(view->toolTipAt(on)),
					int(off));
		check(hover && off, "chart, a lane's border: over a separator the resize cursor, the line lit, the tooltip "
				"\"Drag: this lane's height · Double-click: equal heights\"; off it, none of them");

		/* dragged 30 px down: the first lane 30 px taller, the second 30 px lower, the others as they were; far down
		 * and far up: held at LANE_MIN_H */
		const double h0 = view->laneRect(0).height(), h1 = view->laneRect(1).height(), h2 = view->laneRect(2).height();
		const auto drag = [&](double by) {
			const double from = view->laneSeparators().value(0);
			QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, QPoint(int(x), int(std::lround(from))));
			moveTo(QPointF(x, std::lround(from) + by), Qt::LeftButton);
			QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(int(x), int(std::lround(from) + by)));
			(void) view->grab();
		};
		drag(30);
		const bool moved = std::fabs(view->laneRect(0).height() - (h0 + 30)) < 0.6 && std::fabs(view->laneRect(1).height() - (h1 - 30)) < 0.6
				&& std::fabs(view->laneRect(2).height() - h2) < 0.6 && view->laneScrollBarRect().isEmpty();
		const QStringList saved = QSettings().value(group + QStringLiteral("/laneHeights")).toStringList();
		const double movedH0 = view->laneRect(0).height();
		drag(1000);
		const bool heldBelow = std::fabs(view->laneRect(1).height() - ChartView::LANE_MIN_H) < 0.6;
		drag(-2000);
		const bool heldAbove = std::fabs(view->laneRect(0).height() - ChartView::LANE_MIN_H) < 0.6;
		if (!moved || !heldBelow || !heldAbove || saved.size() != 2)
			std::printf("     (lanes %g %g %g -> %g; far down the second %g, far up the first %g; saved %s)\n", h0, h1, h2,
					movedH0, view->laneRect(1).height(), view->laneRect(0).height(), qPrintable(saved.join(QStringLiteral(", "))));
		check(moved && heldBelow && heldAbove && saved.size() == 2, "chart, a lane's border dragged: the lane above "
				"taller by as much as the one below is lower, the others as they were; neither under 80 px; saved by unit");

		/* weights that would run past the plot (O-11): the first lane twenty times the others, whose share is then under
		 * 80 px: they are held at 80 and the first takes the rest, every lane in the plot, nothing to scroll (a lane held
		 * at the minimum on top of the shares ran the last one below the plot). Lanes that do not fit at 80 px each:
		 * all at 80, scrolled, as before */
		{
			const QStringList kept = view->laneHeights();
			QStringList weights;
			for (int i = 0; i < view->laneCount(); i++)
				weights << view->laneLabel(i) + QLatin1Char('\t') + (i == 0 ? QStringLiteral("6") : QStringLiteral("0.3"));
			view->setLaneHeights(weights);
			(void) view->grab();
			const auto lowest = [view] {
				double h = 1e9;
				for (int i = 0; i < view->laneCount(); i++) h = std::min(h, view->laneRect(i).height());
				return h;
			};
			const double plotH = view->laneRect(3).bottom() - view->laneRect(0).top();
			const bool fit = view->laneScrollBarRect().isEmpty() && view->laneScroll() == 0 && lowest() > ChartView::LANE_MIN_H - 0.01
					&& std::fabs(view->laneRect(1).height() - ChartView::LANE_MIN_H) < 0.01
					&& view->laneRect(0).height() > 2 * ChartView::LANE_MIN_H
					&& std::fabs(view->laneContentHeight() - plotH) < 0.01 && view->laneRect(0).top() >= 0;
			const QSize size = tab.size();
			tab.resize(size.width(), 420);
			QApplication::processEvents();
			(void) view->grab();
			const bool scrolls = !view->laneScrollBarRect().isEmpty() && std::fabs(lowest() - ChartView::LANE_MIN_H) < 0.01
					&& std::fabs(view->laneRect(0).height() - ChartView::LANE_MIN_H) < 0.01;
			if (!fit || !scrolls)
				std::printf("     (weights 6 0.3 0.3 0.3: lanes %g %g %g %g in %g px, scroll bar %d; short: the first %g, the "
						"lowest %g, scroll bar %d)\n", view->laneRect(0).height(), view->laneRect(1).height(),
						view->laneRect(2).height(), view->laneRect(3).height(), plotH, int(!view->laneScrollBarRect().isEmpty()),
						view->laneRect(0).height(), lowest(), int(!view->laneScrollBarRect().isEmpty()));
			tab.resize(size);
			QApplication::processEvents();
			view->setLaneHeights(kept);
			(void) view->grab();
			check(fit && scrolls, "chart, lane heights that fit: weights whose shares would put lanes under 80 px hold those "
					"at 80 and give the rest to the others, every lane in the plot, no scroll bar; too short for 80 px each, "
					"all at 80 and scrolled");
		}

		/* kept: a new tab of the same settings has the same heights; a double-click on a separator: all equal, saved */
		const double kept0 = view->laneRect(0).height();
		bool again = false;
		{
			ChartTab other([] { return 100.0; }, nullptr, group);
			fourLanes(other);
			ChartView *otherView = other.findChild<ChartView *>();
			other.show();
			(void) QTest::qWaitForWindowExposed(&other);
			(void) otherView->grab();
			again = std::fabs(otherView->laneRect(0).height() - kept0) < 0.6;
			other.hide();
		}
		const QPoint sep(int(x), int(std::lround(view->laneSeparators().value(0))));
		QTest::mouseDClick(view, Qt::LeftButton, Qt::NoModifier, sep);
		(void) view->grab();
		const bool equal = std::fabs(view->laneRect(0).height() - view->laneRect(1).height()) < 0.01
				&& std::fabs(view->laneRect(1).height() - view->laneRect(3).height()) < 0.01
				&& QSettings().value(group + QStringLiteral("/laneHeights")).toStringList().isEmpty();
		if (!again || !equal)
			std::printf("     (a new tab's first lane %g of %g; after the double-click %g %g %g)\n", again ? kept0 : -1.0, kept0,
					view->laneRect(0).height(), view->laneRect(1).height(), view->laneRect(3).height());
		check(again && equal, "chart, a lane's border: the heights found again by a new tab; a double-click on a "
				"separator sets every lane back to its equal share");
		tab.hide();
		QSettings().remove(group);
	}

	/* A held view where only a cursor moves reuses its lines: 20 drag steps bin nothing and draw no line again; a sample
	 * in view, the window, the size bin again; a Y range, Normalise or a fold draws the lines again; the picture is the
	 * one drawn without the reuse */
	void heldViewReuse() {
		LoneChart chart(QStringLiteral("R0"), QStringLiteral("V"));
		MathLines::Samples samples;
		QVector<RegDef> defs{ chart.def };
		for (int k = 1; k < 16; k++) {
			RegDef def = chart.def;
			def.addr = uint16_t(0xD000 + 2 * k);
			def.name = QStringLiteral("R%1").arg(k);
			chart.tab.plotRegister(def, true);
			defs << def;
		}
		for (const RegDef &def : std::as_const(defs))
			for (int i = 0; i < 15000; i++) /* 1 kHz for 15 s, up to 95 s: the view held at 100 ends after them */
				samples[regKey(def)] << QPointF(80.0 + i * 0.001, def.addr % 7 + std::sin(i * 0.003 + def.addr));
		chart.tab.frame(samples);
		ChartView *view = chart.view;
		view->setWindow(10);
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		(void) view->grab(); /* painted live first: held, the view stays where it was painted (90 .. 100) */
		view->setLive(false);
		view->setCursorMode(true);
		(void) view->grab();
		double t0, t1;
		view->viewSpan(t0, t1);
		const bool linesInView = t1 > 96 && t0 <= 90.5;
		const QRectF plot = view->laneScrollBarRect().isEmpty() ? QRectF(view->rect()).adjusted(64, 44, -18, -68)
				: view->laneScrollBarRect();
		const QPoint start(int(plot.left() + plot.width() * 0.3), int(plot.center().y()));
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, start);
		(void) view->grab();
		const int binnings = view->binnings(), builds = view->lineBuilds();
		for (int step = 1; step <= 20; step++) {
			const QPointF at(start.x() + step * 10, start.y());
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
			(void) view->grab();
		}
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(start.x() + 200, start.y()));
		(void) view->grab();
		const int dragBinnings = view->binnings() - binnings, dragBuilds = view->lineBuilds() - builds;
		if (dragBinnings || dragBuilds)
			std::printf("     (20 drag steps: %d binnings, %d lines drawn)\n", dragBinnings, dragBuilds);
		if (!linesInView) std::printf("     (the view %g .. %g: not over the lines)\n", t0, t1);
		check(linesInView && dragBinnings == 0 && dragBuilds == 0 && std::isfinite(view->cursorA()), "chart, a held view: a cursor "
				"dragged 20 steps bins nothing and draws no line again (the lines reused)");

		/* each key changed: binned again (the samples in view, the window, the size) or drawn again (a Y range,
		 * Normalise) */
		QStringList missed;
		const auto bins = [&](const char *what, const std::function<void()> &change) {
			const int before = view->binnings();
			change();
			(void) view->grab();
			if (view->binnings() == before) missed << QString::fromLatin1(what);
		};
		const auto draws = [&](const char *what, const std::function<void()> &change) {
			const int before = view->lineBuilds();
			change();
			(void) view->grab();
			if (view->lineBuilds() == before) missed << QString::fromLatin1(what);
		};
		bins("a sample in view", [&] {
			MathLines::Samples one;
			one[regKey(defs[3])] << QPointF(97.0, 3.5); /* after the last, before the view's end */
			chart.tab.frame(one);
		});
		bins("the window", [&] { view->setWindow(8); });
		bins("the size", [&] { chart.tab.resize(1100, 700); });
		draws("a Y range", [&] { view->setYManual(-2, 9); });
		draws("Normalise", [&] { view->setNormalized(true); });
		view->setNormalized(false);
		view->setYAuto();
		(void) view->grab();
		if (!missed.isEmpty()) std::printf("     (not again after: %s)\n", qPrintable(missed.join(QStringLiteral(", "))));

		/* the picture with the lines reused is the one drawn without */
		const QImage reused = view->grab().toImage();
		view->setLineReuse(false);
		const QImage fresh = view->grab().toImage();
		view->setLineReuse(true);
		/* the same pixels, but for the lines' antialiased edges: drawn into a clear picture and that onto the chart,
		 * an edge's blend is rounded once more (at most 2 of 255) */
		int differing = 0, most = 0;
		for (int y = 0; y < reused.height() && reused.size() == fresh.size(); y++)
			for (int x = 0; x < reused.width(); x++) {
				const QRgb a = reused.pixel(x, y), b = fresh.pixel(x, y);
				if (a == b) continue;
				differing++;
				most = std::max({ most, std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)),
						std::abs(qBlue(a) - qBlue(b)) });
			}
		std::printf("     (the lines reused and drawn afresh: %d pixels apart, by %d of 255 at most)\n", differing, most);
		check(missed.isEmpty() && reused.size() == fresh.size() && most <= 2, "chart, a held view: a sample in view, the "
				"window or the size bins again, a Y range or Normalise draws the lines again; the picture with the lines "
				"reused is the one drawn afresh (edges within 2 of 255)");
		view->setCursorMode(false);
		chart.tab.hide();
	}

	/* The measure table while a cursor moves: one repaint per update (its updates off while the cells are written),
	 * the line over it written only when its text changes */
	void measureTableRepaints() {
		QSettings().remove(QStringLiteral("chart/measureColumns"));
		LoneChart chart(QStringLiteral("M0"), QStringLiteral("V"));
		MathLines::Samples samples;
		for (int i = 0; i < 2000; i++) samples[regKey(chart.def)] << QPointF(90.0 + i * 0.005, std::sin(i * 0.01));
		chart.tab.frame(samples);
		auto *measure = chart.tab.findChild<QPushButton *>(QStringLiteral("measure"));
		QTableWidget *table = chart.table();
		if (!measure || !table) {
			check(false, "chart, the measure table: its button and table");
			return;
		}
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		(void) chart.view->grab(); /* painted live first, then held there */
		chart.view->setLive(false);
		measure->setChecked(true);
		QApplication::processEvents();
		struct PaintCounter : QObject {
			int paints = 0;
			bool eventFilter(QObject *, QEvent *e) override {
				if (e->type() == QEvent::Paint) paints++;
				return false;
			}
		} counter;
		/* cursor A dragged over the chart in eight steps, the measurements following it (at most every 100 ms) */
		ChartView *view = chart.view;
		view->setCursorMode(true);
		view->clearCursors();
		(void) view->grab();
		QApplication::processEvents();
		table->viewport()->installEventFilter(&counter);
		const int updates = chart.tab.measureUpdates(), infos = chart.tab.measureInfoChanges(), paints = counter.paints;
		const QPoint start(300, 200);
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, start);
		for (int step = 1; step <= 8; step++) {
			const QPointF at(start.x() + step * 15, start.y());
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
			QTest::qWait(120);
		}
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(start.x() + 120, start.y()));
		QTest::qWait(200);
		const int moved = chart.tab.measureUpdates() - updates, drawn = counter.paints - paints;
		const int movedInfos = chart.tab.measureInfoChanges() - infos;
		table->viewport()->removeEventFilter(&counter);
		/* the same measurements again: the line over the table is not written */
		const int sameInfos = chart.tab.measureInfoChanges();
		for (int k = 0; k < 4; k++) chart.tab.setShown(true);
		const int unchanged = chart.tab.measureInfoChanges() - sameInfos;
		view->setCursorMode(false);
		if (drawn > moved + 1 || movedInfos > 1 || unchanged != 0 || moved < 4)
			std::printf("     (%d updates, %d paints of the table; the line over it written %d times, %d with the same text)\n",
					moved, drawn, movedInfos, unchanged);
		check(moved >= 4 && drawn <= moved + 1 && movedInfos <= 1 && unchanged == 0, "chart, the measure table while a "
				"cursor is dragged: one repaint per update at most, the line over it written only when its text changes");
		measure->setChecked(false);
		chart.tab.hide();
	}

	/* The full measurements on the chart's threads, the window thread never waiting for them: a held view is measured
	 * again only when a sample lands in its range (samples after it change nothing); a dragged cursor starts none until
	 * it is let go; the table's values are those measured on the window thread */
	void measureInBackground() {
		LoneChart chart(QStringLiteral("B0"), QStringLiteral("V"));
		QVector<RegDef> defs{ chart.def };
		for (int k = 1; k < 8; k++) {
			RegDef def = chart.def;
			def.addr = uint16_t(0xD000 + 2 * k);
			def.name = QStringLiteral("B%1").arg(k);
			chart.tab.plotRegister(def, true);
			defs << def;
		}
		MathLines::Samples samples;
		for (const RegDef &def : std::as_const(defs))
			for (int i = 0; i < 15000; i++) /* 1 kHz up to 95 s: the view held at 100 ends after them */
				samples[regKey(def)] << QPointF(80.0 + i * 0.001, def.addr % 5 + std::sin(i * 0.004 + def.addr));
		chart.tab.frame(samples);
		ChartView *view = chart.view;
		view->setWindow(10);
		auto *measure = chart.tab.findChild<QPushButton *>(QStringLiteral("measure"));
		if (!measure) {
			check(false, "chart, measurements in the background: the Measure button");
			return;
		}
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		(void) view->grab(); /* painted live first, then held there (90 .. 100) */
		view->setLive(false);
		const int waitsBefore = view->fullStatsOnWindowThread();
		measure->setChecked(true);
		chart.tab.setShown(true); /* as the window says when the Chart tab is the one shown: its timer measures */
		measured(view);
		QTest::qWait(300); /* a tick of the timer: nothing changed, nothing measured */
		measured(view);
		const auto feed = [&](double t) {
			MathLines::Samples one;
			for (const RegDef &def : std::as_const(defs)) one[regKey(def)] << QPointF(t, 1.0);
			chart.tab.frame(one);
		};
		/* a sample inside the range: measured once more; then samples after the view's end for 2 s: none */
		int full = chart.tab.measureFullUpdates();
		feed(96.0);
		(void) QTest::qWaitFor([&] { return chart.tab.measureFullUpdates() > full; }, 2000);
		measured(view);
		const int inside = chart.tab.measureFullUpdates() - full;
		full = chart.tab.measureFullUpdates();
		QElapsedTimer twoSeconds;
		twoSeconds.start();
		for (double t = 100.5; twoSeconds.elapsed() < 2000; t += 0.05) {
			chart.now = t; /* the clock goes on as the samples come: the held view stays behind */
			feed(t);
			QTest::qWait(50);
		}
		const int outside = chart.tab.measureFullUpdates() - full;
		if (inside != 1 || outside != 0)
			std::printf("     (a sample in the range: %d full measurements; samples after it for 2 s: %d)\n", inside, outside);
		check(inside == 1 && outside == 0, "chart, measurements of a held view: measured again once for a sample in its "
				"range, not at all for 2 s of samples after it");

		/* cursor A dragged for a second: no full measurement until it is let go, then one */
		view->setCursorMode(true);
		measured(view);
		full = chart.tab.measureFullUpdates();
		const QPoint start(300, 200);
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, start);
		for (int step = 1; step <= 4; step++) {
			const QPointF at(start.x() + step * 20, start.y());
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
			QTest::qWait(300);
		}
		const int whileDragged = chart.tab.measureFullUpdates() - full;
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(start.x() + 80, start.y()));
		(void) QTest::qWaitFor([&] { return chart.tab.measureFullUpdates() > full; }, 2000);
		measured(view);
		const int letGo = chart.tab.measureFullUpdates() - full;
		view->setCursorMode(false);
		if (whileDragged != 0 || letGo < 1)
			std::printf("     (dragged: %d full measurements; let go: %d)\n", whileDragged, letGo);
		check(whileDragged == 0 && letGo >= 1, "chart, measurements while a cursor is dragged: no full measurement until "
				"it is let go, then one");

		/* the table's values: those measured on the window thread; and the window thread waited for none */
		const int waits = view->fullStatsOnWindowThread() - waitsBefore;
		QVector<int> keys;
		for (const RegDef &def : std::as_const(defs)) keys << int(regKey(def));
		const QVector<ChartView::Stats> direct = view->stats(keys);
		bool same = chart.table() && chart.table()->rowCount() == keys.size();
		const auto number = [&](int row, int column) {
			const QTableWidgetItem *item = chart.table()->item(row, column);
			return item ? item->text().section(QLatin1Char(' '), 0, 0).toDouble() : NAN;
		};
		for (int row = 0; same && row < keys.size(); row++) {
			const ChartView::Stats &s = direct[row];
			const auto near = [](double a, double b) { return std::fabs(a - b) <= 1e-3 * std::max(1.0, std::fabs(b)); };
			same = near(number(row, ChartTab::ColMin), s.min) && near(number(row, ChartTab::ColMax), s.max)
					&& near(number(row, ChartTab::ColMean), s.mean) && near(number(row, ChartTab::ColRms), s.rms);
		}
		if (!same || waits != 0)
			std::printf("     (the table as measured on the window thread: %d; the window thread waited %d times)\n", int(same),
					waits);
		check(same && waits == 0, "chart, measurements on the chart's threads: the table's values those measured on the "
				"window thread, which never waited for them");
		measure->setChecked(false);
		chart.tab.setShown(false);
		measured(view);
		chart.tab.hide();
	}

	/* The timing aid: EVRE_PERF_LOG=<file> writes a line every 500 ms with the frames, the paint and its stages, the
	 * binnings, the measurements and the polls */
	void perfLog() {
		const QString path = QDir::temp().filePath(QStringLiteral("evre_perf_test.log"));
		QFile::remove(path);
		qputenv("EVRE_PERF_LOG", path.toLocal8Bit());
		bool written = false;
		QString first;
		{
			double now = 100;
			ChartTab tab([&now] { return now; }, nullptr, QStringLiteral("perfTest"));
			tab.resize(900, 500);
			RegDef def;
			def.addr = 0xD000;
			def.name = QStringLiteral("P0");
			tab.plotRegister(def, true);
			tab.show();
			(void) QTest::qWaitForWindowExposed(&tab);
			ChartView *view = tab.findChild<ChartView *>();
			QElapsedTimer running;
			running.start();
			while (running.elapsed() < 1300) {
				MathLines::Samples samples;
				for (int i = 0; i < 16; i++) samples[regKey(def)] << QPointF(now + i * 0.001, std::sin(now + i * 0.001));
				now += 0.016;
				tab.frame(samples);
				(void) view->grab();
				QTest::qWait(16);
			}
			QFile file(path);
			if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
				const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
				first = lines.value(0);
				written = lines.size() >= 2;
				for (const QString &line : lines)
					for (const char *part : { " perfTest ", " fps ", " paint ", " max ", "| bin ", " lines ", " segments ",
							 " present ", " marks ", " strip ", " legend ", " grid ", "| binned ", "| measure ", " threads ",
							 "| polls ", " fast ", " columns " })
						written = written && line.contains(QLatin1String(part));
			}
			tab.hide();
		}
		qunsetenv("EVRE_PERF_LOG");
		QFile::remove(path);
		if (!written) std::printf("     (the log: \"%s\")\n", qPrintable(first));
		check(written, "the timing aid: EVRE_PERF_LOG writes a line every 500 ms (frames, the paint, its stages, the "
				"binnings, the measurements, the polls, the fast records and columns)");
	}

	/* The Studio's icon, the teknile mark: the application's, so every window's (the main window, the Help, a recording),
	 * in every size from 16 to 256 px, a rounded square (its corner clear, its middle not) */
	void studioIcon() {
		const QIcon icon = QApplication::windowIcon();
		QList<QSize> sizes = icon.availableSizes();
		bool all = true;
		for (const int size : { 16, 24, 32, 48, 64, 128, 256 }) all = all && sizes.contains(QSize(size, size));
		const QImage at32 = icon.pixmap(QSize(32, 32), 1.0).toImage();
		const bool shape = at32.size() == QSize(32, 32) && qAlpha(at32.pixel(0, 0)) == 0 && qAlpha(at32.pixel(16, 26)) == 255;
		HelpDialog help;
		const bool shared = window_.windowIcon().cacheKey() == icon.cacheKey() && help.windowIcon().cacheKey() == icon.cacheKey();
		check(all && shape && shared, "the Studio's icon (the teknile mark): every window's, 16 to 256 px, a rounded square");
	}

	/* The analysis's own arithmetic: the FFT (an impulse, a sine, back again), the histogram's Freedman-Diaconis bins,
	 * the spectrum of a sine polled unevenly (resampled, Welch with a Hann window: its amplitude and frequency) */
	void analysisMath() {
		QVector<std::complex<double>> impulse(64, 0.0);
		impulse[0] = 1;
		analysis::fft(impulse);
		bool flat = true;
		for (const auto &x : impulse) flat = flat && std::abs(x - std::complex<double>(1, 0)) < 1e-12;
		QVector<std::complex<double>> sine(256), back;
		for (int k = 0; k < 256; k++) sine[k] = std::sin(2 * M_PI * 8 * k / 256.0);
		back = sine;
		analysis::fft(sine);
		const bool bin = std::abs(std::abs(sine[8]) - 128) < 1e-9 && std::abs(sine[7]) < 1e-9 && std::abs(sine[0]) < 1e-9;
		QVector<std::complex<double>> round = sine;
		analysis::fft(round, true);
		bool again = true;
		for (int k = 0; k < 256; k++) again = again && std::abs(round[k] / 256.0 - back[k]) < 1e-12;
		check(flat && bin && again, "analysis: the FFT of an impulse is flat, a sine of 8 periods in 256 lands in bin 8 "
				"(N/2), and back again");

		QVector<double> uniform;
		for (int i = 0; i < 1000; i++) uniform << i;
		const analysis::Histogram h = analysis::histogram(uniform);
		qint64 sum = 0;
		for (qint64 count : h.counts) sum += count;
		const analysis::Histogram one = analysis::histogram({ 5.0, 5.0, 5.0 });
		QVector<double> levels(100, 5.0);
		levels[0] = 1;
		levels[99] = 9;
		const analysis::Histogram few = analysis::histogram(levels);
		check(h.counts.size() == 10 && std::fabs(h.width - 99.9) < 1e-9 && sum == 1000 && h.total == 1000
						&& one.counts == QVector<qint64>{ 3 } && few.counts.size() == 10 && few.counts[5] == 98,
				"analysis, histogram: 0 .. 999 in 10 bins of 99.9 (2 IQR / n^(1/3)); one value in one bin; values that "
				"do not spread: the square root of n bins");

		QVector<double> times, values;
		quint32 seed = 12345;
		for (int i = 0; i < 10000; i++) { /* polls 1 ms apart, give or take 0.3 ms */
			seed = seed * 1664525u + 1013904223u;
			const double t = 100.0 + i * 0.001 + (double(seed >> 8) / double(1 << 24) - 0.5) * 0.0006;
			times << t;
			values << 1.0 + 2.0 * std::sin(2 * M_PI * 62.5 * t);
		}
		const analysis::Spectrum s = analysis::spectrum(times, values);
		qsizetype peak = 1;
		for (qsizetype j = 1; j < s.amplitude.size(); j++)
			if (s.amplitude[j] > s.amplitude[peak]) peak = j;
		std::printf("     (spectrum: %.2f Hz, %d segments of %d, peak %.4g at %.4g Hz, DC %.4g)\n", s.rate, s.segments,
				s.segment, s.amplitude.value(peak), s.frequency.value(peak), s.amplitude.value(0));
		check(std::fabs(s.rate - 1000) < 1 && s.segment == 2048 && s.segments >= 8
						&& std::fabs(s.frequency.value(peak) - 62.5) < s.resolution() && std::fabs(s.amplitude.value(peak) - 2) < 0.05
						&& std::fabs(s.amplitude.value(0) - 1) < 0.05 && std::fabs(s.frequency.last() - s.rate / 2) < 1e-9
						&& analysis::spectrum(QVector<double>(10, 1.0), QVector<double>(10, 1.0)).frequency.isEmpty(),
				"analysis, spectrum: a sine of 2 V at 62.5 Hz polled unevenly reads 2 V at 62.5 Hz (resampled at the mean "
				"rate, Welch, Hann, 50 % overlap), its mean at 0 Hz, up to half the rate; too few samples: none");
	}

	/* Histogram and Spectrum from a right-click on a line's chip: windows of their own over the view or A -> B, a
	 * readout under the mouse, the picture and CSV */
	void analysisWindows() {
		LoneChart chart(QStringLiteral("WAVE"), QStringLiteral("V"));
		MathLines::Samples samples;
		for (int i = 0; i < 10000; i++) samples[regKey(chart.def)] << QPointF(90.0 + i * 0.001, 3 * std::sin(2 * M_PI * 62.5 * i * 0.001));
		chart.tab.frame(samples);
		chart.view->setWindow(10);
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		(void) chart.view->grab();
		chart.view->setLive(false);
		const QRectF chip = chart.view->legendChips().value(0);
		QContextMenuEvent right(QContextMenuEvent::Mouse, chip.center().toPoint(), chart.view->mapToGlobal(chip.center().toPoint()));
		QApplication::sendEvent(chart.view, &right);
		QMenu *menu = chart.tab.lineMenu();
		QStringList texts;
		QAction *histogramAction = nullptr;
		if (menu)
			for (QAction *action : menu->actions()) {
				texts << action->text();
				if (action->text().startsWith(QLatin1String("Histogram"))) histogramAction = action;
			}
		const bool listed = menu && QTest::qWaitFor([&] { return menu->isVisible(); }, 2000)
				&& texts == QStringList{ QStringLiteral("Histogram of WAVE"), QStringLiteral("Spectrum of WAVE"), QString(),
						QStringLiteral("Trigger on this line") };
		if (histogramAction) histogramAction->trigger();
		if (menu) menu->close();
		auto *histogram = chart.tab.findChild<AnalysisWindow *>(QStringLiteral("histogramWindow"));
		const bool opened = histogram && histogram->isVisible() && histogram->windowTitle().startsWith(QStringLiteral(
				"Histogram of WAVE — the view")) && histogram->histogram().total >= 9000;
		const QRectF area = histogram ? QRectF(histogram->plot()->rect()).adjusted(64, 30, -18, -30) : QRectF();
		const QString readout = histogram ? histogram->readoutAt(area.center().x()) : QString();
		if (!listed || !opened) std::printf("     (the line's menu: %s; \"%s\")\n", qPrintable(texts.join(QStringLiteral(" | "))),
				histogram ? qPrintable(histogram->windowTitle()) : "no window");
		check(listed && opened && readout.contains(QStringLiteral(" V: ")) && readout.endsWith(QLatin1String("%)")),
				"analysis: a right-click on a line's chip offers its Histogram and Spectrum; the histogram over the view in "
				"a window of its own, the bin and its count under the mouse");

		/* A -> B: the spectrum of a second; its picture and CSV */
		chart.view->setCursors(92.0, 93.0);
		AnalysisWindow *spectrum = chart.tab.openAnalysis(AnalysisWindow::Kind::Spectrum, chart.key());
		QTemporaryDir folder;
		QString error;
		const bool exported = spectrum->exportCsv(folder.filePath(QStringLiteral("s.csv")), error);
		QFile csv(folder.filePath(QStringLiteral("s.csv")));
		QStringList rows;
		/* the platform's line ends (CRLF on Windows) */
		if (csv.open(QIODevice::ReadOnly))
			rows = QString::fromUtf8(csv.readAll()).split(QRegularExpression(QStringLiteral("\r?\n")), Qt::SkipEmptyParts);
		const bool picture = spectrum->savePicture(folder.filePath(QStringLiteral("s.png")))
				&& QImage(folder.filePath(QStringLiteral("s.png"))).size() == (QSizeF(spectrum->plot()->size()) * spectrum->devicePixelRatioF()).toSize();
		const QRectF plotArea = QRectF(spectrum->plot()->rect()).adjusted(64, 30, -18, -30);
		const QString at = spectrum->readoutAt(plotArea.left() + plotArea.width() * 62.5 / (spectrum->spectrum().rate / 2));
		spectrum->setLogScale(true);
		const QSize pictureSize = QImage(folder.filePath(QStringLiteral("s.png"))).size();
		std::printf("     (spectrum: \"%s\" / \"%s\" / at \"%s\" / csv %s, %lld rows for %lld / picture %dx%d for %.1fx%.1f)\n",
				qPrintable(spectrum->windowTitle()), qPrintable(spectrum->summary()), qPrintable(at),
				exported ? "written" : "not written", (long long) rows.size(),
				(long long) spectrum->spectrum().frequency.size() + 1, pictureSize.width(), pictureSize.height(),
				spectrum->plot()->width() * spectrum->devicePixelRatioF(),
				spectrum->plot()->height() * spectrum->devicePixelRatioF());
		check(spectrum->windowTitle().startsWith(QStringLiteral("Spectrum of WAVE — A → B, 1 s"))
						&& spectrum->summary().contains(QStringLiteral("peak 62.5 Hz: 3")) && (at == QStringLiteral("62.5 Hz: 3 V") || at.startsWith(QStringLiteral("62.5 Hz: 2.9")))
						&& exported && rows.value(0) == QLatin1String("frequency [Hz],amplitude [V]")
						&& rows.size() == spectrum->spectrum().frequency.size() + 1 && picture,
				"analysis: the spectrum over A -> B (its peak, 62.5 Hz at 3 V; the frequency and amplitude under the "
				"mouse); exported as CSV, its picture saved");
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* the two windows, for a look */
			spectrum->setLogScale(false);
			if (histogram) histogram->grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_histogram.png"));
			spectrum->grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_spectrum.png"));
		}
		for (AnalysisWindow *w : chart.tab.findChildren<AnalysisWindow *>()) w->close();
		chart.tab.hide();
	}

	/* The trigger: the view holds on a crossing with it at 20 % of the window and a marker; Normal holds on each and is
	 * armed again once the view is full; Single holds on the first; rising, falling, either; the level dragged; the
	 * measurements over what is held */
	void chartTrigger() {
		for (const char *key : { "chart/triggerLevel", "chart/triggerMode", "chart/triggerEdge", "chart/triggerLine",
					 "chart/triggerLevels" })
			QSettings().remove(QLatin1String(key)); /* the defaults: Rising, Normal */
		QSettings().setValue(QStringLiteral("chart/triggerPosition"), 0.2); /* saved: the place the times below are for */
		LoneChart chart(QStringLiteral("TRIG"), QStringLiteral("V"));
		chart.view->setWindow(1);
		chart.view->setSmooth(false);
		double fed = 99.0;
		const auto feed = [&](double until) { /* 1 kHz of a 1 Hz sine, the clock with it */
			MathLines::Samples samples;
			for (; fed < until - 1e-9; fed += 0.001) samples[regKey(chart.def)] << QPointF(fed, std::sin(2 * M_PI * fed));
			chart.now = fed;
			chart.tab.frame(samples);
		};
		feed(99.95);
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		auto *action = chart.tab.findChild<QAction *>(QStringLiteral("chartTrigger"));
		auto *row = chart.tab.findChild<QWidget *>(QStringLiteral("triggerRow"));
		auto *level = chart.tab.findChild<QLineEdit *>(QStringLiteral("triggerLevel"));
		auto *edge = chart.tab.findChild<QComboBox *>(QStringLiteral("triggerEdge"));
		auto *mode = chart.tab.findChild<QComboBox *>(QStringLiteral("triggerMode"));
		auto *arm = chart.tab.findChild<QPushButton *>(QStringLiteral("triggerArm"));
		auto *holdoff = chart.tab.findChild<QComboBox *>(QStringLiteral("triggerHoldoff"));
		if (!action || !row || !level || !edge || !mode || !arm || !holdoff) {
			check(false, "chart, Trigger: in the Display menu, its row");
			return;
		}
		action->setChecked(true);
		level->setText(QStringLiteral("0.5")); /* the line's own level, typed */
		emit level->editingFinished();
		holdoff->setEditText(QStringLiteral("0.9 s")); /* the window's length by default: a period here, 1 s */
		emit holdoff->lineEdit()->editingFinished();
		const bool shown = row->isVisible() && chart.view->triggerArmed() && chart.view->triggerLevel() == 0.5
				&& chart.view->triggerHoldoff() == 0.9;
		feed(100.5); /* rising through 0.5 at 100 + 1/12 */
		const double first = 100.0 + 1.0 / 12;
		(void) chart.view->grab();
		double t0, t1;
		bool cursors;
		chart.view->range(t0, t1, cursors);
		const bool held = std::fabs(chart.view->triggeredAt() - first) < 1e-6 && !chart.view->live()
				&& std::fabs(t0 - (first - 0.2)) < 1e-6 && std::fabs(t1 - (first + 0.8)) < 1e-6 && !chart.view->triggerTag().isEmpty()
				&& std::fabs(chart.view->triggerTag().center().x() - chart.view->lastPlot().left() - 0.2 * chart.view->lastPlot().width()) < 1;
		const ChartView::Stats measured = chart.view->stats(chart.key());
		if (!held) std::printf("     (triggered at %.6f, wanted %.6f; the view %.4f .. %.4f)\n", chart.view->triggeredAt(), first, t0, t1);
		check(shown && held && measured.ok && std::fabs(measured.max - 1) < 1e-3,
				"chart, Trigger: rising through 0.5: the view holds with the crossing at 20 % of the window and its marker; "
				"the measurements over it");

		/* Normal: the next period's crossing once the view is full */
		feed(101.0); /* full at 100.883, the hold-off over at 100.983: armed again */
		const bool armedAgain = chart.view->triggerArmed();
		feed(101.5);
		const bool next = std::fabs(chart.view->triggeredAt() - (first + 1)) < 1e-6;
		check(armedAgain && next, "chart, Trigger, Normal: armed again once the view is full and the hold-off has passed, "
				"holds on the next crossing");

		/* Single, falling: once */
		mode->setCurrentIndex(mode->findData(int(ChartView::TriggerMode::Single)));
		edge->setCurrentIndex(edge->findData(int(ChartView::TriggerEdge::Falling)));
		emit mode->activated(mode->currentIndex());
		feed(102.9); /* falling through 0.5 at 102 + 5/12 */
		const double falling = 102.0 + 5.0 / 12;
		const bool single = std::fabs(chart.view->triggeredAt() - falling) < 1e-6 && !chart.view->triggerArmed();
		feed(104.0);
		const bool stays = std::fabs(chart.view->triggeredAt() - falling) < 1e-6
				&& chart.tab.triggerState().startsWith(QStringLiteral("Single · complete at "));
		arm->click();
		edge->setCurrentIndex(edge->findData(int(ChartView::TriggerEdge::Either)));
		emit edge->activated(edge->currentIndex());
		feed(104.2); /* either: rising at 104 + 1/12 */
		const bool either = std::fabs(chart.view->triggeredAt() - (104.0 + 1.0 / 12)) < 1e-6;
		check(single && stays && either, "chart, Trigger: Single holds on the first falling crossing and stays; Arm, Either: "
				"the next crossing of any direction");

		/* the level dragged */
		const QPixmap heldPicture = chart.tab.grab();
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) /* the chart held on a crossing, for a look */
			heldPicture.save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_trigger.png"));
		const double y = chart.view->triggerLineY();
		const QPoint from(int(chart.view->lastPlot().center().x()), int(std::lround(y)));
		const QPoint to(from.x(), int(std::lround(chart.view->yOfValue(0.8))));
		QTest::mousePress(chart.view, Qt::LeftButton, Qt::NoModifier, from);
		QMouseEvent move(QEvent::MouseMove, QPointF(to), chart.view->mapToGlobal(QPointF(to)), Qt::NoButton, Qt::LeftButton,
				Qt::NoModifier);
		QApplication::sendEvent(chart.view, &move);
		QTest::mouseRelease(chart.view, Qt::LeftButton, Qt::NoModifier, to);
		const double dragged = chart.view->triggerLevel();
		const QStringList savedLevels = QSettings().value(QStringLiteral("chart/triggerLevels")).toStringList();
		check(std::isfinite(y) && std::fabs(dragged - 0.8) < 0.02 && std::fabs(level->text().toDouble() - dragged) < 1e-5
						&& savedLevels.size() == 1 && savedLevels[0].startsWith(QStringLiteral("TRIG\t"))
						&& std::fabs(savedLevels[0].section(QLatin1Char('\t'), 1, 1).toDouble() - dragged) < 1e-5,
				"chart, Trigger: the level's dashed line dragged to 0.8: the level follows, the row and the setting too");
		action->setChecked(false);
		check(!chart.view->triggerOn() && !row->isVisible(), "chart, Trigger off: its row hidden, the chart no longer held "
				"by crossings");
		for (const char *key : { "chart/triggerLevel", "chart/triggerMode", "chart/triggerEdge", "chart/triggerLine",
					 "chart/triggerLevels", "chart/triggerHoldoff" })
			QSettings().remove(QLatin1String(key));
		chart.tab.hide();
	}

	/* Trigger v2, each line its own: two lines of other units, VOLTS and AMPS, on one tab */
	struct TriggerPair {
		double now = 100, fed = 99;
		ChartTab tab{ [this] { return now; } };
		RegDef volts, amps;
		ChartView *view = nullptr;
		TriggerPair() {
			tab.resize(1200, 700);
			volts.addr = 0xD000;
			volts.name = QStringLiteral("VOLTS");
			volts.unit = QStringLiteral("V");
			amps.addr = 0xD002;
			amps.name = QStringLiteral("AMPS");
			amps.unit = QStringLiteral("A");
			tab.plotRegister(volts, true);
			tab.plotRegister(amps, true);
			view = tab.view();
			view->setSmooth(false);
			view->setWindow(1);
		}
		int voltsKey() const { return int(regKey(volts)); }
		int ampsKey() const { return int(regKey(amps)); }
		/* 1 kHz samples to `until`: VOLTS 5 + 2 sin(2 pi t), AMPS 0.5 + 0.25 sin(2 pi t + 1); the clock with them */
		void feed(double until) {
			MathLines::Samples samples;
			for (; fed < until - 1e-9; fed += 0.001) {
				samples[regKey(volts)] << QPointF(fed, 5 + 2 * std::sin(2 * M_PI * fed));
				samples[regKey(amps)] << QPointF(fed, 0.5 + 0.25 * std::sin(2 * M_PI * fed + 1));
			}
			now = fed;
			tab.frame(samples);
		}
	};
	static void clearTriggerSettings() {
		for (const char *key : { "chart/triggerLevel", "chart/triggerMode", "chart/triggerEdge", "chart/triggerLine",
					 "chart/triggerLevels", "chart/triggerPosition", "chart/triggerHoldoff", "chart/lanes" })
			QSettings().remove(QLatin1String(key));
		/* the place these checks' times were worked out for, saved (the default is 50 %: chartTriggerScopes checks it) */
		QSettings().setValue(QStringLiteral("chart/triggerPosition"), 0.2);
	}

	/* Trigger v2, the modes, the hold-off and the crossing's place: Auto runs live while no crossing comes, holds on one,
	 * is ready again once the hold-off has passed and runs live again when none comes for a window's length; the
	 * hold-off keeps a 1 kHz sine in a 10 ms window to a picture a window; the place dragged by its mark under the plot */
	void chartTriggerModes() {
		clearTriggerSettings();
		LoneChart chart(QStringLiteral("STEP"), QStringLiteral("V"));
		ChartView *view = chart.view;
		view->setWindow(1);
		view->setSmooth(false);
		double fed = 99.0;
		/* 1 kHz samples to `until`: 1 V while high(t), else 0; the clock with them */
		const auto feed = [&](double until, const std::function<bool(double)> &high) {
			MathLines::Samples samples;
			for (; fed < until - 1e-9; fed += 0.001) samples[regKey(chart.def)] << QPointF(fed, high(fed) ? 1.0 : 0.0);
			chart.now = fed;
			chart.tab.frame(samples);
		};
		const auto steps = [](double t) { return (t >= 100.3 && t < 102.5) || t >= 102.8; }; /* up at 100.3 and 102.8 */
		feed(99.95, steps);
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		auto *action = chart.tab.findChild<QAction *>(QStringLiteral("chartTrigger"));
		auto *level = chart.tab.findChild<QLineEdit *>(QStringLiteral("triggerLevel"));
		auto *mode = chart.tab.findChild<QComboBox *>(QStringLiteral("triggerMode"));
		auto *holdoff = chart.tab.findChild<QComboBox *>(QStringLiteral("triggerHoldoff"));
		auto *position = chart.tab.findChild<QSpinBox *>(QStringLiteral("triggerPosition"));
		if (!action || !level || !mode || !holdoff || !position) {
			check(false, "chart, Trigger modes: the row's controls found (mode, hold-off, position)");
			return;
		}
		action->setChecked(true);
		level->setText(QStringLiteral("0.5"));
		emit level->editingFinished();
		mode->setCurrentIndex(mode->findData(int(ChartView::TriggerMode::Auto)));
		emit mode->activated(mode->currentIndex());
		/* the state corner's last part: the trigger's */
		const auto corner = [view] {
			(void) view->grab();
			return view->stateFullText().section(QStringLiteral("  ·  "), -1);
		};
		const QString first = corner();
		const bool freeAtFirst = view->live() && first == QStringLiteral("Auto · free running")
				&& holdoff->currentText() == QStringLiteral("window") && view->holdoffSeconds() == 1;
		feed(100.5, steps); /* up at 100.3: held, the crossing at 20 % */
		const double at = view->triggeredAt();
		const QString second = corner();
		const bool held = !view->live() && std::fabs(at - 100.2995) < 1e-6
				&& second == QStringLiteral("Auto · triggered");
		feed(101.4, steps); /* the hold-off (the window's 1 s) over at 101.2995: ready, still held, still "triggered" */
		const QString third = corner();
		const bool waiting = !view->live() && view->triggerArmed() && third == QStringLiteral("Auto · triggered")
				&& view->triggeredAt() == at;
		feed(102.4, steps); /* no crossing for a window's length after that: live again */
		const QString fourth = corner();
		const bool runsAgain = view->live() && fourth == QStringLiteral("Auto · free running");
		feed(103.0, steps); /* down at 102.5 (rising only), up at 102.8: held again */
		const bool heldAgain = !view->live() && std::fabs(view->triggeredAt() - 102.7995) < 1e-6 && view->triggerHolds() == 2;
		std::printf("     (Auto: \"%s\", \"%s\" at %.4f, \"%s\", \"%s\"; held again at %.4f, %d holds)\n", qPrintable(first),
				qPrintable(second), at, qPrintable(third), qPrintable(fourth), view->triggeredAt(), view->triggerHolds());
		check(freeAtFirst && held && waiting && runsAgain && heldAgain, "chart, Trigger, Auto: runs live while no crossing "
				"comes (\"Auto · free running\"), holds on one (\"Auto · triggered\", no \"capturing\" while it re-arms), ready again after "
				"the hold-off (still held, still \"Auto · triggered\"), runs live again when none comes for a window's length, "
				"and holds on the next");

		/* the crossing's place: its flag above the plot at 20 %, a hand and a tooltip over it; dragged to 50 % the held
		 * view moves so the crossing sits there; clamped to 90 % and 0 %; the panel and the setting follow */
		(void) view->grab();
		const QRectF plot = view->lastPlot();
		const QRectF mark = view->triggerPositionMark();
		const bool atTwenty = !mark.isEmpty() && std::fabs(mark.center().x() - (plot.left() + 0.2 * plot.width())) < 1
				&& mark.bottom() < plot.top() - 1.5 && mark.top() >= plot.top() - 26 && position->value() == 20;
		QMouseEvent hover(QEvent::MouseMove, mark.center(), view->mapToGlobal(mark.center()), Qt::NoButton, Qt::NoButton,
				Qt::NoModifier);
		QApplication::sendEvent(view, &hover);
		const bool hand = view->cursor().shape() == Qt::PointingHandCursor && view->triggerMarkHovered()
				&& view->toolTipAt(mark.center()).contains(QStringLiteral("\nDrag: where the crossing sits in the window"));
		const auto dragTo = [view](double x) { /* from where the mark is now */
			const QRectF from = view->triggerPositionMark();
			QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from.center().toPoint());
			const QPointF to(x, from.center().y());
			QMouseEvent move(QEvent::MouseMove, to, view->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
			QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to.toPoint());
			(void) view->grab();
		};
		dragTo(plot.left() + 0.5 * plot.width());
		double t0, t1;
		bool cursors;
		view->range(t0, t1, cursors);
		const double crossing = view->triggeredAt();
		const bool atHalf = std::fabs(view->triggerPosition() - 0.5) < 0.005 && position->value() == 50
				&& std::fabs(t1 - (crossing + (1 - view->triggerPosition()) * view->window())) < 1e-9
				&& std::fabs(view->triggerTag().center().x() - (plot.left() + view->triggerPosition() * plot.width())) < 1
				&& std::fabs(QSettings().value(QStringLiteral("chart/triggerPosition")).toDouble() - view->triggerPosition()) < 1e-12;
		dragTo(plot.right() + 60);
		const bool clampedHigh = view->triggerPosition() == ChartView::TRIGGER_AT_MAX && position->value() == 90;
		/* the crossing at 90 % and the level above what the line shows (its tab at the plot's top): the tab right of
		 * the plot, never over the crossing nor over the newest samples; the flag right over the crossing */
		view->setTriggerLevel(5);
		(void) view->grab();
		const QRectF marker = view->triggerTag(), topTag = view->triggerLevelTag(), topSymbol = view->triggerEdgeButton();
		const QRectF flag = view->triggerPositionMark();
		const bool apart = !marker.isEmpty() && !topTag.isEmpty() && !topTag.intersects(marker)
				&& topTag.left() > plot.right() + 2 && topSymbol.right() == topTag.right() && topSymbol.top() == topTag.top()
				&& std::fabs(flag.center().x() - marker.center().x()) < 1;
		std::printf("     (the crossing %.0f..%.0f, the level's tab %.0f..%.0f (the plot ends at %.0f), the flag at %.1f)\n",
				marker.left(), marker.right(), topTag.left(), topTag.right(), plot.right(), flag.center().x());
		check(apart, "chart, Trigger: the level's tab right of the plot, never over the crossing at 90 % nor over the "
				"newest samples; the position's flag right over the crossing");
		view->setTriggerLevel(0.5);
		dragTo(plot.left() - 60);
		const bool clampedLow = view->triggerPosition() == 0 && position->value() == 0;
		position->setValue(20); /* typed in the panel: the mark follows */
		const bool typed = view->triggerPosition() == 0.2;
		if (!atTwenty || !atHalf) std::printf("     (the mark at %.1f, the plot %.1f .. %.1f; dragged to %.3f)\n", mark.center().x(),
				plot.left(), plot.right(), view->triggerPosition());
		check(atTwenty && hand && atHalf && clampedHigh && clampedLow && typed, "chart, Trigger: the crossing's place, a "
				"\"T ▼\" flag above the plot at 20 % (a hand and a tooltip over it); dragged to 50 % the held view moves so the crossing "
				"sits there; clamped to 90 % and 0 %; the panel's box and the setting follow, and the box moves the mark");
		action->setChecked(false);
		chart.tab.hide();

		/* the hold-off: a 1 kHz sine (20 samples a cycle) in a 10 ms window, Normal, half a second of it: the window's
		 * length by default, a picture a window (45 to 51 holds), not one a cycle; hold-off 0 and the crossing at 90 %:
		 * the next counts 1 ms after (one every cycle or two) */
		LoneChart sine(QStringLiteral("SINE"), QStringLiteral("V"));
		sine.view->setWindow(0.01);
		sine.view->setSmooth(false);
		double sineFed = 99.0;
		const auto feedSine = [&](double until) {
			while (sineFed < until - 1e-9) { /* 10 ms a frame */
				MathLines::Samples samples;
				for (int i = 0; i < 200; i++, sineFed += 0.00005)
					samples[regKey(sine.def)] << QPointF(sineFed, std::sin(2 * M_PI * 1000 * sineFed));
				sine.now = sineFed;
				sine.tab.frame(samples);
			}
		};
		feedSine(99.5);
		auto *sineAction = sine.tab.findChild<QAction *>(QStringLiteral("chartTrigger"));
		auto *sineLevel = sine.tab.findChild<QLineEdit *>(QStringLiteral("triggerLevel"));
		auto *sineMode = sine.tab.findChild<QComboBox *>(QStringLiteral("triggerMode"));
		auto *sineHoldoff = sine.tab.findChild<QComboBox *>(QStringLiteral("triggerHoldoff"));
		auto *sinePosition = sine.tab.findChild<QSpinBox *>(QStringLiteral("triggerPosition"));
		int perWindow = 0, perCycle = 0;
		if (sineAction && sineLevel && sineMode && sineHoldoff && sinePosition) {
			sine.tab.triggerOnLine(sine.key()); /* the line saved is the chart before's, not on this one: chosen here */
			sineLevel->setText(QStringLiteral("0.1"));
			emit sineLevel->editingFinished();
			sineMode->setCurrentIndex(sineMode->findData(int(ChartView::TriggerMode::Normal)));
			emit sineMode->activated(sineMode->currentIndex());
			const int before = sine.view->triggerHolds();
			feedSine(100.0);
			perWindow = sine.view->triggerHolds() - before;
			sineHoldoff->setEditText(QStringLiteral("0"));
			emit sineHoldoff->lineEdit()->editingFinished();
			sinePosition->setValue(90);
			const int middle = sine.view->triggerHolds();
			feedSine(100.5);
			perCycle = sine.view->triggerHolds() - middle;
			sineAction->setChecked(false);
		}
		std::printf("     (a 1 kHz sine in a 10 ms window, 0.5 s: %d holds with the hold-off of the window, %d with 0 and the "
				"crossing at 90 %%; the box says \"%s\")\n", perWindow, perCycle, sineHoldoff ? qPrintable(sineHoldoff->currentText()) : "");
		check(sineHoldoff && perWindow >= 40 && perWindow <= 51 && perCycle >= 200 && sineHoldoff->currentText() == QStringLiteral("0 s"),
				"chart, Trigger, hold-off: a 1 kHz signal in a 10 ms window holds once a window (the hold-off of the window's "
				"length, the default), not once a cycle; with a hold-off of 0 and the crossing at 90 % nearly every cycle counts");
		clearTriggerSettings();
	}

	/* Trigger v2, a steady picture at a short window: a 1 kHz sine (20 kHz samples) in a 10 ms window, Normal, fed as at
	 * 60 frames a second: after the first, every view held is shown full (the next crossing's view waits until it is), so
	 * a repeating wave stands still. And a re-trigger bins only the columns that are new: with no hold-off and the
	 * crossing at 90 % each crossing moves the view by a cycle or two; 50 of them, fed 1 ms a frame, bin about the new
	 * samples' columns (the binning counters), not the whole view each time: a polled line and a fast one */
	void chartTriggerSteady() {
		clearTriggerSettings();
		LoneChart sine(QStringLiteral("STEADY"), QStringLiteral("V"));
		ChartView *view = sine.view;
		view->setWindow(0.01);
		view->setSmooth(false);
		double fed = 99.0;
		const auto feed = [&](int samples) { /* 20 kHz */
			MathLines::Samples batch;
			for (int i = 0; i < samples; i++, fed += 0.00005)
				batch[regKey(sine.def)] << QPointF(fed, std::sin(2 * M_PI * 1000 * fed));
			sine.now = fed;
			sine.tab.frame(batch);
		};
		feed(20000);
		sine.tab.show();
		(void) QTest::qWaitForWindowExposed(&sine.tab);
		(void) view->grab();
		view->setTrigger(sine.key(), 0.1, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Normal);
		int frames = 0, shown = 0, partial = 0;
		int holds = view->triggerHolds();
		while (view->triggerHolds() - holds < 51 && frames < 600) {
			feed(334); /* 16.7 ms */
			(void) view->grab();
			frames++;
			double t0, t1;
			bool cursors;
			view->range(t0, t1, cursors);
			if (view->live() || view->triggerHolds() - holds < 2) continue; /* the first holds at once, filling */
			shown++;
			if (t1 > fed - 0.00005 + 1e-9) partial++; /* its end after the newest sample: not full */
		}
		std::printf("     (a 1 kHz sine in a 10 ms window at 60 frames a second: %d frames, %d holds; %d of the %d pictures "
				"held after the first not full)\n", frames, view->triggerHolds() - holds, partial, shown);
		check(view->triggerHolds() - holds >= 51 && shown >= 30 && partial == 0, "chart, Trigger, a short window: after the "
				"first crossing every view held is shown full (the next crossing's view once it is), a repeating wave stands "
				"still");

		/* the re-trigger's binning: no hold-off, the crossing at 90 % (the next counts 1 ms after), 1 ms a frame */
		view->setTriggerHoldoff(0);
		view->setTriggerPosition(0.9);
		feed(20);
		(void) view->grab();
		holds = view->triggerHolds();
		const double polledFrom = view->triggeredAt();
		const qint64 polledBefore = view->polledColumnsBinned();
		int polledFrames = 0;
		while (view->triggerHolds() - holds < 50 && polledFrames < 400) {
			feed(20);
			(void) view->grab();
			polledFrames++;
		}
		const qint64 polledColumns = view->polledColumnsBinned() - polledBefore;
		const double polledMoved = view->triggeredAt() - polledFrom; /* the view moved with its crossing */
		const int polledHolds = view->triggerHolds() - holds;
		view->stopTrigger();
		sine.tab.hide();

		/* the fast line: 100 k records a second, a record a column */
		StreamDef def;
		def.name = QStringLiteral("WAVE");
		def.addr = 0xDC00;
		def.size = 1024;
		def.rate = 100000;
		StreamChannel channel;
		channel.name = QStringLiteral("V");
		channel.unit = QStringLiteral("V");
		channel.scale = 0.001;
		def.channels = { channel };
		QWidget host;
		host.resize(1100, 480);
		auto *fastView = new ChartView(&host);
		fastView->setGeometry(9, 5, 1080, 470);
		double now = 100.0;
		fastView->setClock([&now] { return now; }, 0);
		fastView->setSmooth(false);
		const int key = ChartView::fastKey(0, 0);
		fastView->setFastStream(0, def);
		fastView->addSeries(key, QStringLiteral("WAVE.V"), QStringLiteral("V"), QColor(255, 0, 0));
		fastView->setWindow(0.01);
		qint64 next = 0;
		fast::TriggerScan scan; /* the engine's part: each block's crossings */
		const auto feedFast = [&](qint64 n) { /* n records of the sine, their time mark, its crossings; the clock with them */
			QByteArray records(int(n * 2), '\0');
			for (qint64 k = 0; k < n; k++) {
				const qint16 raw = qint16(std::lround(1000 * std::sin(2 * M_PI * 1000 * double(next + k) / 100000.0)));
				records[int(2 * k)] = char(raw);
				records[int(2 * k + 1)] = char(raw >> 8);
			}
			const qint64 at = fastView->appendFast(0, quint64(next), n, records, next == 0, 0);
			fast::BlockTaken taken;
			taken.first = quint64(next);
			taken.count = int(n);
			taken.newStart = next == 0;
			next += n;
			fastView->markFast(0, quint64(next), 100.0 + double(next) / 100000.0, 1e-5);
			now = 100.0 + double(next) / 100000.0;
			int stream = -1;
			const fast::TriggerWatch watch = fastView->fastTriggerWatch(stream);
			if (watch.serial != scan.watch().serial) scan.set(watch);
			QVector<fast::Crossing> crossings;
			scan.scan(def, taken, records.constData(), { quint64(next), now }, 1e-5, crossings);
			fastView->fastCrossings(0, at, crossings);
		};
		host.show();
		(void) QTest::qWaitForWindowExposed(&host);
		feedFast(2000);
		(void) fastView->grab();
		fastView->setTrigger(key, 0.1, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Normal);
		fastView->setTriggerHoldoff(0);
		fastView->setTriggerPosition(0.9);
		feedFast(200);
		(void) fastView->grab();
		holds = fastView->triggerHolds();
		const double fastFrom = fastView->triggeredAt();
		const qint64 fastBefore = fastView->fastColumnsBinned();
		int fastFrames = 0;
		while (fastView->triggerHolds() - holds < 50 && fastFrames < 400) {
			feedFast(100);
			(void) fastView->grab();
			fastFrames++;
		}
		const qint64 fastColumns = fastView->fastColumnsBinned() - fastBefore;
		const double fastMoved = fastView->triggeredAt() - fastFrom;
		const int fastHolds = fastView->triggerHolds() - holds;
		const double columns = fastView->lastPlot().width();
		/* about the samples fed (a bin each: 20 a frame polled, 100 a frame fast) and the two ends of each frame; and at
		 * least the columns the view moved by (a sample's each: 50 us polled, 10 us fast), so nothing binned fails too */
		const bool polledNew = polledHolds >= 50 && polledColumns <= 2 * 20 * polledFrames + 4 * polledFrames
				&& polledColumns > 0 && polledColumns >= 0.8 * polledMoved / 0.00005;
		const bool fastNew = fastHolds >= 50 && fastColumns <= 1.2 * 100 * fastFrames + 4 * fastFrames && fastColumns > 0
				&& fastColumns >= 0.8 * fastMoved / 1e-5;
		std::printf("     (the views moved %.4f s (polled) and %.4f s (fast) with their crossings)\n", polledMoved, fastMoved);
		std::printf("     (re-triggered: the polled line %d times in %d frames, %lld columns binned (%.0f a re-trigger, %.0f in "
				"its view); the fast line %d times in %d frames, %lld columns (%.0f a re-trigger, %.0f in the view))\n",
				polledHolds, polledFrames, (long long) polledColumns, double(polledColumns) / std::max(1, polledHolds),
				0.01 / 0.00005, fastHolds, fastFrames, (long long) fastColumns, double(fastColumns) / std::max(1, fastHolds),
				columns);
		check(polledNew && fastNew, "chart, Trigger, a short window re-triggered 50 times (no hold-off, the crossing at 90 %): "
				"only the new columns are binned, a polled line's and a fast line's, not the whole view each time, and at "
				"least the columns the view moved by");

		/* a held view that fills after its crossing (Single, 2.5 s, the crossing at 20 %): the view stands, the samples
		 * reach further columns at each frame. The columns already complete are kept, so a frame bins what is new (a
		 * frame's worth, 1/60 s: 7 columns of about 1 000) and the open column at the data's end, not the filled part again */
		const int polledKey = 7;
		fastView->addSeries(polledKey, QStringLiteral("POLLED"), QStringLiteral("V"), QColor(0, 128, 255));
		double polledAt = now;
		const auto feedFrame = [&] { /* a frame's worth: 1 667 fast records, 167 polled samples (10 kHz) */
			feedFast(1667);
			for (; polledAt < now; polledAt += 0.0001) fastView->append(polledKey, polledAt, std::sin(2 * M_PI * 3 * polledAt));
		};
		fastView->setWindow(2.5);
		fastView->setTriggerPosition(0.2);
		feedFrame();
		(void) fastView->grab();
		fastView->setTrigger(key, 0.1, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Single);
		const int fillHolds = fastView->triggerHolds();
		feedFrame();
		(void) fastView->grab(); /* the view held: binned once, from the start */
		const bool fillHeld = fastView->triggerHolds() == fillHolds + 1 && fastView->triggerCapturing();
		const double heldFrom = fastView->triggeredAt();
		const double dataFrom = now;
		const qint64 fillFastBefore = fastView->fastColumnsBinned(), fillPolledBefore = fastView->polledColumnsBinned();
		int fillFrames = 0;
		while (fillHeld && fastView->triggerCapturing() && fillFrames < 200) {
			feedFrame();
			(void) fastView->grab();
			fillFrames++;
		}
		const qint64 fillFast = fastView->fastColumnsBinned() - fillFastBefore;
		const qint64 fillPolled = fastView->polledColumnsBinned() - fillPolledBefore;
		const double fillColumns = fastView->lastPlot().width();
		/* the columns the data reached meanwhile, each binned about once; at most a few more a frame (the first column
		 * and the open one at the data's end), never the filled part again (a column a frame times the frames) */
		const double reached = (std::min(now, heldFrom + 0.8 * 2.5) - dataFrom) / 2.5 * fillColumns;
		const bool fillOnlyNew = fillHeld && fillFrames >= 100 && fillFast <= 1.2 * reached + 4 * fillFrames
				&& fillFast >= 0.8 * reached && fillPolled <= 1.2 * reached + 4 * fillFrames && fillPolled >= 0.8 * reached;
		std::printf("     (a held view filling: %d frames, %.0f columns reached; binned %lld of the fast line (%.1f a frame), "
				"%lld of the polled line (%.1f a frame); the view %.0f columns)\n", fillFrames, reached, (long long) fillFast,
				double(fillFast) / std::max(1, fillFrames), (long long) fillPolled, double(fillPolled) / std::max(1, fillFrames),
				fillColumns);
		check(fillOnlyNew, "chart, Trigger, a held view filling after its crossing (Single, 2.5 s, 120 frames): each frame "
				"bins only the columns its new samples reach and the open one at the data's end, a fast line's and a polled "
				"line's, never the part already filled again");

		/* a short window (100 ms, Normal, the hold-off a window): a view is held only once it is full, and the next
		 * crossing's view fills meanwhile behind the one shown. Its fast lines are binned as their records come, so
		 * the frame that shows it bins what came since the frame before, not the whole view (all of it at once made
		 * that frame late), and the work in all stays about a view's columns a view */
		fastView->stopTrigger();
		fastView->removeSeries(polledKey);
		fastView->setWindow(0.1);
		fastView->setTrigger(key, 0.1, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Normal);
		fastView->setTriggerHoldoff(-1);
		feedFrame();
		(void) fastView->grab();
		const int shortHolds = fastView->triggerHolds();
		const qint64 shortBefore = fastView->fastColumnsBinned();
		qint64 atShowMost = 0;
		int shortFrames = 0, showFrames = 0;
		while (fastView->triggerHolds() - shortHolds < 20 && shortFrames < 300) {
			const int holdsNow = fastView->triggerHolds();
			feedFrame();
			const qint64 before = fastView->fastColumnsBinned();
			(void) fastView->grab();
			if (fastView->triggerHolds() != holdsNow && fastView->triggerHolds() - shortHolds > 1) { /* a view shown */
				atShowMost = std::max(atShowMost, fastView->fastColumnsBinned() - before);
				showFrames++;
			}
			shortFrames++;
		}
		const qint64 shortColumns = fastView->fastColumnsBinned() - shortBefore;
		const int shortShown = fastView->triggerHolds() - shortHolds;
		const double shortWide = fastView->lastPlot().width();
		/* a frame brings 1/60 s of a 0.1 s view: a sixth of its columns, and the first and the open one */
		const bool spread = shortShown >= 20 && showFrames >= 15 && atShowMost <= shortWide / 6 * 1.2 + 4
				&& shortColumns <= 1.2 * shortShown * shortWide + 4 * shortFrames;
		std::printf("     (a short window re-triggered: %d views in %d frames, %lld columns binned (%.0f a view, %.0f in the "
				"view); at the frame a view is shown at most %lld)\n", shortShown, shortFrames, (long long) shortColumns,
				double(shortColumns) / std::max(1, shortShown), shortWide, (long long) atShowMost);
		check(spread, "chart, Trigger, a short window re-triggered (100 ms, Normal): the next view's fast lines are binned "
				"while it fills behind the one shown, so the frame that shows it bins only what came since the frame before "
				"(at most a sixth of the view, not all of it), and about a view's columns a view in all");
		fastView->stopTrigger();
		host.hide();
		clearTriggerSettings();
	}

	/* Trigger v2 on a fast line, the engine's part played by a fast::TriggerScan given each watch the view posts (as
	 * IoEngine::setFastTrigger is) and each block before the view has it (as takeBlock): the level dragged reaches the
	 * engine at the next frame, so the view holds on the new level before the mouse is let go; the crossing's place
	 * dragged posts at most once a frame and only when it moved; Clear with Single armed; a new start that the store
	 * shifts after the one before; the window zoomed by the wheel. And the engine's own: a crossing that went with a
	 * dropped block arms its scan again; the blocks still waiting for the window are scanned again for a new watch */
	void chartTriggerFast() {
		clearTriggerSettings();
		StreamDef def;
		def.name = QStringLiteral("STEP");
		def.addr = 0xDC00;
		def.size = 1024;
		def.rate = 100000;
		StreamChannel channel;
		channel.name = QStringLiteral("V");
		channel.unit = QStringLiteral("V");
		channel.scale = 0.001;
		def.channels = { channel };
		QWidget host;
		host.resize(1100, 480);
		auto *view = new ChartView(&host);
		view->setGeometry(9, 5, 1080, 470);
		double now = 100.0;
		view->setClock([&now] { return now; }, 0);
		view->setSmooth(false);
		const int key = ChartView::fastKey(0, 0);
		view->setFastStream(0, def);
		view->addSeries(key, QStringLiteral("STEP.V"), QStringLiteral("V"), QColor(255, 0, 0));
		view->setWindow(0.01);
		fast::TriggerScan scan;
		int posts = 0;
		QObject::connect(view, &ChartView::fastTriggerChanged, view, [&] {
			int stream = -1;
			scan.set(view->fastTriggerWatch(stream));
			posts++;
		});
		/* record k of this start at start + k / 100 kHz, its value wave(time); scanned, then appended, marked and its
		 * crossings handed over, as the window takes a block */
		qint64 next = 0;
		double start = 100.0;
		std::function<double(double)> wave = [](double t) { return std::fmod(t, 0.001) < 0.0005 ? 0.3 : 0.0; };
		const auto feed = [&](qint64 n, bool newStart) {
			QByteArray records(int(n * 2), '\0');
			for (qint64 k = 0; k < n; k++) {
				const qint16 raw = qint16(std::lround(1000 * wave(start + double(next + k) / 100000.0)));
				records[int(2 * k)] = char(raw);
				records[int(2 * k + 1)] = char(raw >> 8);
			}
			fast::BlockTaken taken;
			taken.first = quint64(next);
			taken.count = int(n);
			taken.newStart = newStart;
			const fast::FastClock::Mark mark{ quint64(next + n), start + double(next + n) / 100000.0 };
			QVector<fast::Crossing> crossings;
			scan.scan(def, taken, records.constData(), mark, 1e-5, crossings);
			const qint64 at = view->appendFast(0, quint64(next), n, records, newStart, 0);
			next += n;
			view->markFast(0, mark.record, mark.time, 1e-5);
			now = mark.time;
			view->fastCrossings(0, at, crossings);
		};
		const auto moveTo = [view](QPointF at) {
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
		};
		host.show();
		(void) QTest::qWaitForWindowExposed(&host);
		feed(2000, true);
		(void) view->grab();

		/* the level: Single at 0.5 V over a 1 kHz square of 0 to 0.3 V (no crossing), dragged to 0.15 V in ten moves */
		view->setTrigger(key, 0.5, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Single);
		feed(1000, false);
		(void) view->grab();
		const int holds = view->triggerHolds();
		const QPointF from(view->lastPlot().center().x(), view->triggerLineY());
		const QPointF to(from.x(), view->yOfValue(0.15));
		const int before = posts;
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from.toPoint());
		for (int i = 1; i <= 10; i++) moveTo(from + (to - from) * (i / 10.0));
		const int whileMoving = posts - before;
		view->frame();
		const int atFrame = posts - before;
		feed(1000, false); /* crossings at the new level, the mouse still down */
		const bool heldBeforeRelease = view->triggerHolds() == holds + 1 && std::fabs(view->triggerLevel() - 0.15) < 0.01;
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to.toPoint());
		const int atRelease = posts - before;
		std::printf("     (the level dragged: %d posts while moving, %d at the frame, %d after the release; held %d time(s) "
				"before the release at %.4f V)\n", whileMoving, atFrame, atRelease, view->triggerHolds() - holds,
				view->triggerLevel());
		check(holds == 0 && whileMoving == 0 && atFrame == 1 && atRelease == 1 && heldBeforeRelease, "chart, Trigger on a "
				"fast line: the level dragged reaches the engine at the next frame, once (not at each move, not only at the "
				"release): Single holds on a crossing at the new level while the mouse is still down");

		/* the crossing's place: ten moves, a frame; five moves to the same place, a frame; the release */
		(void) view->grab();
		const QPointF mark = view->triggerPositionMark().center();
		const int placeBefore = posts;
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, mark.toPoint());
		for (int i = 1; i <= 10; i++) moveTo(QPointF(mark.x() + 30 * i, mark.y()));
		const int placeMoving = posts - placeBefore;
		view->frame();
		const int placeFrame = posts - placeBefore;
		for (int i = 0; i < 5; i++) moveTo(QPointF(mark.x() + 300, mark.y()));
		view->frame();
		const int placeStill = posts - placeBefore;
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(int(mark.x()) + 300, int(mark.y())));
		const int placeRelease = posts - placeBefore;
		const double placed = view->triggerPosition();
		view->setTriggerPosition(ChartView::TRIGGER_AT);
		std::printf("     (the place dragged to %.3f: %d posts while moving, %d at the frame, %d after moves to the same "
				"place and a frame, %d after the release)\n", placed, placeMoving, placeFrame, placeStill, placeRelease);
		check(placed > 0.3 && placeMoving == 0 && placeFrame == 1 && placeStill == 1 && placeRelease == 1, "chart, Trigger "
				"on a fast line: the crossing's place dragged is given to the engine once a frame, and only when it moved "
				"(each post a new arm: the crossings found for the one before are not used)");

		/* Clear with Single armed: the next block begins above the level after one that ended below (cleared): its
		 * crossing at the block's first record has no record before it in the store; the engine counted it */
		wave = [](double) { return 0.0; };
		view->armTrigger();
		feed(500, false);
		const int clearHolds = view->triggerHolds();
		const double cleared = now;
		const int clearPosts = posts;
		view->clearData();
		wave = [cleared](double t) { return t < cleared + 0.005 ? 0.3 : t < cleared + 0.01 ? 0.0 : 0.3; };
		for (int i = 0; i < 4; i++) feed(500, false);
		const bool afterClear = view->triggerHolds() == clearHolds + 1 && view->triggeredAt() > cleared + 0.009
				&& view->triggeredAt() < cleared + 0.011;
		std::printf("     (Clear with Single armed: %d posts after it, held %d time(s), at %.5f s after the Clear)\n",
				posts - clearPosts, view->triggerHolds() - clearHolds, view->triggeredAt() - cleared);
		check(afterClear && posts - clearPosts >= 2, "chart, Trigger on a fast line, Single, Clear: a crossing the view "
				"cannot hold on (its record before cleared) arms the engine again, and the next crossing holds");

		/* a new start whose clock begins 20 ms before the last one ended: the store shifts it after it, the engine's
		 * clock does not; Single armed before it, a step 7 ms into it */
		wave = [](double) { return 0.0; };
		view->armTrigger();
		feed(1000, false);
		const double lastEnd = start + double(next - 1) / 100000.0;
		const int shiftHolds = view->triggerHolds();
		const int shiftPosts = posts;
		start = lastEnd - 0.02;
		next = 0;
		wave = [&start](double t) { return t >= start + 0.007 ? 0.3 : 0.0; };
		feed(500, true);
		const int postedAtStart = posts - shiftPosts;
		feed(500, false);
		feed(500, false);
		const bool shifted = view->triggerHolds() == shiftHolds + 1 && view->triggeredAt() > lastEnd + 0.006
				&& view->triggeredAt() < lastEnd + 0.008;
		std::printf("     (a new start 20 ms early: %d post(s) at its mark; held %d time(s), %.5f s after the last start's "
				"end)\n", postedAtStart, view->triggerHolds() - shiftHolds, view->triggeredAt() - lastEnd);
		check(shifted && postedAtStart == 1, "chart, Trigger on a fast line: after a new start the store shifts (its clock "
				"20 ms behind the start before), the engine is armed again in its clock's terms and the step 7 ms into it "
				"holds the view");

		/* the wheel: Normal, no hold-off, held on a crossing; zoomed out a notch (10 to 20 ms, a 1-2-5 step of the
		 * divisions' time grid): the engine's re-arm is the new fill (after the crossing's place), and the next crossing
		 * counts after it */
		wave = [](double t) { return std::fmod(t, 0.001) < 0.0005 ? 0.3 : 0.0; };
		view->setTriggerHoldoff(0);
		view->setTrigger(key, ChartView::TriggerMode::Normal);
		int steps = 0;
		const int wheelHolds = view->triggerHolds();
		while (view->triggerHolds() == wheelHolds && steps++ < 50) feed(100, false);
		const double firstAt = view->triggeredAt();
		(void) view->grab();
		const int wheelPosts = posts;
		const QPointF middle = view->lastPlot().center();
		QWheelEvent wheel(middle, view->mapToGlobal(middle), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
				Qt::NoScrollPhase, false);
		QApplication::sendEvent(view, &wheel);
		int stream = -1;
		const fast::TriggerWatch zoomed = view->fastTriggerWatch(stream);
		const double newFill = (1 - view->triggerPosition()) * view->window();
		steps = 0;
		while (view->triggerHolds() == wheelHolds + 1 && steps++ < 100) {
			feed(100, false);
			view->frame();
		}
		const double gap = view->triggeredAt() - firstAt;
		std::printf("     (the wheel: the window %.4f s, %d post(s), the engine's re-arm %.4f s; the next crossing %.4f s "
				"after the one held)\n", view->window(), posts - wheelPosts, zoomed.rearm, gap);
		check(std::fabs(view->window() - 0.02) < 1e-9 && posts - wheelPosts >= 1 && std::fabs(zoomed.rearm - newFill) < 1e-12
						&& view->triggerHolds() == wheelHolds + 2 && gap >= newFill - 1e-9 && gap < newFill + 0.0015,
				"chart, Trigger on a fast line: the window zoomed by the wheel is given to the engine (its re-arm the new "
				"window's fill), and the next crossing counts after the new fill");
		view->stopTrigger();
		host.hide();

		/* the engine's scan alone: Single finds the first rising edge and stops; that crossing dropped with its block
		 * (the queue's overflow): armed again from it; a crossing of an older watch dropped: nothing changes */
		const auto block = [](const QVector<double> &values) {
			QByteArray records(int(values.size() * 2), '\0');
			for (int k = 0; k < values.size(); k++) {
				const qint16 raw = qint16(std::lround(1000 * values[k]));
				records[2 * k] = char(raw);
				records[2 * k + 1] = char(raw >> 8);
			}
			return records;
		};
		QVector<double> stepUp(100, 0.0);
		for (int k = 50; k < 100; k++) stepUp[k] = 0.3;
		const QByteArray up = block(stepUp);
		fast::TriggerScan engine;
		fast::TriggerWatch single;
		single.on = true;
		single.level = 0.15;
		single.from = -1e9;
		single.serial = 7;
		engine.set(single);
		int first = 0;
		const auto scanUp = [&](fast::TriggerScan &onto) {
			fast::BlockTaken taken;
			taken.first = quint64(first);
			taken.count = 100;
			taken.newStart = first == 0;
			first += 100;
			QVector<fast::Crossing> found;
			onto.scan(def, taken, up.constData(), { quint64(first), 200.0 + first / 100000.0 }, 1e-5, found);
			return found;
		};
		const QVector<fast::Crossing> one = scanUp(engine);
		const bool stopped = one.size() == 1 && scanUp(engine).isEmpty();
		fast::Crossing older = one.value(0);
		older.serial = 6;
		engine.dropped(older);
		const bool olderIgnored = scanUp(engine).isEmpty();
		engine.dropped(one.value(0));
		const QVector<fast::Crossing> again = scanUp(engine);
		check(stopped && olderIgnored && again.size() == 1 && again[0].serial == 7, "fast trigger, the engine: Single "
				"stops at its crossing; that crossing dropped with its block (the window never had it) arms the scan again "
				"and the next one is found; one of an older watch changes nothing");

		/* the blocks waiting for the window scanned again for a new watch: the first paired with the record before it
		 * (0 V, the window's newest; it begins at 0.3 V), the second's step found; another stream's left alone */
		QVector<IoEngine::FastBlock> waiting(3);
		for (int b = 0; b < 3; b++) {
			waiting[b].stream = b == 1 ? 1 : 0;
			waiting[b].first = quint64(1000 + 100 * b);
			waiting[b].count = 100;
		}
		waiting[0].records = block(QVector<double>(100, 0.3));
		waiting[0].before = block({ 0.0 });
		waiting[1].records = up;
		waiting[1].crossings.push_back({ 5, 0.5, 0.0, 3 });
		waiting[2].first = 1100;
		QVector<double> down(100, 0.3);
		for (int k = 30; k < 60; k++) down[k] = 0.0;
		waiting[2].records = block(down);
		fast::TriggerScan rescan;
		fast::TriggerWatch normal = single;
		normal.rearm = 0;
		normal.serial = 9;
		rescan.set(normal);
		IoEngine::rescanWaiting(rescan, def, 0, waiting, { 1200, 210.0 }, 1e-5);
		const bool rescanned = waiting[0].crossings.size() == 1 && waiting[0].crossings[0].record == 0
				&& waiting[0].crossings[0].serial == 9 && waiting[1].crossings.size() == 1 && waiting[1].crossings[0].serial == 3
				&& waiting[2].crossings.size() == 1
				&& waiting[2].crossings[0].record == 60;
		check(rescanned, "fast trigger, the engine: a new watch scans the blocks still waiting for the window again (a "
				"crossing between the window's newest record and the first of them counts), another stream's untouched");
		clearTriggerSettings();
	}

	/* Trigger v2, Auto and the user's view: a short window whose samples come 50 ms after their time stays held from
	 * crossing to crossing (Auto's window by the samples' time, as the arm: by the clock it ran live at every frame); a
	 * view the user held (Hold) or moved (a pan) stays where it is; Live drops a crossing waiting for its view, which
	 * pulled the view back to it a few frames later */
	void chartTriggerAuto() {
		clearTriggerSettings();
		LoneChart late(QStringLiteral("LATE"), QStringLiteral("V"));
		ChartView *view = late.view;
		view->setWindow(0.01);
		view->setSmooth(false);
		double fed = 99.0;
		const auto feed = [&](int samples, double behind) { /* 20 kHz of a 1 kHz sine; the clock `behind` the newest */
			MathLines::Samples batch;
			for (int i = 0; i < samples; i++, fed += 0.00005)
				batch[regKey(late.def)] << QPointF(fed, std::sin(2 * M_PI * 1000 * fed));
			late.now = fed + behind;
			late.tab.frame(batch);
		};
		feed(2000, 0.05);
		late.tab.show();
		(void) QTest::qWaitForWindowExposed(&late.tab);
		view->setTrigger(late.key(), 0.1, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Auto);
		const int holds = view->triggerHolds();
		int liveFrames = 0;
		for (int frame = 0; frame < 60; frame++) {
			feed(334, 0.05); /* 16.7 ms */
			(void) view->grab();
			if (view->triggerHolds() > holds && view->live()) liveFrames++;
		}
		std::printf("     (Auto, a 1 kHz sine in a 10 ms window, the samples 50 ms late: %d holds in 60 frames, %d frames "
				"live after the first)\n", view->triggerHolds() - holds, liveFrames);
		check(view->triggerHolds() - holds >= 20 && liveFrames == 0, "chart, Trigger, Auto: crossings coming all along keep "
				"the view held, its window of no crossing by the samples' time (they come 50 ms late), not by the clock");

		/* Stop while a crossing waits for its view (Normal, no hold-off): the crossing dropped, the view and its T stay,
		 * no crossing holds while stopped; Run: the next hold is on a crossing after it */
		view->setTriggerHoldoff(0);
		view->setTrigger(late.key(), ChartView::TriggerMode::Normal);
		int frames = 0;
		while (!std::isfinite(view->triggerPending()) && frames++ < 300) feed(20, 0);
		const bool waited = std::isfinite(view->triggerPending());
		auto *hold = late.tab.findChild<QPushButton *>(QStringLiteral("hold"));
		const bool stopOffered = hold && hold->text() == QStringLiteral("Stop");
		const double heldOn = view->triggeredAt();
		if (hold) hold->click(); /* Stop */
		(void) view->grab();
		double s0, s1;
		bool spanned;
		view->range(s0, s1, spanned);
		const int stoppedHolds = view->triggerHolds();
		for (int i = 0; i < 30; i++) feed(20, 0); /* 30 crossings */
		(void) view->grab();
		double e0, e1;
		view->range(e0, e1, spanned);
		const int whileStopped = view->triggerHolds() - stoppedHolds;
		const bool dropped = !view->live() && std::isnan(view->triggerPending()) && whileStopped == 0
				&& e1 == s1 && view->triggeredAt() == heldOn && !view->triggerTag().isEmpty()
				&& view->triggerPhase() == ChartView::TriggerPhase::Stopped && hold && hold->text() == QStringLiteral("Run");
		const double runAt = fed - 0.00005;
		if (hold) hold->click(); /* Run */
		const int runHolds = view->triggerHolds();
		frames = 0;
		while (view->triggerHolds() == runHolds && frames++ < 100) feed(20, 0);
		std::printf("     (a crossing waiting for its view at Stop: %d; stopped: %d holds over 30 crossings, the view's end "
				"moved %.6f s; the next hold %.5f s after Run)\n", int(waited), whileStopped, e1 - s1,
				view->triggeredAt() - runAt);
		check(stopOffered && waited && dropped && view->triggerHolds() == runHolds + 1 && view->triggeredAt() > runAt,
				"chart, Trigger: Stop drops a crossing waiting for its view (a short window) and keeps the view and its T, no "
				"crossing holds while stopped; Run: the next hold is on a crossing after it, not on the one before");
		view->stopTrigger();
		late.tab.hide();

		/* the user's view in Auto: a level no sample reaches (free running), Hold, 3 s: still held; a crossing held,
		 * a pan, 3 s with none: still where the pan left it */
		LoneChart quiet(QStringLiteral("QUIET"), QStringLiteral("V"));
		ChartView *still = quiet.view;
		still->setSmooth(false);
		double at = 99.0;
		const auto feedStep = [&](double until, double stepAt) { /* 1 kHz: 0, 1 from stepAt on */
			MathLines::Samples batch;
			for (; at < until - 1e-9; at += 0.001) batch[regKey(quiet.def)] << QPointF(at, at >= stepAt ? 1.0 : 0.0);
			quiet.now = at;
			quiet.tab.frame(batch);
		};
		feedStep(100, 1e9);
		quiet.tab.show();
		(void) QTest::qWaitForWindowExposed(&quiet.tab);
		/* the chart takes the focus first: the window's box, losing it, applies its own text (the press of the pan
		 * below did, and the view's window became the box's 30 s) */
		still->setFocus();
		QApplication::processEvents();
		still->setWindow(1);
		still->setTrigger(quiet.key(), 0.5, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Auto);
		auto *quietHold = quiet.tab.findChild<QPushButton *>(QStringLiteral("hold"));
		const bool freeRunning = still->live();
		if (quietHold) quietHold->click(); /* Stop */
		for (double until = 100.1; until <= 103.0; until += 0.1) feedStep(until, 1e9);
		const bool holdKept = !still->live() && still->triggerPhase() == ChartView::TriggerPhase::Stopped;
		if (quietHold) quietHold->click(); /* Run: Auto runs live */
		feedStep(103.5, 103.2); /* up at 103.2: held by the trigger */
		const bool heldByCrossing = !still->live() && std::fabs(still->triggeredAt() - 103.2) < 0.002;
		(void) still->grab();
		/* far from the level's line (a press by it drags the level); 80 % of the window back (the view held ends 0.5 s
		 * after now: a shorter pan is live again) */
		const QRectF stillPlot = still->lastPlot();
		const double offLevel = still->triggerLineY() < stillPlot.center().y() ? stillPlot.bottom() - 15 : stillPlot.top() + 15;
		const QPointF grip(stillPlot.left() + 0.1 * stillPlot.width(), offLevel);
		const QPointF panTo(stillPlot.left() + 0.9 * stillPlot.width(), grip.y());
		QTest::mousePress(still, Qt::LeftButton, Qt::NoModifier, grip.toPoint());
		QMouseEvent pan(QEvent::MouseMove, panTo, still->mapToGlobal(panTo), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(still, &pan);
		QTest::mouseRelease(still, Qt::LeftButton, Qt::NoModifier, panTo.toPoint());
		(void) still->grab(); /* range(): the view as painted */
		const bool pannedHeld = !still->live();
		double t0, t1;
		bool cursors;
		still->range(t0, t1, cursors);
		for (double until = 103.6; until <= 106.5; until += 0.1) feedStep(until, 103.2);
		(void) still->grab();
		double u0, u1;
		still->range(u0, u1, cursors);
		const bool panKept = pannedHeld && !still->live() && u1 == t1 && t1 < 103.4
				&& still->triggerPhase() == ChartView::TriggerPhase::Stopped;
		std::printf("     (Auto and the user: free running %d, held by Hold after 3 s %d; held by a crossing %d, after a pan "
				"and 3 s %d: the view's end %.4f after the pan, %.4f 3 s later, live %d; the press at %.0f, the level's line at %.0f)\n",
				int(freeRunning), int(holdKept), int(heldByCrossing), int(panKept), t1, u1, int(still->live()), grip.y(),
				still->triggerLineY());
		check(quietHold && freeRunning && holdKept && heldByCrossing && panKept, "chart, Trigger, Auto: Stop or a pan stops "
				"the trigger and the view stays where it is; Run runs live again; Auto runs live by itself only from a view a "
				"crossing held");
		still->stopTrigger();
		quiet.tab.hide();
		clearTriggerSettings();
	}

	/* Trigger v2, Run and Stop, one state in one voice: while the trigger is on the toolbar's Hold / Live is Run / Stop,
	 * in each mode (Stop disarms and keeps the view and its T, no crossing moves it; Run arms in the mode from now: Auto
	 * runs live, Normal and Single wait on a still view); a pan is a Stop; the row's and the corner's texts per state as
	 * the review's table; a faint "now" edge at the data's end in every lane while a view fills after its crossing; Arm
	 * only in Single; and the texts not rewritten while crossings keep coming in the same state */
	void chartTriggerRunStop() {
		clearTriggerSettings();
		LoneChart chart(QStringLiteral("RUN"), QStringLiteral("V"));
		ChartView *view = chart.view;
		view->setWindow(1);
		view->setSmooth(false);
		double fed = 99.0;
		const auto feed = [&](double until) { /* 1 kHz of a 1 Hz sine (rising through 0.5 at k + 1/12), 0.1 s a frame */
			while (fed < until - 1e-9) {
				MathLines::Samples samples;
				const double to = std::min(until, fed + 0.1);
				for (; fed < to - 1e-9; fed += 0.001) samples[regKey(chart.def)] << QPointF(fed, std::sin(2 * M_PI * fed));
				chart.now = fed;
				chart.tab.frame(samples);
			}
		};
		feed(99.95);
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		/* the chart takes the focus first: the window's box, losing it to the pan's press, applies its own 30 s */
		view->setFocus();
		QApplication::processEvents();
		view->setWindow(1);
		auto *action = chart.tab.findChild<QAction *>(QStringLiteral("chartTrigger"));
		auto *row = chart.tab.findChild<QWidget *>(QStringLiteral("triggerRow"));
		auto *level = chart.tab.findChild<QLineEdit *>(QStringLiteral("triggerLevel"));
		auto *mode = chart.tab.findChild<QComboBox *>(QStringLiteral("triggerMode"));
		auto *arm = chart.tab.findChild<QPushButton *>(QStringLiteral("triggerArm"));
		auto *hold = chart.tab.findChild<QPushButton *>(QStringLiteral("hold"));
		/* its whole text (fullText): a narrower row, as with Linux's fonts, shows it cut by the policy, whole in the tooltip */
		auto *state = chart.tab.findChild<ElidedLabel *>(QStringLiteral("triggerState"));
		if (!action || !row || !level || !mode || !arm || !hold || !state) {
			check(false, "chart, Trigger, Run and Stop: the row's controls and the toolbar's button found");
			return;
		}
		action->setChecked(true);
		const auto setLevel = [level](const char *text) {
			level->setText(QLatin1String(text));
			emit level->editingFinished();
		};
		const auto setMode = [mode](ChartView::TriggerMode m) {
			mode->setCurrentIndex(mode->findData(int(m)));
			emit mode->activated(mode->currentIndex());
		};
		const auto viewEnd = [view] { /* as painted */
			(void) view->grab();
			double t0, t1;
			bool cursors;
			view->range(t0, t1, cursors);
			return t1;
		};
		bool textsOk = true;
		const auto texts = [&](const QString &rowText, const QString &cornerText, const char *what) {
			(void) view->grab();
			const QString shownRow = state->fullText(), shownCorner = view->stateFullText();
			const bool same = shownRow == rowText && shownCorner == cornerText;
			if (!same)
				std::printf("     (%s: the row \"%s\", the corner \"%s\")\n", what, qPrintable(shownRow), qPrintable(shownCorner));
			textsOk = textsOk && same;
		};
		const auto stopRun = [hold] { hold->click(); };
		using Phase = ChartView::TriggerPhase;
		bool armOnlySingle = true;

		/* Normal, a level the sine never reaches: a still view, "waiting" */
		setMode(ChartView::TriggerMode::Normal);
		setLevel("2");
		view->setTriggerHoldoff(0.5);
		/* waiting in Normal: the button is Force (Arm does nothing there) */
		armOnlySingle = armOnlySingle && arm->isVisibleTo(row) && arm->text() == QStringLiteral("Force");
		const double stillEnd = viewEnd();
		bool still = !view->live();
		for (double until = 100.0; until <= 101.5 + 1e-9; until += 0.1) {
			feed(until);
			still = still && !view->live() && viewEnd() == stillEnd;
		}
		texts(QStringLiteral("waiting for a crossing"), QStringLiteral("Normal · waiting"), "Normal, waiting");
		const bool normalRuns = hold->text() == QStringLiteral("Stop") && view->triggerPhase() == Phase::Waiting;
		check(still && normalRuns, "chart, Trigger, Normal waiting for a crossing: the view stands still (it does not roll "
				"as Auto does), the button says Stop");

		/* the level reached: held, the view filling after its T, the "now" edge at the data's end; then full */
		setLevel("0.5");
		feed(102.2); /* rising through 0.5 at 102.0833 */
		texts(QStringLiteral("Normal · triggered"), QStringLiteral("Normal · triggered"), "Normal, capturing");
		const QRectF plot = view->lastPlot();
		const QVector<QLineF> edges = view->nowEdges();
		double t0, t1;
		bool cursors;
		view->range(t0, t1, cursors);
		const double newest = fed - 0.001;
		const double wantX = plot.left() + (newest - t0) / (t1 - t0) * plot.width();
		bool edgeAt = edges.size() == 1 && std::fabs(edges[0].x1() - wantX) < 1 && edges[0].x1() == edges[0].x2()
				&& edges[0].y1() >= plot.top() - 0.5 && edges[0].y2() <= plot.bottom() + 0.5;
		/* drawn, in both themes: the edge's column differs from the empty one a few pixels right of it */
		const bool wasDark = Theme::isDark();
		int seenIn[2] = { 0, 0 };
		for (const bool dark : { false, true }) {
			Theme::apply(*qApp, dark);
			const QImage picture = view->grab().toImage();
			const double dpr = picture.devicePixelRatio();
			const int x = int(std::lround(wantX * dpr - 0.5)), right = x + int(std::lround(5 * dpr));
			int rows = 0, differ = 0;
			for (int y = int((plot.top() + 6) * dpr); y < int((plot.bottom() - 6) * dpr); y++, rows++) {
				const QColor a = picture.pixelColor(x, y), b = picture.pixelColor(right, y), c = picture.pixelColor(x + 1, y);
				if (a != b || c != b) differ++;
			}
			seenIn[dark] = rows > 0 ? 100 * differ / rows : 0;
		}
		Theme::apply(*qApp, wasDark);
		feed(102.95); /* the view full at 102.8833 */
		texts(QStringLiteral("Normal · triggered"), QStringLiteral("Normal · triggered"), "Normal, triggered");
		(void) view->grab();
		const bool edgeGone = view->nowEdges().isEmpty();

		/* with lanes, one lane per unit: an edge in each */
		bool laneEdges = false;
		{
			TriggerPair pair;
			pair.feed(99.95);
			pair.view->setLanes(true);
			pair.tab.show();
			(void) QTest::qWaitForWindowExposed(&pair.tab);
			pair.view->setTrigger(pair.voltsKey(), 5, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Normal);
			pair.feed(100.3); /* rising through 5 at 100 */
			(void) pair.view->grab();
			const QVector<QLineF> laneLines = pair.view->nowEdges();
			laneEdges = pair.view->laneCount() == 2 && laneLines.size() == 2;
			for (int i = 0; laneEdges && i < 2; i++) {
				const QRectF lane = pair.view->laneRect(i);
				laneEdges = laneLines[i].x1() == laneLines[0].x1() && laneLines[i].y1() >= lane.top() - 0.5
						&& laneLines[i].y2() <= lane.bottom() + 0.5 && laneLines[i].length() > 0.5 * lane.height();
			}
			pair.view->stopTrigger();
			pair.tab.hide();
		}
		std::printf("     (the now edge at x %.1f, wanted %.1f; its column seen in %d %% (light) and %d %% (dark) of the "
				"plot's rows; with lanes: %s)\n", edges.isEmpty() ? -1.0 : edges[0].x1(), wantX, seenIn[0], seenIn[1],
				laneEdges ? "an edge in each lane" : "not in each lane");
		check(edgeAt && seenIn[0] >= 80 && seenIn[1] >= 80 && edgeGone && laneEdges, "chart, Trigger: a view filling after "
				"its crossing has a faint \"now\" edge at the newest sample, drawn in both themes, one in every lane with "
				"Lanes on; none once the view is full");

		/* Stop and Run in Normal: the picture and its T stay through crossings; Run waits on the same picture */
		bool runStop = true;
		const double heldOn = view->triggeredAt();
		runStop = runStop && hold->text() == QStringLiteral("Stop");
		stopRun();
		texts(QStringLiteral("Stopped · Run to arm"), QStringLiteral("Stopped · Run to arm"), "Normal, stopped");
		const double stoppedEnd = viewEnd();
		int holds = view->triggerHolds();
		feed(104.5); /* crossings at 103.08 and 104.08 */
		runStop = runStop && hold->text() == QStringLiteral("Run") && view->triggerHolds() == holds && viewEnd() == stoppedEnd
				&& view->triggeredAt() == heldOn && !view->triggerTag().isEmpty();
		stopRun(); /* Run */
		/* waiting again after a capture: when the last one was, in the row alone */
		texts(QStringLiteral("Normal · waiting, last at %1").arg(QDateTime::fromMSecsSinceEpoch(view->epochMs()
				+ qint64(std::llround(heldOn * 1000))).toString(QStringLiteral("HH:mm:ss"))), QStringLiteral("Normal · waiting"),
				"Normal, run again");
		runStop = runStop && hold->text() == QStringLiteral("Stop") && !view->live() && viewEnd() == stoppedEnd;
		feed(105.2);
		runStop = runStop && view->triggerHolds() == holds + 1 && std::fabs(view->triggeredAt() - (105 + 1.0 / 12)) < 1e-6;
		const bool normalRunStop = runStop;
		/* a pan while it runs: a Stop */
		(void) view->grab();
		const QRectF area = view->lastPlot();
		const double offLevel = view->triggerLineY() < area.center().y() ? area.bottom() - 15 : area.top() + 15;
		const QPointF grip(area.left() + 0.3 * area.width(), offLevel), to(area.left() + 0.6 * area.width(), offLevel);
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, grip.toPoint());
		QMouseEvent pan(QEvent::MouseMove, to, view->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(view, &pan);
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to.toPoint());
		const bool panStops = view->triggerPhase() == Phase::Stopped && hold->text() == QStringLiteral("Run");

		/* Auto: Run runs live; Stop holds through crossings. The mode chosen while stopped stays stopped until Run */
		setMode(ChartView::TriggerMode::Auto);
		const bool modeKeptStop = view->triggerPhase() == Phase::Stopped && hold->text() == QStringLiteral("Run");
		stopRun(); /* Run */
		armOnlySingle = armOnlySingle && !arm->isVisibleTo(row);
		texts(QStringLiteral("Auto · free running"), QStringLiteral("Auto · free running"), "Auto, free running");
		bool autoRunStop = modeKeptStop && view->live() && hold->text() == QStringLiteral("Stop");
		holds = view->triggerHolds();
		feed(106.3); /* 106.0833: held */
		autoRunStop = autoRunStop && view->triggerHolds() == holds + 1 && !view->live();
		stopRun();
		const double autoStopped = viewEnd();
		feed(107.5);
		autoRunStop = autoRunStop && view->triggerPhase() == Phase::Stopped && view->triggerHolds() == holds + 1
				&& viewEnd() == autoStopped && hold->text() == QStringLiteral("Run");
		stopRun(); /* Run */
		autoRunStop = autoRunStop && view->live() && view->triggerPhase() == Phase::FreeRunning;
		feed(108.3);
		autoRunStop = autoRunStop && view->triggerHolds() == holds + 2;

		/* Single: Run waits on a still view, the crossing stops it (its time in the row, Arm the primary button) */
		setMode(ChartView::TriggerMode::Single);
		const bool armInSingle = arm->isVisibleTo(row);
		texts(QStringLiteral("waiting for a crossing"), QStringLiteral("Single · waiting"), "Single, waiting");
		const double singleStill = viewEnd();
		bool singleRunStop = !view->live() && hold->text() == QStringLiteral("Stop");
		holds = view->triggerHolds();
		feed(109.3); /* 109.0833 */
		singleRunStop = singleRunStop && view->triggerHolds() == holds + 1 && view->triggerPhase() == Phase::Done
				&& hold->text() == QStringLiteral("Run") && arm->property("primary").toBool() && singleStill < viewEnd();
		const QString when = QDateTime::fromMSecsSinceEpoch(view->epochMs() + qint64(std::llround(view->triggeredAt() * 1000)))
				.toString(QStringLiteral("HH:mm:ss.zzz"));
		texts(QStringLiteral("Single · complete at %1").arg(when), QStringLiteral("Single · complete, capturing after T"),
				"Single, capturing");
		feed(110.5); /* full at 109.8833; 110.0833 does not count */
		texts(QStringLiteral("Single · complete at %1").arg(when), QStringLiteral("Single · complete"), "Single, complete");
		singleRunStop = singleRunStop && view->triggerHolds() == holds + 1;
		view->setLive(true); /* back at now, as a pan to the right end */
		texts(QStringLiteral("Single · complete at %1").arg(when), QStringLiteral("Single · complete · Arm to wait"),
				"Single, live");
		stopRun(); /* Run: as Arm */
		singleRunStop = singleRunStop && view->triggerPhase() == Phase::Waiting && !view->live()
				&& hold->text() == QStringLiteral("Stop") && !arm->property("primary").toBool();
		stopRun(); /* Stop */
		singleRunStop = singleRunStop && view->triggerPhase() == Phase::Stopped;
		stopRun(); /* Run */
		feed(111.3); /* 111.0833 */
		singleRunStop = singleRunStop && view->triggerHolds() == holds + 2 && view->triggerPhase() == Phase::Done;
		std::printf("     (Run and Stop: Normal %d, a pan stops %d, Auto %d, Single %d)\n", int(normalRunStop), int(panStops),
				int(autoRunStop), int(singleRunStop));
		check(normalRunStop && panStops && autoRunStop && singleRunStop, "chart, Trigger: the toolbar's button is Run / "
				"Stop while the trigger is on, in each mode: Stop keeps the view and its T through crossings, Run arms from "
				"now (Auto runs live, Normal and Single wait on a still view); a pan is a Stop; Single's crossing stops it");
		check(textsOk, "chart, Trigger: the row and the corner say one state in the review's words (waiting for a "
				"crossing / Normal · waiting, Normal · triggered in both while it captures and after, Stopped · Run to arm, Normal · "
				"waiting, last at the time / Normal · waiting, Auto · free running, Single · complete at the time / Single · "
				"complete, Single · complete · Arm to wait)");
		check(armOnlySingle && armInSingle, "chart, Trigger: the Arm button in Single (Force while Normal waits, hidden in "
				"Auto), the primary button while Single holds its crossing");
		action->setChecked(false);
		const bool backToHold = hold->text() == QStringLiteral("Live") || hold->text() == QStringLiteral("Hold");
		chart.tab.hide();

		/* crossings coming all along (a 1 kHz sine in a 10 ms window, no hold-off, the crossing at 75 %: one counts
		 * every 3 ms): 50 re-triggers in one state rewrite neither the row nor the corner */
		clearTriggerSettings(); /* the row of the next tab reads the mode saved: Normal again */
		LoneChart sine(QStringLiteral("RATE"), QStringLiteral("V"));
		sine.view->setWindow(0.01);
		sine.view->setSmooth(false);
		double sineFed = 99.0;
		const auto feedSine = [&](int samples) { /* 20 kHz */
			MathLines::Samples batch;
			for (int i = 0; i < samples; i++, sineFed += 0.00005)
				batch[regKey(sine.def)] << QPointF(sineFed, std::sin(2 * M_PI * 1000 * sineFed));
			sine.now = sineFed;
			sine.tab.frame(batch);
			(void) sine.view->grab();
		};
		feedSine(20000);
		sine.tab.show();
		(void) QTest::qWaitForWindowExposed(&sine.tab);
		auto *sineState = sine.tab.findChild<ElidedLabel *>(QStringLiteral("triggerState"));
		sine.tab.triggerOnLine(sine.key());
		sine.view->setTriggerLevel(0.1);
		sine.view->setTriggerHoldoff(0);
		sine.view->setTriggerPosition(0.75);
		int frames = 0;
		while (sine.view->triggerHolds() < 10 && frames++ < 300) feedSine(20);
		const int warm = sine.view->triggerHolds();
		while (sine.view->triggerHolds() == warm && frames++ < 400) feedSine(20);
		const QString rowBefore = sineState ? sineState->fullText() : QString(), cornerBefore = sine.view->stateFullText();
		const int start = sine.view->triggerHolds();
		int rewrites = 0;
		while (sine.view->triggerHolds() - start < 50 && frames++ < 1200) {
			feedSine(20);
			if (sineState && sineState->fullText() != rowBefore) rewrites++;
			if (sine.view->stateFullText() != cornerBefore) rewrites++;
		}
		const int retriggers = sine.view->triggerHolds() - start;
		std::printf("     (%d re-triggers: the row \"%s\", the corner \"%s\", %d rewrites; then the trigger off: the button "
				"%s)\n", retriggers, qPrintable(rowBefore), qPrintable(cornerBefore), rewrites, backToHold ? "Hold / Live" : "?");
		check(retriggers >= 50 && rewrites == 0 && rowBefore == QStringLiteral("Normal · triggered")
						&& cornerBefore == QStringLiteral("Normal · triggered") && backToHold,
				"chart, Trigger: 50 re-triggers in one state rewrite neither the row nor the corner (both \"Normal · "
				"triggered\", no rate); the button is Hold / Live again with the trigger off");
		sine.view->stopTrigger();
		sine.tab.hide();
		clearTriggerSettings();
	}

	/* Trigger v2, the review's fixes: Run arms from now (the Stop cleared the hold-off) and the rate counts only the
	 * crossings after it; while stopped, the row's edge and mode change what Run arms and the picture stays; a wheel
	 * zoom and a press that does not move a view still capturing after T leave it the trigger's (not live, not
	 * stopped); a press on an off-scale level keeps its value; the now edge lies under the marks' lines on both
	 * drawing paths */
	void chartTriggerReview() {
		clearTriggerSettings();
		LoneChart chart(QStringLiteral("REVIEW"), QStringLiteral("V"));
		ChartView *view = chart.view;
		view->setWindow(1);
		view->setSmooth(false);
		double fed = 99.0;
		const auto feed = [&](double until) { /* 1 kHz of a 1 Hz sine (rising through 0.5 at k + 1/12), 0.1 s a frame */
			while (fed < until - 1e-9) {
				MathLines::Samples samples;
				const double to = std::min(until, fed + 0.1);
				for (; fed < to - 1e-9; fed += 0.001) samples[regKey(chart.def)] << QPointF(fed, std::sin(2 * M_PI * fed));
				chart.now = fed;
				chart.tab.frame(samples);
			}
		};
		feed(99.95);
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		view->setFocus();
		QApplication::processEvents();
		view->setWindow(1);
		auto *action = chart.tab.findChild<QAction *>(QStringLiteral("chartTrigger"));
		auto *level = chart.tab.findChild<QLineEdit *>(QStringLiteral("triggerLevel"));
		auto *mode = chart.tab.findChild<QComboBox *>(QStringLiteral("triggerMode"));
		auto *edge = chart.tab.findChild<QComboBox *>(QStringLiteral("triggerEdge"));
		auto *hold = chart.tab.findChild<QPushButton *>(QStringLiteral("hold"));
		if (!action || !level || !mode || !edge || !hold) {
			check(false, "chart, Trigger, the review's fixes: the row's controls and the toolbar's button found");
			return;
		}
		action->setChecked(true);
		const auto setLevel = [level](const char *text) {
			level->setText(QLatin1String(text));
			emit level->editingFinished();
		};
		const auto choose = [](QComboBox *box, int data) {
			box->setCurrentIndex(box->findData(data));
			emit box->activated(box->currentIndex());
		};
		const auto viewEnd = [view] { /* as painted */
			(void) view->grab();
			double t0, t1;
			bool cursors;
			view->range(t0, t1, cursors);
			return t1;
		};
		using Phase = ChartView::TriggerPhase;
		using Mode = ChartView::TriggerMode;

		/* Run after a Stop: from now, the hold-off gone with the Stop */
		choose(mode, int(Mode::Normal));
		setLevel("0.5");
		view->setTriggerHoldoff(0);
		feed(102.5); /* crossings at 100.0833, 101.0833, 102.0833 */
		view->setTriggerHoldoff(10); /* the next would count 10 s after 102.0833 */
		hold->click(); /* Stop */
		feed(106.5);
		int holds = view->triggerHolds();
		hold->click(); /* Run */
		feed(107.3); /* 107.0833: inside the last crossing's hold-off, counted */
		const bool runNow = view->triggerHolds() == holds + 1 && std::fabs(view->triggeredAt() - (107 + 1.0 / 12)) < 1e-6;
		view->setTriggerHoldoff(0);
		feed(109.3); /* 108.0833, 109.0833 */
		std::printf("     (Run after a Stop with a 10 s hold-off: the first crossing counted %d)\n", int(runNow));
		check(runNow, "chart, Trigger: Run after a Stop arms from now (the Stop cleared the hold-off: the first crossing "
				"counts at once)");

		/* stopped: the row's edge and mode are what Run arms; the picture and its T stay */
		hold->click(); /* Stop */
		const double stoppedAt = view->triggeredAt(), stoppedEnd = viewEnd();
		choose(edge, int(ChartView::TriggerEdge::Falling));
		choose(mode, int(Mode::Auto));
		feed(110.5);
		const bool keptStopped = view->triggerPhase() == Phase::Stopped && view->triggeredAt() == stoppedAt && !view->live()
				&& viewEnd() == stoppedEnd && !view->triggerTag().isEmpty() && view->triggerEdge() == ChartView::TriggerEdge::Falling
				&& view->triggerMode() == Mode::Auto && hold->text() == QStringLiteral("Run");
		hold->click(); /* Run: Auto runs live */
		const bool runAuto = view->live() && view->triggerPhase() == Phase::FreeRunning;
		std::printf("     (stopped, the edge and the mode changed in the row: the stop, the view and its T kept %d; Run: "
				"Auto live %d)\n", int(keptStopped), int(runAuto));
		check(keptStopped && runAuto, "chart, Trigger: while stopped, a change of the row's edge or mode stays stopped (the "
				"picture and its T kept until Run, as the chart's edge symbol does); Run then arms in the new mode");

		/* a wheel zoom over a view still capturing after T: still the trigger's, in Normal and in Single */
		choose(edge, int(ChartView::TriggerEdge::Rising));
		choose(mode, int(Mode::Normal));
		const auto wheel = [view] {
			const QRectF plot = view->lastPlot();
			const QPointF inside(plot.left() + 0.5 * plot.width(), plot.top() + 0.75 * plot.height());
			QWheelEvent zoom(inside, view->mapToGlobal(inside), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
					Qt::NoScrollPhase, false);
			QApplication::sendEvent(view, &zoom);
			(void) view->grab();
		};
		feed(111.2); /* 111.0833: held, filling until 111.8833 */
		bool zoomKept = view->triggerCapturing();
		wheel();
		zoomKept = zoomKept && !view->live() && view->triggerRunning() && view->triggerCapturing() && view->window() < 1
				&& view->stateFullText() == QStringLiteral("Normal · triggered");
		view->setWindow(1);
		choose(mode, int(Mode::Single));
		feed(112.2); /* 112.0833: Single's crossing, filling */
		bool singleKept = view->triggerCapturing() && view->triggerPhase() == Phase::Done;
		wheel();
		singleKept = singleKept && !view->live() && view->triggerPhase() == Phase::Done && view->triggerCapturing()
				&& view->stateFullText() == QStringLiteral("Single · complete, capturing after T");
		std::printf("     (a wheel zoom while capturing: Normal kept %d, Single kept %d; the corner \"%s\")\n", int(zoomKept),
				int(singleKept), qPrintable(view->stateFullText()));
		check(zoomKept && singleKept, "chart, Trigger: a wheel zoom over a view still capturing after T keeps it the "
				"trigger's (not live: Normal does not roll, Single keeps its capture)");
		view->setWindow(1);

		/* a press on the plot with no move across it (a 1 px jitter up), while the view captures: nothing stops */
		choose(mode, int(Mode::Normal));
		feed(113.2); /* 113.0833 */
		(void) view->grab();
		const QRectF area = view->lastPlot();
		const QPoint grip(int(area.left() + 0.3 * area.width()), int(view->triggerLineY() < area.center().y()
				? area.bottom() - 15 : area.top() + 15));
		const QPointF jitter(grip.x(), grip.y() - 1);
		const bool capturing = view->triggerCapturing();
		const double endBefore = viewEnd();
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, grip);
		QMouseEvent still(QEvent::MouseMove, jitter, view->mapToGlobal(jitter), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(view, &still);
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, jitter.toPoint());
		const bool pressKept = capturing && view->triggerPhase() == Phase::Triggered && view->triggerRunning()
				&& !view->live() && viewEnd() == endBefore && hold->text() == QStringLiteral("Stop");
		std::printf("     (a press without a move while capturing: %s, live %d)\n", pressKept ? "still triggered"
				: qPrintable(view->stateFullText()), int(view->live()));
		check(pressKept, "chart, Trigger: a press on the plot that does not move the view (a 1 px jitter) while it captures "
				"after T stops nothing and keeps the view");

		/* an off-scale level: a press on its handle with a jitter keeps it; a drag moves it */
		setLevel("2"); /* above the sine: pinned to the top edge */
		(void) view->grab();
		(void) view->grab();
		const bool offScale = view->triggerLevelOffScale() == 1;
		const QRectF pinned = view->triggerLevelTag();
		const QPoint handle(int(pinned.left()) + 6, int(pinned.center().y()));
		const QPointF shaken(handle.x() + 2, handle.y() + 1);
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, handle);
		QMouseEvent shake(QEvent::MouseMove, shaken, view->mapToGlobal(shaken), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(view, &shake);
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, shaken.toPoint());
		(void) view->grab();
		const double keptLevel = view->triggerLevel();
		const QPointF to(handle.x(), view->yOfValue(0.6) + (handle.y() - view->triggerLineY()));
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, handle);
		QMouseEvent drag(QEvent::MouseMove, to, view->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(view, &drag);
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to.toPoint());
		(void) view->grab();
		std::printf("     (an off-scale level %d: after a press with a jitter %g, after a drag %g)\n", int(offScale), keptLevel,
				view->triggerLevel());
		check(offScale && keptLevel == 2 && std::fabs(view->triggerLevel() - 0.6) < 0.02, "chart, Trigger: a press on an "
				"off-scale level's handle with a small jitter keeps its value (not the lane's edge value); a drag still "
				"moves it");

		/* the now edge under the marks' lines: cursor A on the newest sample, its dashes over the edge, on the CPU and
		 * on the card alike */
		setLevel("0.5");
		feed(115.2); /* 115.0833: filling */
		const double newest = fed - 0.001;
		QEvent away(QEvent::Leave); /* no crosshair */
		QApplication::sendEvent(view, &away);
		view->setCursors(newest, newest - 0.5);
		(void) view->grab();
		const QVector<QLineF> edges = view->nowEdges();
		const QColor accent = Theme::colors().accent;
		const auto accentLike = [accent](const QColor &p) {
			const int hue = std::abs(p.hsvHue() - accent.hsvHue());
			return p.hsvSaturation() > 0.8 * accent.hsvSaturation() && std::min(hue, 360 - hue) < 25;
		};
		/* the share of the edge's rows (clear of the tags at the top and the bottom) where the cursor's colour is on top;
		 * origin: the view's top left in the picture's pixels */
		const auto accentShare = [&](const QImage &picture, QPointF origin) {
			if (edges.size() != 1) return 0.0;
			const double dpr = view->devicePixelRatioF();
			const int x = int(std::lround(edges[0].x1() * dpr + origin.x() - 0.5));
			int rows = 0, hits = 0;
			for (int y = int((edges[0].y1() + 24) * dpr + origin.y()); y < int((edges[0].y2() - 24) * dpr + origin.y()); y++) {
				if (y < 0 || y >= picture.height()) continue;
				rows++;
				bool hit = false;
				for (int dx = -1; dx <= 1 && !hit; dx++)
					if (x + dx >= 0 && x + dx < picture.width()) hit = accentLike(picture.pixelColor(x + dx, y));
				if (hit) hits++;
			}
			return rows > 0 ? double(hits) / rows : 0.0;
		};
		const double cpuShare = accentShare(view->grab().toImage(), QPointF());
		const QVector<GpuLines::Adapter> adapters = GpuLines::adapters();
		double cardShare = -1;
		if (!adapters.isEmpty()) {
			view->setDrawing(adapters.first().dedicated ? ChartView::Drawing::Dedicated : ChartView::Drawing::Internal);
			if (QTest::qWaitFor([view] { return !view->openingGpu(); }, 10000) && view->drawsOnGpu()) {
				for (int k = 0; k < 3; k++) {
					view->repaint();
					QApplication::processEvents();
				}
				QRect at;
				const QImage card = view->gpuPicture(&at).convertToFormat(QImage::Format_RGB32);
				const QPointF origin = QPointF(view->mapTo(&chart.tab, QPoint(0, 0))) * view->devicePixelRatioF()
						- QPointF(at.topLeft());
				if (view->plotOnCard()) cardShare = accentShare(card, origin);
			}
			view->setDrawing(ChartView::Drawing::Cpu);
		}
		std::printf("     (cursor A's colour on the now edge's column: %.0f %% of its rows on the CPU, %s on the card)\n",
				cpuShare * 100, cardShare < 0 ? "no card" : qPrintable(QStringLiteral("%1 %").arg(std::lround(cardShare * 100))));
		check(edges.size() == 1 && cpuShare >= 0.45 && (adapters.isEmpty() || (cardShare >= 0.45
						&& std::fabs(cardShare - cpuShare) < 0.15)),
				"chart, Trigger: the now edge lies under the marks' lines (cursor A's dashes over it), on the CPU and on the "
				"card alike");
		view->clearCursors();
		action->setChecked(false);
		chart.tab.hide();
		clearTriggerSettings();
	}

	/* Trigger v2, per line: each line keeps its level and edge by its name (switching the line watched keeps the
	 * others'), saved and read back by a new tab; a line never set starts at its mid-range; armed from a chip's menu
	 * (Trigger on this line); the level a dashed line in its lane with a tag naming the line and the level in its unit,
	 * dragged by the tag with Lanes on and off; the tag's edge symbol cycles the edge (a hand, lit, a tooltip) and the
	 * panel follows */
	void chartTriggerLines() {
		clearTriggerSettings();
		TriggerPair pair;
		ChartView *view = pair.view;
		pair.feed(99.95);
		pair.tab.show();
		(void) QTest::qWaitForWindowExposed(&pair.tab);
		(void) view->grab();
		auto *action = pair.tab.findChild<QAction *>(QStringLiteral("chartTrigger"));
		auto *row = pair.tab.findChild<QWidget *>(QStringLiteral("triggerRow"));
		auto *line = pair.tab.findChild<QComboBox *>(QStringLiteral("triggerLine"));
		auto *level = pair.tab.findChild<QLineEdit *>(QStringLiteral("triggerLevel"));
		auto *edge = pair.tab.findChild<QComboBox *>(QStringLiteral("triggerEdge"));
		if (!action || !row || !line || !level || !edge) {
			check(false, "chart, Trigger per line: the row's controls found");
			return;
		}
		/* a line never set: its mid-range in view (AMPS 0.5 +- 0.25), rising */
		const ChartView::TriggerSettings fresh = view->triggerSettings(pair.ampsKey());
		const bool midRange = std::fabs(fresh.level - 0.5) < 0.01 && fresh.edge == ChartView::TriggerEdge::Rising;

		/* armed from AMPS's chip menu: the entry is there, enabled, with a tooltip; the trigger on, on AMPS, the panel too */
		pair.tab.showLineMenu(pair.ampsKey(), QPoint(0, 0));
		QMenu *menu = pair.tab.lineMenu();
		QAction *watch = menu ? menu->findChild<QAction *>(QStringLiteral("triggerOnLine")) : nullptr;
		const bool offered = watch && watch->isVisible() && watch->isEnabled() && watch->text() == QStringLiteral("Trigger on this line")
				&& watch->toolTip().contains(QLatin1String("AMPS"));
		if (menu) menu->hide();
		if (watch) watch->trigger();
		const bool armed = action->isChecked() && row->isVisible() && view->triggerOn() && view->triggerKey() == pair.ampsKey()
				&& view->triggerArmed() && line->currentData().toInt() == pair.ampsKey()
				&& std::fabs(level->text().toDouble() - view->triggerLevel()) < 1e-5 && std::fabs(view->triggerLevel() - 0.5) < 0.01;
		check(midRange && offered && armed, "chart, Trigger per line: a right-click on a line's chip offers Trigger on this "
				"line; it turns the trigger on, armed on that line at its mid-range (rising), the panel showing the same");

		/* each its own: AMPS 0.6 falling, VOLTS 6 either; back to AMPS: its own again; both saved by name */
		level->setText(QStringLiteral("0.6"));
		emit level->editingFinished();
		edge->setCurrentIndex(edge->findData(int(ChartView::TriggerEdge::Falling)));
		emit edge->activated(edge->currentIndex());
		line->setCurrentIndex(line->findData(pair.voltsKey()));
		emit line->activated(line->currentIndex());
		const bool voltsFresh = view->triggerKey() == pair.voltsKey() && std::fabs(view->triggerLevel() - 5) < 0.05
				&& std::fabs(level->text().toDouble() - view->triggerLevel()) < 1e-5;
		level->setText(QStringLiteral("6"));
		emit level->editingFinished();
		edge->setCurrentIndex(edge->findData(int(ChartView::TriggerEdge::Either)));
		emit edge->activated(edge->currentIndex());
		line->setCurrentIndex(line->findData(pair.ampsKey()));
		emit line->activated(line->currentIndex());
		const bool ampsKept = view->triggerKey() == pair.ampsKey() && view->triggerLevel() == 0.6
				&& view->triggerEdge() == ChartView::TriggerEdge::Falling && level->text() == QStringLiteral("0.6")
				&& edge->currentData().toInt() == int(ChartView::TriggerEdge::Falling);
		const QStringList saved = QSettings().value(QStringLiteral("chart/triggerLevels")).toStringList();
		const bool savedBoth = saved == QStringList{ QStringLiteral("AMPS\t0.6\t1"), QStringLiteral("VOLTS\t6\t2") };
		bool readBack = false;
		{
			ChartTab again([&pair] { return pair.now; });
			again.plotRegister(pair.volts, true);
			again.plotRegister(pair.amps, true);
			const ChartView::TriggerSettings a = again.view()->triggerSettings(pair.ampsKey());
			const ChartView::TriggerSettings v = again.view()->triggerSettings(pair.voltsKey());
			readBack = a.level == 0.6 && a.edge == ChartView::TriggerEdge::Falling && v.level == 6
					&& v.edge == ChartView::TriggerEdge::Either;
		}
		if (!savedBoth || !voltsFresh) std::printf("     (saved: \"%s\"; VOLTS's level when first chosen %g)\n",
				qPrintable(saved.join(QStringLiteral(" | "))), view->triggerLevel());
		check(voltsFresh && ampsKept && savedBoth && readBack, "chart, Trigger per line: each line keeps its own level "
				"and edge (another line chosen starts at its own, the first one's come back with it), saved by name "
				"(chart/triggerLevels) and read back by a new tab");

		/* the tab: the level in its unit and the edge's arrow, in the margin right of the plot at the level's
		 * height (none of it over the plot); its tooltip the line, the level and the edge in words */
		(void) view->grab();
		const QRectF tag = view->triggerLevelTag();
		const bool tagged = view->triggerTagText() == QStringLiteral("0.6 A ↓") && !tag.isEmpty()
				&& std::fabs(tag.center().y() - view->triggerLineY()) <= 1 && tag.left() > view->lastPlot().right() + 2
				&& tag.right() <= view->width() && view->triggerEdgeButton().right() == tag.right()
				&& view->toolTipAt(tag.center()).startsWith(QStringLiteral("AMPS 0.6 A, falling\nDrag: the trigger level"));
		/* the level dragged by its tag, without lanes: it follows the mouse from where it was taken */
		const auto drag = [view](QPointF from, QPointF to) {
			QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from.toPoint());
			QMouseEvent move(QEvent::MouseMove, to, view->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
			QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to.toPoint());
			(void) view->grab();
		};
		/* grabbed near its top, not on the line: the level moves by the mouse's move, it does not jump to the mouse */
		const QPointF grip(tag.left() + 8, tag.top() + 3);
		const double offset = grip.y() - view->triggerLineY();
		drag(grip, QPointF(grip.x(), view->yOfValue(0.7) + offset));
		const double plain = view->triggerLevel();
		const bool draggedPlain = std::fabs(offset) >= 4 && std::fabs(view->triggerLineY() - view->yOfValue(0.7)) <= 1
				&& std::fabs(plain - 0.7) < 0.01 && std::fabs(level->text().toDouble() - plain) < 1e-5
				&& std::fabs(QSettings().value(QStringLiteral("chart/triggerLevels")).toStringList().value(0)
						.section(QLatin1Char('\t'), 1, 1).toDouble() - plain) < 1e-9;
		/* with Lanes: the level's line and tag in AMPS's lane, dragged there */
		view->setLanes(true);
		(void) view->grab();
		int ampsLane = -1;
		for (int i = 0; i < view->laneCount(); i++)
			if (view->laneLabel(i) == QLatin1String("A")) ampsLane = i;
		const QRectF lane = ampsLane >= 0 ? view->laneRect(ampsLane) : QRectF();
		const QRectF laneTag = view->triggerLevelTag();
		const bool inLane = ampsLane >= 0 && laneTag.top() >= lane.top() && laneTag.bottom() <= lane.bottom()
				&& laneTag.left() > lane.right() && view->triggerLineY() >= lane.top()
				&& view->triggerLineY() <= lane.bottom() && std::fabs(view->triggerLineY() - view->laneYOfValue(ampsLane, plain)) < 1;
		const QPointF laneGrip(laneTag.left() + 8, laneTag.top() + 3);
		const double laneOffset = laneGrip.y() - view->triggerLineY();
		/* a third of a pixel off: a level with more digits than the box shows (the check after the edge's) */
		drag(laneGrip, QPointF(laneGrip.x(), view->laneYOfValue(ampsLane, 0.4) + laneOffset + 0.37));
		const double inLaneLevel = view->triggerLevel();
		const bool draggedInLane = inLane && std::fabs(laneOffset) >= 4
				&& std::fabs(view->triggerLineY() - view->laneYOfValue(ampsLane, 0.4)) <= 1 && std::fabs(inLaneLevel - 0.4) < 0.01
				&& std::fabs(level->text().toDouble() - inLaneLevel) < 1e-5;
		if (!tagged || !draggedPlain || !draggedInLane)
			std::printf("     (the tag \"%s\" at %.1f,%.1f %.0fx%.0f, the line at %.1f; dragged to %g, in its lane %d to %g)\n",
					qPrintable(view->triggerTagText()), tag.x(), tag.y(), tag.width(), tag.height(), view->triggerLineY(), plain,
					int(inLane), inLaneLevel);
		check(tagged && draggedPlain && draggedInLane, "chart, Trigger per line: the level's tab right of the plot at its "
				"line's height (\"0.6 A ↓\", the words in its tooltip); dragged by it (taken off its middle), the level "
				"follows the mouse's move from where it was taken, without lanes and in the line's own lane's band with "
				"Lanes on; the panel and the setting follow");

		/* the edge symbol: a hand and its tooltip over the tag, the symbol lit under the mouse; a click takes the next
		 * edge (falling -> either), the panel and the setting follow, the tag says so */
		const QRectF symbol = view->triggerEdgeButton();
		const auto moveTo = [view](QPointF at) {
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
		};
		const auto symbolPicture = [view, &symbol] {
			const QImage whole = view->grab().toImage();
			const qreal dpr = whole.devicePixelRatio();
			return whole.copy(QRectF(symbol.topLeft() * dpr, symbol.size() * dpr).toAlignedRect());
		};
		moveTo(QPointF(view->triggerLevelTag().left() + 8, symbol.center().y()));
		const bool handOnTag = view->cursor().shape() == Qt::PointingHandCursor && !view->triggerEdgeHovered()
				&& view->triggerTabHovered() && view->toolTipAt(QPointF(view->triggerLevelTag().left() + 8, symbol.center().y()))
					.endsWith(QStringLiteral("Drag: the trigger level · Click the arrow: the edge (rising, falling, either)"));
		const QImage rest = symbolPicture();
		moveTo(symbol.center());
		const QImage lit = symbolPicture();
		const bool handOnSymbol = view->cursor().shape() == Qt::PointingHandCursor && view->triggerEdgeHovered() && rest != lit
				&& view->toolTipAt(symbol.center()).endsWith(QStringLiteral("Click the arrow: the edge (rising, falling, either)"));
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, symbol.center().toPoint());
		(void) view->grab();
		const bool cycled = view->triggerEdge() == ChartView::TriggerEdge::Either
				&& edge->currentData().toInt() == int(ChartView::TriggerEdge::Either)
				&& view->triggerTagText().endsWith(QStringLiteral(" ↕")) && std::fabs(view->triggerLevel() - inLaneLevel) < 1e-12
				&& QSettings().value(QStringLiteral("chart/triggerLevels")).toStringList().value(0).endsWith(QStringLiteral("\t2"));
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, symbol.center().toPoint());
		const bool cycledOn = view->triggerEdge() == ChartView::TriggerEdge::Rising;
		/* the edge's arrow in the middle of its part (the owner saw the font's ↕ right of the centre): the text's colour
		 * across the part, its ink's middle within a device pixel and a half of the part's middle, for each edge */
		{
			moveTo(QPointF(view->lastPlot().center().x(), view->lastPlot().top() + 10)); /* the part not lit */
			bool centred = true;
			QString seen;
			for (int k = 0; k < 3; k++) {
				(void) view->grab(); /* the tab drawn with this edge */
				const QRectF part = view->triggerEdgeButton();
				const QImage whole = view->grab().toImage();
				const qreal dpr = whole.devicePixelRatio();
				const QRect area = QRectF(part.topLeft() * dpr, part.size() * dpr).toAlignedRect().adjusted(int(3 * dpr), 2, -2, -2);
				const QColor text = Theme::colors().text;
				int left = 1 << 30, right = -1, top = 1 << 30, bottom = -1;
				for (int y = area.top(); y <= area.bottom(); y++)
					for (int x = area.left(); x <= area.right(); x++) {
						const QColor px = whole.pixelColor(x, y);
						if (std::abs(px.red() - text.red()) + std::abs(px.green() - text.green()) + std::abs(px.blue() - text.blue()) > 120)
							continue;
						left = std::min(left, x), right = std::max(right, x), top = std::min(top, y), bottom = std::max(bottom, y);
					}
				const double inkX = (left + right) / 2.0, inkY = (top + bottom) / 2.0;
				const double midX = part.center().x() * dpr, midY = part.center().y() * dpr;
				centred = centred && right >= 0 && std::fabs(inkX - midX) <= 1.5 + dpr / 2 && std::fabs(inkY - midY) <= 1.5 + dpr / 2;
				seen += QStringLiteral(" %1,%2").arg(inkX - midX, 0, 'f', 1).arg(inkY - midY, 0, 'f', 1);
				QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, part.center().toPoint());
				moveTo(QPointF(view->lastPlot().center().x(), view->lastPlot().top() + 10));
			}
			if (!centred) std::printf("     (the arrow's ink off the part's middle, device px, rising falling either:%s)\n", qPrintable(seen));
			check(centred && view->triggerEdge() == ChartView::TriggerEdge::Rising, "chart, Trigger per line: the edge's arrow "
					"(↑ ↓ ↕, drawn as lines) in the middle of its part of the level's tab, for each edge");
		}
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* the level's marks in its lane, for a look, in both themes */
			pair.tab.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_trigger_lane.png"));
			moveTo(QPointF(view->lastPlot().center().x(), view->lastPlot().top() + 10));
			const bool wasDark = Theme::isDark();
			for (const bool dark : { true, false }) {
				Theme::apply(*qApp, dark);
				pair.tab.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_trigger_lane_%1.png")
						.arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
			}
			Theme::apply(*qApp, wasDark);
		}
		check(handOnTag && handOnSymbol && cycled && cycledOn, "chart, Trigger per line: over the level's tab a pointing hand, "
				"the tab lit and its tooltip; its arrow lit under the mouse, a click takes the next edge (either, then rising), "
				"the panel, the setting and the tab's arrow follow");

		/* the level as dragged has more digits than the box shows (6): a change of the mode in the row keeps it */
		auto *rowMode = pair.tab.findChild<QComboBox *>(QStringLiteral("triggerMode"));
		const double exact = view->triggerLevel();
		const bool manyDigits = QString::number(exact, 'g', 6).toDouble() != exact;
		if (rowMode) {
			rowMode->setCurrentIndex(rowMode->findData(int(ChartView::TriggerMode::Single)));
			emit rowMode->activated(rowMode->currentIndex());
		}
		const bool exactKept = rowMode && view->triggerMode() == ChartView::TriggerMode::Single && view->triggerLevel() == exact
				&& QSettings().value(QStringLiteral("chart/triggerLevels")).toStringList().value(0).section(QLatin1Char('\t'), 1, 1)
						.toDouble() == exact;
		std::printf("     (the level dragged %.17g, the box \"%s\"; after the mode changed in the row %.17g)\n", exact,
				qPrintable(level->text()), view->triggerLevel());
		check(manyDigits && exactKept, "chart, Trigger per line: a level dragged keeps all its digits through a change of the "
				"mode in the row (the box's 6 digits are written back only when typed)");
		view->setLanes(false);
		action->setChecked(false);
		pair.tab.hide();

		/* read back by a new tab: the hold-off and the crossing's place (the row too); the one level saved before each
		 * line kept its own (chart/triggerLevel, triggerEdge, triggerLine) taken over by its line, and not over the
		 * levels saved since (chart/triggerLevels) */
		clearTriggerSettings();
		QSettings settings;
		settings.setValue(QStringLiteral("chart/triggerHoldoff"), 0.005);
		settings.setValue(QStringLiteral("chart/triggerPosition"), 0.35);
		settings.setValue(QStringLiteral("chart/triggerLine"), QStringLiteral("AMPS"));
		settings.setValue(QStringLiteral("chart/triggerLevel"), 0.25);
		settings.setValue(QStringLiteral("chart/triggerEdge"), 1);
		bool placesBack = false;
		QStringList takenOver, notAgain;
		{
			ChartTab again([&pair] { return pair.now; });
			auto *againHoldoff = again.findChild<QComboBox *>(QStringLiteral("triggerHoldoff"));
			auto *againPosition = again.findChild<QSpinBox *>(QStringLiteral("triggerPosition"));
			placesBack = again.view()->triggerHoldoff() == 0.005 && again.view()->triggerPosition() == 0.35 && againHoldoff
					&& againHoldoff->currentText() == QStringLiteral("5 ms") && againPosition && againPosition->value() == 35;
			takenOver = again.view()->triggerSettingsTexts();
		}
		settings.setValue(QStringLiteral("chart/triggerLevels"), QStringList{ QStringLiteral("VOLTS\t4\t0") });
		{
			ChartTab again([&pair] { return pair.now; });
			notAgain = again.view()->triggerSettingsTexts();
		}
		std::printf("     (read back: \"%s\" from the old settings; \"%s\" with the new beside them)\n",
				qPrintable(takenOver.join(QStringLiteral(" | "))), qPrintable(notAgain.join(QStringLiteral(" | "))));
		check(placesBack && takenOver == QStringList{ QStringLiteral("AMPS\t0.25\t1") }
						&& notAgain == QStringList{ QStringLiteral("VOLTS\t4\t0") },
				"chart, Trigger settings: the hold-off (5 ms) and the crossing's place (35 %) read back by a new tab, its row "
				"too; the one level saved before (its line, level and edge) taken over by its line, but not over the levels "
				"saved per line since");
		clearTriggerSettings();
	}

	/* Trigger v2, what the eye sees (the review's rows 5, 6, 8, 13, 14, 16): the level box's unit and one way of writing
	 * a level; the row's "position"; a level beyond the lane's range pinned to its edge, dotted, ▲ or ▼ and said in the
	 * tag and the row, the Auto range not widened; the level's tag a handle that opens under the mouse and while
	 * dragged, its triangle solid or hollow; the T drawn as the tag with a tooltip and a hover; the row's state never
	 * setting the window's least width, in English and Arabic */
	void chartTriggerLook() {
		clearTriggerSettings();
		/* a window of 1 s in the tab's own box too: its text is applied again when it loses the focus */
		const QVariant savedWindow = QSettings().value(QStringLiteral("chart/window"));
		QSettings().setValue(QStringLiteral("chart/window"), 1.0);
		LoneChart chart(QStringLiteral("LOOK"), QStringLiteral("A"));
		ChartView *view = chart.view;
		view->setWindow(1);
		view->setSmooth(false);
		double fed = 99.0;
		const auto feed = [&](double until) { /* 1 kHz of 0.5 + 0.25 sin(2 pi t), the clock with it */
			MathLines::Samples samples;
			for (; fed < until - 1e-9; fed += 0.001) samples[regKey(chart.def)] << QPointF(fed, 0.5 + 0.25 * std::sin(2 * M_PI * fed));
			chart.now = fed;
			chart.tab.frame(samples);
		};
		const auto settle = [&] {
			for (int k = 0; k < 3; k++) {
				(void) view->grab();
				QApplication::processEvents();
			}
			chart.tab.refreshStatus();
		};
		feed(99.95);
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		auto *action = chart.tab.findChild<QAction *>(QStringLiteral("chartTrigger"));
		auto *row = chart.tab.findChild<QWidget *>(QStringLiteral("triggerRow"));
		auto *line = chart.tab.findChild<QComboBox *>(QStringLiteral("triggerLine"));
		auto *level = chart.tab.findChild<QLineEdit *>(QStringLiteral("triggerLevel"));
		auto *unit = chart.tab.findChild<QLabel *>(QStringLiteral("triggerUnit"));
		auto *state = chart.tab.findChild<ElidedLabel *>(QStringLiteral("triggerState"));
		if (!action || !row || !line || !level || !unit || !state) {
			check(false, "chart, Trigger's look: the row's controls found");
			return;
		}
		action->setChecked(true);
		settle();
		const QColor color = view->lines().value(0).color;

		/* the level box: the line's unit after it, muted; the box, the tag and the drag write a level alike */
		view->setTriggerLevel(0.523456789);
		emit line->activated(line->currentIndex()); /* the boxes show the line's settings again */
		settle();
		const bool unitShown = unit->isVisible() && unit->text() == QStringLiteral("A")
				&& unit->x() > level->x() + level->width() - 1 && unit->x() < level->x() + level->width() + 12;
		const bool oneWay = ChartView::levelText(0.523456789) == QStringLiteral("0.523457") && level->text() == QStringLiteral("0.523457")
				&& view->triggerTagText() == QStringLiteral("0.523457 A ↑") && view->triggerLevel() == 0.523456789
				&& ChartView::levelText(10) == QStringLiteral("10") && ChartView::levelText(12.34) == QStringLiteral("12.34");
		if (!unitShown || !oneWay)
			std::printf("     (the unit \"%s\" at %d, the box \"%s\" ends at %d; the tag \"%s\")\n", qPrintable(unit->text()),
					unit->x(), qPrintable(level->text()), level->x() + level->width(), qPrintable(view->triggerTagText()));
		check(unitShown && oneWay, "chart, Trigger's look: the line's unit after the level's box; the box and the tag write "
				"a level as set, 6 digits through one helper (0.523456789 -> 0.523457, 12.34 stays 12.34, not 12.3)");

		/* the row names the crossing's place "position", with a tooltip, apart from the hold-off */
		bool positionNamed = false, atGone = true;
		for (QLabel *label : row->findChildren<QLabel *>()) {
			if (label->text() == QStringLiteral("position"))
				positionNamed = label->toolTip() == QStringLiteral("Where the crossing sits in the window; or drag the T ▼ "
						"flag above the chart");
			if (label->text() == QStringLiteral("at")) atGone = false;
		}
		check(positionNamed && atGone, "chart, Trigger's look: the row's label of the crossing's place reads \"position\" "
				"(not \"at\") and its tooltip says what it is");

		/* off scale: the level line's dots and dashes along it, from the line's colour (its hue, not the greys around) */
		const auto meanRun = [view, color](double y) {
			const QImage image = view->grab().toImage();
			const qreal dpr = image.devicePixelRatio();
			const QRectF plot = view->lastPlot();
			int on = 0, runs = 0;
			bool was = false;
			const double start = plot.left() + plot.width() * 0.25; /* clear of the legend and the state corner */
			for (int x = int(start * dpr); x < int((start + 300) * dpr); x++) {
				bool hit = false;
				for (int dy = -2; dy <= 2 && !hit; dy++) {
					const int yy = int(std::floor(y * dpr)) + dy;
					if (yy < 0 || yy >= image.height()) continue;
					const QColor p = image.pixelColor(x, yy);
					const int hue = std::abs(p.hsvHue() - color.hsvHue());
					hit = p.hsvSaturation() > color.hsvSaturation() / 2 && std::min(hue, 360 - hue) < 25;
				}
				if (hit) {
					on++;
					if (!was) runs++;
				}
				was = hit;
			}
			return runs ? on / double(runs) / dpr : 0.0;
		};
		view->setTriggerLevel(0.5);
		settle();
		const double lo = view->yLo(), hi = view->yHi(), dashRun = meanRun(view->triggerLineY());
		view->setTriggerLevel(10);
		settle();
		const QRectF plot = view->lastPlot();
		const double dotRun = meanRun(view->triggerLineY());
		const bool above = view->triggerLevelOffScale() == 1 && std::fabs(view->triggerLineY() - plot.top()) <= 1
				&& view->triggerTagText() == QStringLiteral("10 A ↑")
				&& view->toolTipAt(view->triggerLevelTag().center()).startsWith(QStringLiteral("▲ LOOK 10 A, rising (above range)\n"))
				&& std::fabs(view->triggerLevelTag().top() - plot.top()) <= 1
				&& view->triggerLevelBeyondLine() == 1 && chart.tab.triggerState() == QStringLiteral("waiting: level above the line's range")
				&& state->fullText() == chart.tab.triggerState() && std::fabs(view->yLo() - lo) < 0.02 * (hi - lo)
				&& std::fabs(view->yHi() - hi) < 0.02 * (hi - lo)
				&& dashRun >= 3.5 && dotRun > 0 && dotRun <= 2.6;
		std::printf("     (in range: dashes of %.1f px; at 10 A: the line at %.1f (the plot's top %.1f), dots of %.1f px, the tag "
				"\"%s\", the row \"%s\", Y %g .. %g (was %g .. %g))\n", dashRun, view->triggerLineY(), plot.top(), dotRun,
				qPrintable(view->triggerTagText()), qPrintable(state->fullText()), view->yLo(), view->yHi(), lo, hi);
		check(above, "chart, Trigger's look: a level above the lane's range is pinned to its top edge, dotted (not dashed), "
				"its tab at the top (its pointer a ▲, its tooltip \"▲ ... (above range)\"), the row \"waiting: level above the "
				"line's range\"; the Auto range not widened");

		view->setTriggerLevel(-10);
		settle();
		const bool below = view->triggerLevelOffScale() == -1 && std::fabs(view->triggerLineY() - view->lastPlot().bottom()) <= 1
				&& view->toolTipAt(view->triggerLevelTag().center()).startsWith(QStringLiteral("▼ LOOK -10 A, rising (below range)\n"))
				&& chart.tab.triggerState() == QStringLiteral("waiting: level below the line's range");
		/* dragged back by its handle on the edge: in range, said no more */
		const auto drag = [view](QPointF from, QPointF to) {
			QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from.toPoint());
			QMouseEvent move(QEvent::MouseMove, to, view->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
			QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to.toPoint());
			(void) view->grab();
		};
		const QRectF pinned = view->triggerLevelTag();
		const QPointF grip(pinned.left() + 6, pinned.center().y());
		drag(grip, QPointF(grip.x(), view->yOfValue(0.6) + (grip.y() - view->triggerLineY())));
		settle();
		const bool back = view->triggerLevelOffScale() == 0 && std::fabs(view->triggerLevel() - 0.6) < 0.02
				&& !view->toolTipAt(view->triggerLevelTag().center()).contains(QStringLiteral("range"))
				&& view->triggerLevelBeyondLine() == 0
				&& chart.tab.triggerState() == QStringLiteral("waiting for a crossing");
		if (!below || !back)
			std::printf("     (at -10 A: the line at %.1f, the tag \"%s\"; dragged back to %g: \"%s\", \"%s\")\n",
					view->triggerLineY(), qPrintable(view->triggerTagText()), view->triggerLevel(),
					qPrintable(view->triggerTagText()), qPrintable(chart.tab.triggerState()));
		check(below && back, "chart, Trigger's look: a level below the range pinned to the bottom edge, \"▼ ... (below "
				"range)\" in its tab's tooltip and the row says so; dragged back by its tab into the range, said no more");

		/* the tab: labelled at rest, the same under the mouse (lit, a hand, its tooltip) and while dragged (the level as
		 * set in its text, from where it was taken); about three times the old handle's 32 x 18 px to take the mouse */
		const auto moveTo = [view](QPointF at) {
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
			(void) view->grab();
		};
		const auto tabPicture = [view] {
			const QImage whole = view->grab().toImage();
			const qreal dpr = whole.devicePixelRatio();
			const QRectF r = view->triggerLevelTag();
			return whole.copy(QRectF(r.topLeft() * dpr, r.size() * dpr).toAlignedRect());
		};
		const QPointF away(view->lastPlot().center().x(), view->triggerLineY() > view->lastPlot().center().y()
				? view->lastPlot().top() + 30 : view->lastPlot().bottom() - 30);
		moveTo(away);
		const QRectF rest = view->triggerLevelTag();
		const QImage restPicture = tabPicture();
		const bool large = !rest.isEmpty() && rest.width() * rest.height() >= 2.5 * 32 * 18 && !view->triggerTabHovered()
				&& rest.left() > view->lastPlot().right() + 2;
		moveTo(QPointF(rest.left() + 10, rest.center().y()));
		const bool lit = view->triggerTabHovered() && view->triggerLevelTag() == rest && tabPicture() != restPicture
				&& view->cursor().shape() == Qt::PointingHandCursor
				&& view->toolTipAt(QPointF(rest.left() + 10, rest.center().y())).endsWith(QStringLiteral("Drag: the trigger "
					"level · Click the arrow: the edge (rising, falling, either)"));
		moveTo(away);
		const bool unlit = !view->triggerTabHovered() && tabPicture() == restPicture;
		const QPointF from(rest.left() + 10, rest.center().y());
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from.toPoint());
		const QPointF to(from.x(), view->yOfValue(0.55) + (from.y() - view->triggerLineY()));
		QMouseEvent dragMove(QEvent::MouseMove, to, view->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(view, &dragMove);
		(void) view->grab();
		const bool whileDragged = view->triggerTabHovered() && view->triggerLevelTag().size() == rest.size()
				&& std::fabs(view->triggerLevel() - 0.55) < 0.02
				&& view->triggerTagText() == QStringLiteral("%1 A ↑").arg(ChartView::levelText(view->triggerLevel()));
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to.toPoint());
		moveTo(away);
		const bool after = !view->triggerTabHovered() && view->triggerLevelTag().size() == rest.size();
		if (!large || !lit || !unlit || !whileDragged || !after)
			std::printf("     (the tab %.0fx%.0f at %.0f..%.0f; large %d, lit %d, unlit %d, dragged %d, after %d)\n", rest.width(),
					rest.height(), rest.left(), rest.right(), int(large), int(lit), int(unlit), int(whileDragged), int(after));
		check(large && lit && unlit && whileDragged && after, "chart, Trigger's look: the level's tab is labelled at rest, "
				"right of the plot, about three times the old handle's area; over it a hand, a highlight and its tooltip; "
				"dragged from where it was taken it keeps its size and writes the level as set");

		/* the handle's triangle: hollow before a crossing, solid on the level the view held, hollow after the level moved
		 * until a crossing at the new one */
		const auto filled = [view, color] { /* the triangle's middle: the line's colour (solid) or the surface (hollow) */
			const QImage whole = view->grab().toImage();
			const qreal dpr = whole.devicePixelRatio();
			const QRectF r = view->triggerLevelTag(); /* the pointer: at the line's height, its tip the tab's left */
			const double y = std::clamp(view->triggerLineY(), r.top() + 9, r.bottom() - 9);
			const QColor p = whole.pixelColor(int((r.left() + 3) * dpr), int(y * dpr));
			return std::abs(p.red() - color.red()) + std::abs(p.green() - color.green()) + std::abs(p.blue() - color.blue()) < 40;
		};
		view->setTriggerLevel(0.5);
		settle();
		const bool hollowFirst = !view->triggerHandleSolid() && !filled();
		feed(100.5); /* rising through 0.5 at 100 */
		settle();
		const bool held = view->triggerHolds() >= 1 && std::fabs(view->triggeredAt() - 100) < 0.002;
		const bool solid = held && view->triggerHandleSolid() && filled();
		view->setTriggerLevel(0.6);
		settle();
		const bool hollowMoved = !view->triggerHandleSolid() && !filled();
		const int holds = view->triggerHolds();
		/* the samples wait while the measurements run on the chart's threads: the events let them in */
		for (int k = 0; k < 40 && view->triggerHolds() == holds; k++) {
			feed(fed + 0.05);
			QTest::qWait(10);
		}
		settle();
		const bool solidAgain = view->triggerHolds() > holds && view->triggerHandleSolid() && filled();
		if (!hollowFirst || !solid || !hollowMoved || !solidAgain)
			std::printf("     (hollow first %d, held %d solid %d, moved hollow %d, solid again %d; mode %d, phase %d, running %d, "
					"holds %d, at %.4f, fed to %.3f; measuring %d, armed now %d, newest %.3f, window %g, holdoff %g, level %g)\n",
					int(hollowFirst), int(held), int(solid), int(hollowMoved), int(solidAgain),
					int(view->triggerMode()), int(view->triggerPhase()), int(view->triggerRunning()), view->triggerHolds(),
					view->triggeredAt(), fed, int(view->measuring()), int(view->triggerArmed()), [&] {
						QVector<double> times, values;
						view->lineSamples(chart.key(), 0, 1e9, times, values);
						return times.isEmpty() ? -1.0 : times.back();
					}(), view->window(), view->holdoffSeconds(), view->triggerLevel());
		check(hollowFirst && solid && hollowMoved && solidAgain, "chart, Trigger's look: the handle's triangle is hollow before "
				"a crossing, solid on the level the held view crossed, hollow once the level moves, solid again at a crossing "
				"of the new level");

		/* no T on the curve (the owner: the flag above and the level's marks at the sides show the crossing): over the
		 * crossing the level's line drags as anywhere on it, and the crossing in words is the flag's tooltip's first line */
		const QRectF tTag = view->triggerTag();
		const qint64 ms = view->epochMs() + qint64(std::llround(view->triggeredAt() * 1000));
		const QString tip = QStringLiteral("Trigger point: LOOK crossed 0.6 A, rising, at %1")
				.arg(QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss.zzz")));
		moveTo(QPointF(tTag.center().x(), view->triggerLineY()));
		const bool lineThere = view->triggerTabHovered() && view->cursor().shape() == Qt::SizeVerCursor
				&& !view->toolTipAt(QPointF(tTag.center().x(), view->triggerLineY())).startsWith(QStringLiteral("Trigger point"));
		const QRectF flag = view->triggerPositionMark();
		const bool flagTells = !tTag.isEmpty() && std::fabs(flag.center().x() - tTag.center().x()) < 1
				&& view->toolTipAt(flag.center()).startsWith(tip + QLatin1Char('\n'));
		moveTo(away);
		if (!lineThere || !flagTells)
			std::printf("     (the crossing at %.0f,%.0f; the flag at %.0f; its tooltip \"%s\", wanted it to start \"%s\")\n",
					tTag.center().x(), tTag.center().y(), flag.center().x(), qPrintable(view->toolTipAt(flag.center())),
					qPrintable(tip));
		check(lineThere && flagTells, "chart, Trigger's look: no T on the curve: over the crossing the level's line drags "
				"(lit, a vertical arrow), the flag stands over it and its tooltip starts \"Trigger point: LOOK crossed 0.6 A, "
				"rising, at hh:mm:ss.zzz\"");
		action->setChecked(false);
		chart.tab.hide();

		/* the row's state is cut to its room (its whole text in its tooltip): no state's text widens the window, in
		 * English or in Arabic */
		bool steady = true, tipped = true;
		QString widths;
		for (const QString &code : { QStringLiteral("en"), QStringLiteral("ar") }) {
			language::apply(*qApp, code);
			LoneChart other(QStringLiteral("WIDTH_OF_A_LONG_NAME"), QStringLiteral("A"));
			double t = 99.0;
			const auto feedOther = [&](double until) {
				MathLines::Samples samples;
				for (; t < until - 1e-9; t += 0.001) samples[regKey(other.def)] << QPointF(t, 0.5 + 0.25 * std::sin(2 * M_PI * t));
				other.now = t;
				other.tab.frame(samples);
			};
			feedOther(99.95);
			other.tab.show();
			(void) QTest::qWaitForWindowExposed(&other.tab);
			auto *otherAction = other.tab.findChild<QAction *>(QStringLiteral("chartTrigger"));
			auto *otherState = other.tab.findChild<ElidedLabel *>(QStringLiteral("triggerState"));
			if (!otherAction || !otherState) {
				steady = false;
				continue;
			}
			otherAction->setChecked(true);
			const auto least = [&] {
				for (int k = 0; k < 3; k++) {
					(void) other.view->grab();
					QApplication::processEvents();
				}
				other.tab.refreshStatus();
				QApplication::processEvents();
				return other.tab.minimumSizeHint().width();
			};
			const int waiting = least();
			other.view->setTriggerLevel(10);
			const int offScale = least();
			other.view->setTriggerLevel(0.5);
			other.view->setTrigger(other.key(), ChartView::TriggerMode::Single);
			feedOther(100.5);
			const int stopped = least();
			const QString longest = otherState->fullText();
			other.tab.resize(other.tab.minimumSizeHint().width(), 700);
			QApplication::processEvents();
			tipped = tipped && otherState->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored
					&& otherState->toolTip() == otherState->fullText() && !longest.isEmpty();
			steady = steady && waiting == offScale && waiting == stopped;
			widths += QStringLiteral(" %1: %2 %3 %4 (\"%5\"%6)").arg(code).arg(waiting).arg(offScale).arg(stopped).arg(longest)
					.arg(otherState->isCut() ? QStringLiteral(", cut") : QString());
			otherAction->setChecked(false);
		}
		language::apply(*qApp, QStringLiteral("en"));
		std::printf("     (the tab's least width waiting, off scale, Single complete:%s)\n", qPrintable(widths));
		check(steady && tipped, "chart, Trigger's look: the row's state never sets the window's least width (waiting, off "
				"scale and Single complete alike, in English and Arabic): cut to its room, its whole text in its tooltip");
		clearTriggerSettings();
		if (savedWindow.isValid()) QSettings().setValue(QStringLiteral("chart/window"), savedWindow);
		else QSettings().remove(QStringLiteral("chart/window"));
	}

	/* Trigger v2, what the reference scopes add: the crossing's place at 50 % by default (a saved one kept) and back
	 * there by a double-click on its triangle; the row's list shows each line's colour dot; Find level; Normal waiting
	 * after a capture says when the last was (a fixed text); Force while Normal or Single waits, one button with Arm;
	 * the T on the level's line at the crossing (in its lane); Run in the warn colour while stopped, both themes */
	void chartTriggerScopes() {
		clearTriggerSettings();
		QSettings().remove(QStringLiteral("chart/triggerPosition")); /* none saved: the default */
		LoneChart chart(QStringLiteral("SCOPE"), QStringLiteral("V"));
		ChartView *view = chart.view;
		view->setWindow(1);
		view->setSmooth(false);
		double fed = 99.0, last = NAN;
		bool flat = false; /* the signal stopped: 0 from then on */
		const auto feed = [&](double until) { /* 1 kHz of a 1 Hz sine (rising through 0.5 at k + 1/12), 0.1 s a frame */
			while (fed < until - 1e-9) {
				MathLines::Samples samples;
				const double to = std::min(until, fed + 0.1);
				for (; fed < to - 1e-9; fed += 0.001) {
					samples[regKey(chart.def)] << QPointF(fed, flat ? 0.0 : std::sin(2 * M_PI * fed));
					last = fed;
				}
				chart.now = fed;
				chart.tab.frame(samples);
			}
		};
		feed(99.95);
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		view->setFocus();
		QApplication::processEvents();
		view->setWindow(1);
		auto *action = chart.tab.findChild<QAction *>(QStringLiteral("chartTrigger"));
		auto *row = chart.tab.findChild<QWidget *>(QStringLiteral("triggerRow"));
		auto *line = chart.tab.findChild<QComboBox *>(QStringLiteral("triggerLine"));
		auto *level = chart.tab.findChild<QLineEdit *>(QStringLiteral("triggerLevel"));
		auto *mode = chart.tab.findChild<QComboBox *>(QStringLiteral("triggerMode"));
		auto *position = chart.tab.findChild<QSpinBox *>(QStringLiteral("triggerPosition"));
		auto *arm = chart.tab.findChild<QPushButton *>(QStringLiteral("triggerArm"));
		auto *find = chart.tab.findChild<QPushButton *>(QStringLiteral("triggerFindLevel"));
		auto *hold = chart.tab.findChild<QPushButton *>(QStringLiteral("hold"));
		auto *state = chart.tab.findChild<ElidedLabel *>(QStringLiteral("triggerState"));
		if (!action || !row || !line || !level || !mode || !position || !arm || !find || !hold || !state) {
			check(false, "chart, Trigger, the scopes' additions: the row's controls and the toolbar's button found");
			return;
		}
		action->setChecked(true);
		const auto setLevel = [level](const char *text) {
			level->setText(QLatin1String(text));
			emit level->editingFinished();
		};
		const auto setMode = [mode](ChartView::TriggerMode m) {
			mode->setCurrentIndex(mode->findData(int(m)));
			emit mode->activated(mode->currentIndex());
		};
		const auto near = [](const QColor &a, const QColor &b, int most) {
			return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue()) < most;
		};

		/* the place: 50 % with none saved, a saved one kept; a double-click on the triangle puts it back (its tooltip
		 * says so), and the box and the setting follow */
		(void) view->grab();
		const QRectF plot = view->lastPlot();
		const bool atHalf = view->triggerPosition() == 0.5 && position->value() == 50 && std::fabs(
				view->triggerPositionMark().center().x() - (plot.left() + 0.5 * plot.width())) < 1;
		QSettings().setValue(QStringLiteral("chart/triggerPosition"), 0.35);
		bool savedKept = false;
		{
			LoneChart saved(QStringLiteral("SAVED"), QStringLiteral("V"));
			savedKept = saved.view->triggerPosition() == 0.35;
		}
		position->setValue(30);
		(void) view->grab();
		const QRectF mark = view->triggerPositionMark();
		const QString markTip = view->toolTipAt(mark.center());
		QTest::mouseDClick(view, Qt::LeftButton, Qt::NoModifier, mark.center().toPoint());
		(void) view->grab();
		const bool reset = view->triggerPosition() == ChartView::TRIGGER_AT && position->value() == 50
				&& QSettings().value(QStringLiteral("chart/triggerPosition")).toDouble() == 0.5
				&& markTip.endsWith(QStringLiteral("· Double-click: back to 50 %"));
		std::printf("     (the place by default 0.5: %d, a saved 0.35 kept %d; the triangle's tooltip \"%s\", after a "
				"double-click %.2f)\n", int(atHalf), int(savedKept), qPrintable(markTip), view->triggerPosition());
		check(atHalf && savedKept && reset, "chart, Trigger: the crossing's place is 50 % by default (a saved place kept); "
				"a double-click on its flag puts it back to 50 % (its tooltip says so), the box and the setting follow");

		/* the row's list: the line's colour dot before its name, as on its chip */
		const QIcon icon = line->itemIcon(line->findData(chart.key()));
		const QImage dot = icon.pixmap(line->iconSize()).toImage();
		const QColor lineColor = view->lines().value(0).color;
		const bool dotted = !icon.isNull() && line->iconSize() == QSize(10, 10) && !dot.isNull()
				&& near(dot.pixelColor(dot.width() / 2, dot.height() / 2), lineColor, 30);
		check(dotted, "chart, Trigger: the row's Line list shows each line's colour dot before its name");

		/* Find level: halfway between the line's lowest and highest in view (a sine of +-1: about 0), the same helper as
		 * a line's start; the box shows it */
		setLevel("2");
		find->click();
		const double found = view->triggerLevel();
		const bool findOk = std::fabs(found) < 0.05 && found == view->midRange(chart.key())
				&& level->text() == ChartView::levelText(found)
				&& find->toolTip() == QStringLiteral("Set the level halfway between the line's lowest and highest in view");
		std::printf("     (Find level: %g, the box \"%s\")\n", found, qPrintable(level->text()));
		check(findOk, "chart, Trigger: Find level sets the level halfway between the line's lowest and highest in view "
				"(the rule a line watched first starts with), the box shows it, the button says so in its tooltip");

		/* Normal: held at 100 + 1/12; the T on the level's line at the crossing */
		setMode(ChartView::TriggerMode::Normal);
		setLevel("0.5");
		feed(100.5);
		(void) view->grab();
		const double crossing = view->triggeredAt();
		const QRectF tTag = view->triggerTag();
		double t0, t1;
		bool cursors;
		view->range(t0, t1, cursors);
		const double crossX = plot.left() + (crossing - t0) / (t1 - t0) * plot.width();
		const bool tOnLine = std::fabs(crossing - (100 + 1.0 / 12)) < 1e-6 && !tTag.isEmpty()
				&& std::fabs(tTag.center().y() - view->triggerLineY()) <= 1 && std::fabs(tTag.center().x() - crossX) < 1;
		view->setTriggerLevel(0.8); /* the level moved: the T stays where the line crossed */
		(void) view->grab();
		const bool tStays = view->triggerTag() == tTag && std::fabs(view->triggerLineY() - tTag.center().y()) > 3
				&& std::fabs(view->triggerPositionMark().center().x() - tTag.center().x()) < 1;
		view->setTriggerLevel(0.5);
		/* with lanes: in the watched line's lane, on its level */
		bool tInLane = false;
		{
			TriggerPair pair;
			pair.feed(99.95);
			pair.view->setLanes(true);
			pair.tab.show();
			(void) QTest::qWaitForWindowExposed(&pair.tab);
			pair.view->setTrigger(pair.ampsKey(), 0.5, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Normal);
			pair.feed(101.0);
			(void) pair.view->grab();
			const QRectF tag = pair.view->triggerTag();
			const int lane = pair.view->laneAtY(tag.center().y());
			tInLane = !tag.isEmpty() && lane >= 0 && pair.view->laneLabel(lane) == QStringLiteral("A")
					&& std::fabs(tag.center().y() - pair.view->triggerLineY()) <= 1;
			pair.view->stopTrigger();
			pair.tab.hide();
		}
		std::printf("     (the T at %.1f,%.1f, the level's line at %.1f, the crossing's x %.1f; stays after a move %d, in "
				"the A lane %d)\n", tTag.center().x(), tTag.center().y(), view->triggerLineY(), crossX, int(tStays),
				int(tInLane));
		check(tOnLine && tStays && tInLane, "chart, Trigger: the crossing's place (the flag's x; no T drawn on the curve) is "
				"where the line crossed (its time on the level crossed), in the watched line's lane; a level moved later "
				"leaves it there, the flag over it");

		/* the signal stops: back to waiting after a window plus the hold-off; the row says when the last capture was, a
		 * fixed text; the corner "Normal · waiting" */
		flat = true;
		feed(103.5);
		chart.tab.refreshStatus(); /* the row as its timer writes it */
		const QString lastAt = QStringLiteral("Normal · waiting, last at %1").arg(QDateTime::fromMSecsSinceEpoch(
				view->epochMs() + qint64(std::llround(crossing * 1000))).toString(QStringLiteral("HH:mm:ss")));
		(void) view->grab();
		const QString rowFirst = state->fullText(), cornerFirst = view->stateFullText();
		feed(104.7);
		chart.tab.refreshStatus();
		(void) view->grab();
		const bool lastShown = rowFirst == lastAt && state->fullText() == lastAt && cornerFirst == QStringLiteral(
				"Normal · waiting") && view->stateFullText() == cornerFirst;
		std::printf("     (Normal waiting after a capture: the row \"%s\", the corner \"%s\")\n", qPrintable(rowFirst),
				qPrintable(cornerFirst));
		check(lastShown, "chart, Trigger: Normal back to waiting after a capture says \"Normal · waiting, last at hh:mm:ss\" "
				"in the row, a fixed text (no counter), and \"Normal · waiting\" in the corner");

		/* Force: while Normal waits, the button reads Force; a click holds the view at the newest sample as a crossing
		 * would (its T there); Single waiting the same, then Arm (primary) once complete; one width throughout */
		const int armWidth = arm->width();
		const bool forceShown = arm->isVisibleTo(row) && arm->text() == QStringLiteral("Force")
				&& arm->toolTip() == QStringLiteral("Hold the view now, as if the line crossed");
		const int holds = view->triggerHolds();
		arm->click();
		(void) view->grab();
		const bool forcedNormal = view->triggeredAt() == last && view->triggerHolds() == holds + 1 && !view->live()
				&& view->triggerPhase() == ChartView::TriggerPhase::Triggered && !view->triggerTag().isEmpty()
				&& !arm->isVisibleTo(row);
		setMode(ChartView::TriggerMode::Single);
		feed(105.0);
		chart.tab.refreshStatus();
		const bool singleForce = view->triggerPhase() == ChartView::TriggerPhase::Waiting && arm->text() == QStringLiteral(
				"Force") && arm->width() == armWidth;
		arm->click();
		(void) view->grab();
		const bool forcedSingle = view->triggeredAt() == last && view->triggerPhase() == ChartView::TriggerPhase::Done
				&& arm->text() == QStringLiteral("Arm") && arm->property("primary").toBool() && arm->width() == armWidth
				&& arm->toolTip() == QStringLiteral("Wait for one more crossing");
		std::printf("     (Force: shown %d, Normal held at the newest %d, Single waiting %d, complete %d; width %d)\n",
				int(forceShown), int(forcedNormal), int(singleForce), int(forcedSingle), arm->width());
		check(forceShown && forcedNormal && singleForce && forcedSingle, "chart, Trigger: while Normal or Single waits, the "
				"Arm button reads Force (its tooltip says what it does); a click holds the view at the newest sample, its T "
				"there; Single then reads Arm again; the button keeps one width");

		/* Run in the warn colour while stopped (the corner's amber), in both themes; not after Single's crossing */
		const auto warnPixels = [hold] {
			QApplication::processEvents();
			const QImage picture = hold->grab().toImage();
			const QColor warn = Theme::colors().warn;
			int n = 0;
			for (int y = 0; y < picture.height(); y++)
				for (int x = 0; x < picture.width(); x++) {
					const QColor p = picture.pixelColor(x, y);
					if (std::abs(p.red() - warn.red()) + std::abs(p.green() - warn.green()) + std::abs(p.blue() - warn.blue()) < 40)
						n++;
				}
			return n;
		};
		const bool runPlain = hold->text() == QStringLiteral("Run") && !hold->property("stopped").toBool();
		hold->click(); /* Run */
		hold->click(); /* Stop */
		const bool wasDark = Theme::isDark();
		int stoppedPixels[2] = { 0, 0 }, runningPixels[2] = { 0, 0 };
		for (const bool dark : { false, true }) {
			Theme::apply(*qApp, dark);
			chart.tab.themeChanged();
			stoppedPixels[dark] = warnPixels();
		}
		const bool stoppedWarn = view->triggerPhase() == ChartView::TriggerPhase::Stopped && hold->text() == QStringLiteral(
				"Run") && hold->property("stopped").toBool();
		hold->click(); /* Run */
		for (const bool dark : { false, true }) {
			Theme::apply(*qApp, dark);
			chart.tab.themeChanged();
			runningPixels[dark] = warnPixels();
		}
		Theme::apply(*qApp, wasDark);
		chart.tab.themeChanged();
		const bool runningPlain = !hold->property("stopped").toBool() && hold->text() == QStringLiteral("Stop");
		std::printf("     (the toolbar's button in the warn colour: stopped %d / %d px (light / dark), running %d / %d)\n",
				stoppedPixels[0], stoppedPixels[1], runningPixels[0], runningPixels[1]);
		check(runPlain && stoppedWarn && runningPlain && stoppedPixels[0] >= 30 && stoppedPixels[1] >= 30
				&& runningPixels[0] < 5 && runningPixels[1] < 5, "chart, Trigger: Run on the toolbar's button is drawn in the "
				"warn colour while stopped (as the corner's \"Stopped · Run to arm\"), in both themes; plain after Single's "
				"crossing and while running");
		action->setChecked(false);
		chart.tab.hide();
		clearTriggerSettings();
	}

	/* Trigger v2, the owner's test of the marks (U-18): the level's tab ("0.4 A ↑") in a margin right of the plot and
	 * the position's flag (a T over a ▼) in a strip above it, both there only while the trigger is on, neither over the data;
	 * the tab dragged and its arrow clicked, the flag dragged, clamped and double-clicked; hands, highlights, tooltips;
	 * the old triangle under the time axis gone */
	void chartTriggerMarksOutside() {
		clearTriggerSettings();
		/* the window box says 1 s too: a window saved by an earlier step (a wheel's zoom) came back from the box once
		 * the trigger's row took the focus */
		QSettings().setValue(QStringLiteral("chart/window"), 1.0);
		LoneChart chart(QStringLiteral("TAB"), QStringLiteral("A"));
		ChartView *view = chart.view;
		view->setWindow(1);
		view->setSmooth(false);
		double fed = 99.0;
		const auto feed = [&](double until) { /* 1 kHz of a 1 Hz sine (rising through 0.4 at k + 0.0655), 0.1 s a frame */
			while (fed < until - 1e-9) {
				MathLines::Samples samples;
				const double to = std::min(until, fed + 0.1);
				for (; fed < to - 1e-9; fed += 0.001) samples[regKey(chart.def)] << QPointF(fed, std::sin(2 * M_PI * fed));
				chart.now = fed;
				chart.tab.frame(samples);
			}
		};
		feed(99.95);
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		QApplication::processEvents();
		view->setWindow(1); /* the tab shown sets its own */
		auto *action = chart.tab.findChild<QAction *>(QStringLiteral("chartTrigger"));
		auto *level = chart.tab.findChild<QLineEdit *>(QStringLiteral("triggerLevel"));
		auto *mode = chart.tab.findChild<QComboBox *>(QStringLiteral("triggerMode"));
		auto *position = chart.tab.findChild<QSpinBox *>(QStringLiteral("triggerPosition"));
		if (!action || !level || !mode || !position) {
			check(false, "chart, Trigger's marks outside the data: the row's controls found");
			return;
		}
		(void) view->grab();
		const QRectF without = view->lastPlot();
		action->setChecked(true);
		mode->setCurrentIndex(mode->findData(int(ChartView::TriggerMode::Normal)));
		emit mode->activated(mode->currentIndex());
		level->setText(QStringLiteral("0.4"));
		emit level->editingFinished();
		feed(100.5); /* rising through 0.4 at 100.0655: held, the crossing at 20 % */
		(void) view->grab();
		const QRectF plot = view->lastPlot();
		/* the room: a strip above the plot, a column left of it and a margin right of it, with the trigger on only */
		const bool room = std::fabs((plot.top() - without.top()) - 26) < 0.5 && without.right() - plot.right() > 55
				&& without.right() - plot.right() < 100 && std::fabs(plot.left() - without.left() - 21) < 0.5;

		/* the tab: right of the plot (never over its newest samples), at the level's height, labelled */
		const QRectF tab = view->triggerLevelTag();
		const bool tabOut = !tab.isEmpty() && !tab.intersects(plot) && tab.left() > plot.right() + 2
				&& tab.top() <= view->triggerLineY() && tab.bottom() >= view->triggerLineY() && tab.right() <= view->width();
		const bool labelled = view->triggerTagText() == QStringLiteral("0.4 A ↑");
		/* hover: a hand, the tab lit, its tooltip (the words, then what it does) */
		const auto moveTo = [view](QPointF at) {
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
			(void) view->grab();
		};
		moveTo(QPointF(tab.left() + 12, tab.center().y()));
		const bool tabHover = view->triggerTabHovered() && view->cursor().shape() == Qt::PointingHandCursor
				&& view->toolTipAt(QPointF(tab.left() + 12, tab.center().y())) == QStringLiteral("TAB 0.4 A, rising\nDrag: the "
					"trigger level · Click the arrow: the edge (rising, falling, either)");
		/* dragged from where it was taken (its lower part): the level follows the mouse's move, no jump */
		const QPointF grip(tab.left() + 20, tab.bottom() - 4);
		const double offset = grip.y() - view->triggerLineY();
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, grip.toPoint());
		const QPointF to(grip.x(), view->yOfValue(-0.2) + offset);
		QMouseEvent drag(QEvent::MouseMove, to, view->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(view, &drag);
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to.toPoint());
		(void) view->grab();
		const bool dragged = std::fabs(offset) >= 4 && std::fabs(view->triggerLevel() + 0.2) < 0.02
				&& std::fabs(view->triggerLevelTag().center().y() - view->triggerLineY()) <= 1
				&& std::fabs(level->text().toDouble() - view->triggerLevel()) < 1e-5;
		/* the arrow: a click takes the next edge */
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, view->triggerEdgeButton().center().toPoint());
		(void) view->grab();
		const bool edged = view->triggerEdge() == ChartView::TriggerEdge::Falling && view->triggerTagText().endsWith(QStringLiteral(" ↓"));
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, view->triggerEdgeButton().center().toPoint());
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, view->triggerEdgeButton().center().toPoint());
		const bool rising = view->triggerEdge() == ChartView::TriggerEdge::Rising;
		std::printf("     (dragged by %.1f px to %.6g, the line at %.1f, the tab's middle %.1f, the box \"%s\")\n", offset,
				view->triggerLevel(), view->triggerLineY(), view->triggerLevelTag().center().y(), qPrintable(level->text()));
		std::printf("     (the plot %.0f..%.0f x %.0f..%.0f (without the trigger %.0f..%.0f x %.0f..%.0f); the tab \"%s\" at "
				"%.0f..%.0f x %.0f..%.0f; room %d, out %d, labelled %d, hover %d, dragged %d, edge %d %d)\n", plot.left(),
				plot.right(), plot.top(), plot.bottom(), without.left(), without.right(), without.top(), without.bottom(),
				qPrintable(view->triggerTagText()), tab.left(), tab.right(), tab.top(), tab.bottom(), int(room), int(tabOut),
				int(labelled), int(tabHover), int(dragged), int(edged), int(rising));
		check(room && tabOut && labelled && tabHover && dragged && edged && rising, "chart, Trigger's marks: the level's tab "
				"(\"0.4 A ↑\") in a margin right of the plot, never over the newest samples (the strip above and the margin "
				"there only while the trigger is on); a hand, a highlight and its tooltip over it; dragged from where it was "
				"taken; a click on its arrow takes the next edge");

		/* the level's marker "T▸": left of the plot in a column of its own (the value labels left of it, none under it),
		 * its point at the level's height on the card's layer's edge (2 px left of the plot), where the dashed line
		 * starts; lit with the tab, a hand and its tooltip over it; dragged from where it was taken; with Lanes in its
		 * lane's band, clear of the labels there too */
		const auto markerClear = [view] {
			const QRectF mark = view->triggerLevelMark();
			bool clear = !mark.isEmpty() && !view->valueLabelRects().isEmpty();
			for (const QRectF &label : view->valueLabelRects()) clear = clear && label.right() < mark.left();
			return clear;
		};
		const auto markPicture = [view] {
			const QImage whole = view->grab().toImage();
			const qreal dpr = whole.devicePixelRatio();
			const QRectF r = view->triggerLevelMark();
			return whole.copy(QRectF(r.topLeft() * dpr, r.size() * dpr).toAlignedRect());
		};
		moveTo(QPointF(plot.center().x(), plot.top() + 10));
		const QRectF levelMark = view->triggerLevelMark();
		const QImage markRest = markPicture();
		bool markDrawn = false;
		{
			const QImage shot = view->grab().toImage();
			const qreal r = shot.devicePixelRatio();
			const QColor line = view->lines().value(0).color;
			const auto lineColoured = [&](double x, double y) {
				const QColor p = shot.pixelColor(int(x * r), int(y * r));
				return std::abs(p.red() - line.red()) + std::abs(p.green() - line.green()) + std::abs(p.blue() - line.blue()) < 60;
			};
			/* the point's tip just left of the layer, on the level's row and the device pixel above and below it: at 100 %
			 * (Linux's virtual screen) the anti-aliased 1 px outline of a hollow pointer and the dashed line share those
			 * rows and none of their pixels is the line's colour itself, so a pixel nearer the line's colour than the box's
			 * fill and the ground beside the marker counts, hollow or solid; the rightmost such pixel at the layer's edge */
			const auto distance = [](const QColor &a, const QColor &b) {
				return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
			};
			const QColor fill = shot.pixelColor(int((levelMark.left() + 3) * r), int((levelMark.center().y() + 4) * r));
			const QColor ground = shot.pixelColor(int((levelMark.left() - 3) * r), int(levelMark.center().y() * r));
			int tip = 0, tipRight = -1;
			const int row = int(view->triggerLineY() * r);
			for (int y = row - 1; y <= row + 1; y++)
				for (int x = int((plot.left() - 6) * r); x < int((plot.left() - 2) * r); x++) {
					const QColor p = shot.pixelColor(x, y);
					if (distance(p, line) < std::min(distance(p, fill), distance(p, ground))) {
						tip++;
						tipRight = std::max(tipRight, x);
					}
				}
			markDrawn = tip >= 2 && tipRight >= (plot.left() - 3) * r - 1
					&& lineColoured(levelMark.left() + 0.5, levelMark.center().y()) && !lineColoured(levelMark.left() - 3, levelMark.center().y());
			if (!markDrawn)
				std::printf("     (the marker's tip: %d pixels nearer the line's colour, the rightmost at %d (device px), the "
						"border %d, the ground %d)\n", tip, tipRight, int(lineColoured(levelMark.left() + 0.5, levelMark.center().y())),
						int(!lineColoured(levelMark.left() - 3, levelMark.center().y())));
		}
		const bool markPlaced = !levelMark.isEmpty() && std::fabs(levelMark.right() - (plot.left() - 2)) < 0.01
				&& levelMark.width() == 21 && levelMark.top() <= view->triggerLineY() && levelMark.bottom() >= view->triggerLineY()
				&& std::fabs(levelMark.center().y() - view->triggerLevelTag().center().y()) < 0.01 && markerClear() && markDrawn;
		moveTo(levelMark.center());
		const QString markTip = view->toolTipAt(levelMark.center());
		const bool markHover = view->triggerTabHovered() && view->cursor().shape() == Qt::PointingHandCursor
				&& markPicture() != markRest && markTip.startsWith(QStringLiteral("TAB "))
				&& markTip.endsWith(QStringLiteral("\nDrag: the trigger level · the edge: in the Trigger row"));
		const QPointF markGrip(levelMark.center().x(), levelMark.top() + 4);
		const double markOffset = markGrip.y() - view->triggerLineY();
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, markGrip.toPoint());
		const QPointF markTo(markGrip.x(), view->yOfValue(0.3) + markOffset);
		QMouseEvent markMove(QEvent::MouseMove, markTo, view->mapToGlobal(markTo), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(view, &markMove);
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, markTo.toPoint());
		moveTo(QPointF(plot.center().x(), plot.top() + 10));
		const bool markDragged = std::fabs(markOffset) >= 4 && std::fabs(view->triggerLevel() - 0.3) < 0.02
				&& std::fabs(view->triggerLevelMark().center().y() - view->triggerLevelTag().center().y()) < 0.01
				&& std::fabs(level->text().toDouble() - view->triggerLevel()) < 1e-5;
		view->setLanes(true);
		(void) view->grab();
		const QRectF markLane = view->laneRect(0), inLane = view->triggerLevelMark();
		const bool markInLane = view->laneCount() == 1 && !inLane.isEmpty() && inLane.top() >= markLane.top()
				&& inLane.bottom() <= markLane.bottom() && std::fabs(inLane.right() - (view->lastPlot().left() - 2)) < 0.01
				&& markerClear();
		view->setLanes(false);
		(void) view->grab();
		std::printf("     (the marker %.1f..%.1f x %.1f..%.1f, the plot's left %.0f, the line at %.1f; placed %d, hover %d (\"%s\"), "
				"dragged %d to %.4g, in its lane %d)\n", levelMark.left(), levelMark.right(), levelMark.top(), levelMark.bottom(),
				plot.left(), view->triggerLineY(), int(markPlaced), int(markHover), qPrintable(markTip), int(markDragged),
				view->triggerLevel(), int(markInLane));
		check(markPlaced && markHover && markDragged && markInLane, "chart, Trigger's marks: the level's marker \"T▸\" left "
				"of the plot in a column of its own (no value label under it), its point at the level's height 2 px left of "
				"the plot where the dashed line starts; lit with the tab, a hand and its tooltip (the level in words, then "
				"\"Drag: the trigger level · the edge: in the Trigger row\"); dragged from where it was taken the level follows; "
				"with Lanes in its lane, clear of the labels");

		/* the flag: above the plot, over the crossing (the same x); a hand and its tooltip; dragged, clamped,
		 * double-clicked back to 50 % */
		level->setText(QStringLiteral("0.4")); /* typed: the row and the tab say the same */
		emit level->editingFinished();
		feed(101.5); /* 101.0655 */
		(void) view->grab();
		const QRectF flag = view->triggerPositionMark(), tMark = view->triggerTag();
		{
			double v0, v1;
			bool cursors;
			view->range(v0, v1, cursors);
			std::printf("     (the view %.4f .. %.4f, the window %g, live %d, the crossing at %.4f, %d holds, phase %d, the place "
					"%.2f)\n", v0, v1, view->window(), int(view->live()), view->triggeredAt(), view->triggerHolds(),
					int(view->triggerPhase()), view->triggerPosition());
		}
		/* a T in its box over a triangle whose point touches the plot's top (the card's layer's top, 2 px over it):
		 * the line's colour at the point, the box's border (the line's colour too) above the triangle */
		bool flagShape = false;
		{
			const QImage shot = view->grab().toImage();
			const qreal r = shot.devicePixelRatio();
			const QColor line = view->lines().value(0).color;
			const auto lineColoured = [&](double x, double y) {
				const QColor p = shot.pixelColor(int(x * r), int(y * r));
				return std::abs(p.red() - line.red()) + std::abs(p.green() - line.green()) + std::abs(p.blue() - line.blue()) < 60;
			};
			const double pointX = flag.center().x();
			flagShape = lineColoured(pointX, flag.bottom() - 2) && lineColoured(pointX, flag.bottom() - 4)
					&& lineColoured(pointX, flag.top() + 0.5) && !lineColoured(pointX + 6, flag.bottom() - 1);
		}
		const bool flagAbove = !flag.isEmpty() && std::fabs(flag.bottom() - (plot.top() - 2)) < 0.01
				&& flag.top() >= plot.top() - 26 && flag.width() == 16 && flagShape
				&& !tMark.isEmpty() && std::fabs(flag.center().x() - tMark.center().x()) < 1;
		moveTo(flag.center());
		const bool flagHover = view->triggerMarkHovered() && view->cursor().shape() == Qt::PointingHandCursor
				&& view->toolTipAt(flag.center()).startsWith(QStringLiteral("Trigger point: TAB crossed 0.4 A, rising, at "))
				&& view->toolTipAt(flag.center()).endsWith(QStringLiteral("\nDrag: where the crossing sits in the window · "
					"Double-click: back to 50 %"));
		/* taken off its middle: the place moves by the mouse's move */
		const QPointF flagGrip(flag.center().x() + 6, flag.center().y());
		const auto dragFlag = [view](QPointF from, double dx) {
			QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from.toPoint());
			const QPointF at(from.x() + dx, from.y());
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
			QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, at.toPoint());
			(void) view->grab();
		};
		const double before = view->triggerPosition();
		dragFlag(flagGrip, 0.3 * plot.width());
		const double moved = view->triggerPosition();
		const QRectF flagMoved = view->triggerPositionMark();
		const bool flagDragged = std::fabs(moved - before - 0.3) < 0.01 && position->value() == int(std::lround(moved * 100))
				&& !view->triggerTag().isEmpty() && std::fabs(flagMoved.center().x() - view->triggerTag().center().x()) < 1;
		dragFlag(flagMoved.center(), plot.width());
		const bool clampHigh = view->triggerPosition() == ChartView::TRIGGER_AT_MAX;
		dragFlag(view->triggerPositionMark().center(), -2 * plot.width());
		const bool clampLow = view->triggerPosition() == 0;
		QTest::mouseDClick(view, Qt::LeftButton, Qt::NoModifier, view->triggerPositionMark().center().toPoint());
		(void) view->grab();
		const bool backToHalf = view->triggerPosition() == ChartView::TRIGGER_AT && position->value() == 50;
		/* the old triangle under the plot is gone: no pixel of the line's colour on the time labels' row at the place */
		const QColor color = view->lines().value(0).color;
		const QImage picture = view->grab().toImage();
		const qreal dpr = picture.devicePixelRatio();
		const double placeX = view->lastPlot().left() + view->triggerPosition() * view->lastPlot().width();
		int coloured = 0;
		for (int y = int((view->lastPlot().bottom() + 1) * dpr); y < int((view->lastPlot().bottom() + 11) * dpr); y++)
			for (int x = int((placeX - 7) * dpr); x < int((placeX + 7) * dpr); x++) {
				const QColor p = picture.pixelColor(x, y);
				if (std::abs(p.red() - color.red()) + std::abs(p.green() - color.green()) + std::abs(p.blue() - color.blue()) < 40)
					coloured++;
			}
		std::printf("     (the flag %.0f..%.0f x %.0f..%.0f, the T at %.1f; dragged %.3f -> %.3f; clamped %d %d; back %d; "
				"line-coloured pixels under the plot at the place %d)\n", flag.left(), flag.right(), flag.top(), flag.bottom(),
				tMark.center().x(), before, moved, int(clampHigh), int(clampLow), int(backToHalf), coloured);
		check(flagAbove && flagHover && flagDragged && clampHigh && clampLow && backToHalf && coloured == 0, "chart, "
				"Trigger's marks: the position's flag (a T over a ▼) in a strip above the plot, the ▼'s point on the plot's top edge right over the crossing; a hand "
				"and its tooltip (the crossing in words, then what it does) over it; dragged from where it was taken (the box follows), clamped to 90 % and 0 %, a "
				"double-click puts it back to 50 %; no triangle under the time axis any more");

		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* for a look: the tab and the flag, in both themes */
			const bool wasDark = Theme::isDark();
			view->setTriggerPosition(0.5);
			moveTo(QPointF(view->lastPlot().center().x(), view->lastPlot().top() + 10)); /* every mark at rest */
			for (const bool lanes : { false, true }) {
				view->setLanes(lanes);
				for (const bool dark : { true, false }) {
					Theme::apply(*qApp, dark);
					chart.tab.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_trigger_marks%1_%2.png")
							.arg(lanes ? QStringLiteral("_lanes") : QString(), dark ? QStringLiteral("dark") : QStringLiteral("light")));
				}
			}
			view->setLanes(false);
			Theme::apply(*qApp, wasDark);
		}
		action->setChecked(false);
		(void) view->grab();
		const bool roomGone = view->lastPlot().top() == without.top() && view->lastPlot().right() == without.right()
				&& view->triggerLevelTag().isEmpty()
				&& view->triggerPositionMark().isEmpty();
		check(roomGone, "chart, Trigger's marks: with the trigger off the plot takes the strip and the margin back");
		chart.tab.hide();
		clearTriggerSettings();
	}

	/* Trigger v2, the owner: "the Normal / triggered text is dancing". A 50 Hz sine re-triggering Normal at 10, 20 and
	 * 100 ms windows: across 100 frames each, after the first capture, the row and the corner say "Normal · triggered"
	 * and nothing else, their places unchanged (no "capturing after T" coming and going, no rate, no flip to waiting
	 * between crossings); the signal gone from the level, "Normal · waiting, last at ..." about a second after the last
	 * crossing, and it stays */
	void chartTriggerSteadyState() {
		clearTriggerSettings();
		LoneChart chart(QStringLiteral("STEADY"), QStringLiteral("V"));
		ChartView *view = chart.view;
		view->setWindow(1);
		view->setSmooth(false);
		qint64 index = 0;
		bool away = false; /* the signal below the level from then on */
		const auto sampleTime = [](qint64 i) { return 99.0 + double(i) / 20000; };
		const auto frame = [&] { /* a 60th of a second of a 50 Hz sine at 20 kHz */
			MathLines::Samples samples;
			const qint64 end = index + 333;
			for (; index < end; index++) {
				const double t = sampleTime(index);
				samples[regKey(chart.def)] << QPointF(t, away ? -0.5 : std::sin(2 * M_PI * 50 * t));
			}
			chart.now = sampleTime(index - 1);
			chart.tab.frame(samples);
			(void) view->grab();
			chart.tab.refreshStatus(); /* the row's state, as the tab's timer writes it */
		};
		for (int k = 0; k < 20; k++) frame();
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		auto *action = chart.tab.findChild<QAction *>(QStringLiteral("chartTrigger"));
		auto *level = chart.tab.findChild<QLineEdit *>(QStringLiteral("triggerLevel"));
		auto *mode = chart.tab.findChild<QComboBox *>(QStringLiteral("triggerMode"));
		auto *state = chart.tab.findChild<ElidedLabel *>(QStringLiteral("triggerState"));
		if (!action || !level || !mode || !state) {
			check(false, "chart, Trigger's state steady: the row's controls found");
			return;
		}
		action->setChecked(true);
		mode->setCurrentIndex(mode->findData(int(ChartView::TriggerMode::Normal)));
		emit mode->activated(mode->currentIndex());
		level->setText(QStringLiteral("0.3"));
		emit level->editingFinished();
		bool steady = true;
		QStringList seen;
		for (const double window : { 0.01, 0.02, 0.1 }) {
			view->setWindow(window);
			const int holds = view->triggerHolds();
			for (int k = 0; k < 60 && (view->triggerHolds() <= holds + 1 || view->triggerPhase() != ChartView::TriggerPhase::Triggered); k++)
				frame();
			for (int k = 0; k < 5; k++) frame();
			QApplication::processEvents();
			const QString row = state->fullText(), corner = view->stateFullText();
			const QRect rowAt = state->geometry();
			const QRectF cornerAt = view->stateRect();
			const int start = view->triggerHolds();
			int changes = 0;
			for (int k = 0; k < 100; k++) {
				frame();
				QApplication::processEvents();
				if (state->fullText() != row || state->geometry() != rowAt
						|| view->stateFullText() != corner || view->stateRect() != cornerAt)
					changes++;
			}
			const int retriggers = view->triggerHolds() - start;
			seen << QStringLiteral("%1 ms: %2 re-triggers, \"%3\" / \"%4\", %5 changes").arg(window * 1000).arg(retriggers)
					.arg(row, corner).arg(changes);
			steady = steady && retriggers >= 10 && changes == 0 && row == QStringLiteral("Normal · triggered")
					&& corner == QStringLiteral("Normal · triggered") && !cornerAt.isEmpty();
		}
		/* the signal away from the level: "triggered" for a second after the last crossing, then waiting, and it stays */
		const double lastCrossing = view->triggeredAt();
		away = true;
		double changedAt = NAN;
		int after = 0;
		bool stays = true;
		for (int k = 0; k < 150; k++) {
			frame();
			QApplication::processEvents();
			const bool waiting = view->stateFullText() == QStringLiteral("Normal · waiting")
					&& state->fullText().startsWith(QStringLiteral("Normal · waiting, last at "));
			if (std::isnan(changedAt)) {
				if (waiting) changedAt = sampleTime(index - 1);
				else stays = stays && view->stateFullText() == QStringLiteral("Normal · triggered");
			} else {
				after++;
				stays = stays && waiting;
			}
		}
		const double waitedFor = changedAt - lastCrossing;
		std::printf("     (%s; the signal away: waiting %.3f s after the last crossing, %d frames after)\n",
				qPrintable(seen.join(QStringLiteral("; "))), waitedFor, after);
		check(steady, "chart, Trigger: a 50 Hz sine re-triggering Normal at 10, 20 and 100 ms windows: across 100 frames "
				"each the row and the corner say \"Normal · triggered\", their text and place unchanged");
		check(stays && waitedFor >= 0.95 && waitedFor <= 1.2 && after >= 60, "chart, Trigger: the crossings gone, \"Normal "
				"· triggered\" stays for a second after the last one, then \"Normal · waiting\" (the row: \"last at\" its "
				"time), and it stays");
		action->setChecked(false);
		chart.tab.hide();
		clearTriggerSettings();
	}

	/* U-15, the time grid as a scope's: below a 1 s window ten divisions whose lines stand still while the wave moves,
	 * labelled by their offset from the right edge ("-8 ms" ... "0"), or from T while a trigger holds the view ("0"
	 * under the crossing, "+4 ms"); a "1 ms/div" readout with the clock time at 0 at the axis's right end; the wheel
	 * steps 1-2-5 windows; Display's Time grid (Auto, Clock times, Divisions; chart/timeGrid); clock times from 1 s as
	 * before; in Arabic each offset and the readout one left-to-right piece, the unit beside its number */
	void chartTimeGrid() {
		clearTriggerSettings();
		QSettings().remove(QStringLiteral("chart/timeGrid"));
		QSettings().setValue(QStringLiteral("chart/autoShortWindows"), false); /* the plain live view: the wave runs */
		LoneChart chart(QStringLiteral("GRID"), QStringLiteral("V"));
		ChartView *view = chart.view;
		view->setWindow(1);
		view->setSmooth(false);
		qint64 index = 0;
		const auto sampleTime = [](qint64 i) { return 99.0 + double(i) / 20000; };
		const auto feedTo = [&](ChartTab &tab, double &now, const RegDef &def, qint64 end) { /* a 70 Hz sine at 20 kHz */
			MathLines::Samples samples;
			for (; index < end; index++) {
				const double t = sampleTime(index);
				samples[regKey(def)] << QPointF(t, std::sin(2 * M_PI * 70 * t));
			}
			now = sampleTime(index - 1);
			tab.frame(samples);
		};
		const auto windowBoxSays = [&chart](const QString &text) {
			for (QComboBox *box : chart.tab.findChildren<QComboBox *>())
				if (box->toolTip().startsWith(QLatin1String("View: the time shown"))) box->setEditText(text);
		};
		const auto frame = [&] { /* a 60th of a second */
			feedTo(chart.tab, chart.now, chart.def, index + 333);
			(void) view->grab();
		};
		for (int k = 0; k < 20; k++) frame();
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		auto *autoGrid = chart.tab.findChild<QAction *>(QStringLiteral("chartTimeGridAuto"));
		auto *clockGrid = chart.tab.findChild<QAction *>(QStringLiteral("chartTimeGridClock"));
		auto *divisionsGrid = chart.tab.findChild<QAction *>(QStringLiteral("chartTimeGridDivisions"));
		if (!autoGrid || !clockGrid || !divisionsGrid) {
			check(false, "chart, time grid: Display's three choices found (Auto, Clock times, Divisions)");
			return;
		}
		const QRegularExpression clockForm(QStringLiteral("^\\d\\d:\\d\\d:\\d\\d(\\.\\d+)?$"));
		const auto clockLabels = [&clockForm](const QStringList &labels) {
			bool all = labels.size() >= 3;
			for (const QString &label : labels) all = all && clockForm.match(label).hasMatch();
			return all;
		};
		frame();
		const bool clockAtOne = autoGrid->isChecked() && !view->divisionsShown() && clockLabels(view->timeLabels())
				&& view->divisionReadout().isEmpty() && view->divisionReadoutRect().isEmpty();
		std::printf("     (at 1 s: %s)\n", qPrintable(view->timeLabels().join(QStringLiteral(", "))));
		check(clockAtOne, "chart, time grid: at a 1 s window (Auto) the clock-time labels as before, no division readout");

		/* 10 ms live: 30 frames, the lines inside the plot at the same places (each tenth of it) while the view's end
		 * moves with the data */
		view->setWindow(0.01);
		frame();
		const QRectF plot = view->lastPlot();
		const QVector<double> lines = view->timeGridX();
		double t0, firstEnd, lastEnd;
		view->viewSpan(t0, firstEnd);
		bool still = lines.size() == 9;
		for (int k = 0; still && k < lines.size(); k++)
			still = std::fabs(lines[k] - (plot.left() + (k + 1) * plot.width() / 10)) < 0.01;
		QSet<QString> readouts;
		QElapsedTimer age;
		age.start();
		int moves = 0;
		for (int k = 0; k < 30; k++) {
			frame();
			if (view->timeGridX() != lines) moves++;
			readouts << view->divisionReadout();
		}
		const qint64 ms = age.elapsed();
		view->viewSpan(t0, lastEnd);
		std::printf("     (10 ms, 30 frames: the view's end moved %.3f s, the lines moved in %d frames, %d readouts in %lld ms)\n",
				lastEnd - firstEnd, moves, int(readouts.size()), (long long) ms);
		check(still && moves == 0 && lastEnd - firstEnd > 0.4, "chart, time grid: at a 10 ms live window, 30 frames: the 9 "
				"lines inside the plot stand still at its tenths while the view's end moves with the data");
		const QStringList labels = view->timeLabels();
		const QVector<double> labelX = view->timeLabelX();
		const int zero = int(labels.indexOf(QStringLiteral("0")));
		const bool offsets = labels.contains(QStringLiteral("-8 ms")) && labels.contains(QStringLiteral("-5 ms"))
				&& labels.contains(QStringLiteral("-10 ms")) && zero >= 0 && std::fabs(labelX[zero] - plot.right()) < 0.5
				&& !labels.join(QString()).contains(QLatin1Char('+'));
		const QString readout = view->divisionReadout();
		const QRectF readoutRect = view->divisionReadoutRect();
		const QRegularExpression readoutForm(QStringLiteral("^1 ms/div · \\d\\d:\\d\\d:\\d\\d\\.\\d{3}$"));
		/* the readout in the state corner's row above the plot (alone there live: at its right end), so every one of
		 * the 11 labels is drawn (the owner: it hid "-2 ms"); never over the legend */
		const QRectF legendRow = view->legendViewport();
		const bool everyLabel = labels.size() == 11;
		const bool readoutOk = readoutForm.match(readout).hasMatch() && readoutRect.bottom() < plot.top()
				&& readoutRect.top() >= 0 && view->stateRect().isEmpty() && std::fabs(readoutRect.right() - plot.right()) <= 1
				&& !readoutRect.intersects(legendRow) && readouts.size() <= 2 + ms / 500
				&& view->toolTipAt(readoutRect.center()).contains(QStringLiteral("the clock time at 0, the right edge"));
		std::printf("     (10 ms: %d labels %s; the readout \"%s\" at %.0f..%.0f x %.0f..%.0f, the legend to %.0f)\n",
				int(labels.size()), qPrintable(labels.join(QStringLiteral(", "))), qPrintable(readout), readoutRect.left(),
				readoutRect.right(), readoutRect.top(), readoutRect.bottom(), legendRow.right());
		check(offsets, "chart, time grid: at 10 ms live the labels are offsets from the right edge (-10 ms, -8 ms, -5 ms "
				"... 0, the 0 at the right edge)");
		check(readoutOk && everyLabel, "chart, time grid: the readout \"1 ms/div · 14:03:12.345\" (the division and the "
				"clock time at 0) in the state corner's row above the plot, at its right end and clear of the legend; all 11 "
				"time labels drawn; its clock time written at most twice a second, its tooltip saying what it is");
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) {
			const bool wasDark = Theme::isDark();
			windowBoxSays(QStringLiteral("10 ms")); /* the picture's Window box as the view (set here by the view itself) */
			for (const bool dark : { false, true }) {
				Theme::apply(*qApp, dark);
				frame();
				chart.tab.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_timegrid_live_%1.png")
						.arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
			}
			Theme::apply(*qApp, wasDark);
		}

		/* the wheel: the next window of 1, 2 or 5 per division; Auto leaves the divisions at 1 s */
		const auto wheel = [view, plot](int notches) {
			const QPointF inside = plot.center();
			QWheelEvent e(inside, view->mapToGlobal(inside), QPoint(), QPoint(0, 120 * notches), Qt::NoButton, Qt::NoModifier,
					Qt::NoScrollPhase, false);
			QApplication::sendEvent(view, &e);
			return view->window();
		};
		const double in = wheel(1), out = wheel(-1), out2 = wheel(-1);
		frame();
		const QString fiveHundred = view->divisionReadout();
		view->setWindow(0.5);
		const double up = wheel(-1), beyond = wheel(-1), back = wheel(1), down = wheel(1);
		std::printf("     (the wheel from 10 ms: %g, %g, %g; from 0.5 s: %g, %g, %g, %g; at 20 ms \"%s\")\n", in, out, out2,
				up, beyond, back, down, qPrintable(fiveHundred));
		check(std::fabs(in - 0.005) < 1e-12 && std::fabs(out - 0.01) < 1e-12 && std::fabs(out2 - 0.02) < 1e-12
						&& fiveHundred.startsWith(QStringLiteral("2 ms/div · ")) && std::fabs(up - 1) < 1e-12
						&& std::fabs(beyond - 1.25) < 1e-12 && std::fabs(back - 1) < 1e-12 && std::fabs(down - 0.5) < 1e-12,
				"chart, time grid: below 1 s a wheel notch is the next window of 1, 2 or 5 per division (10 ms -> 5 ms, "
				"10 ms, 20 ms: \"2 ms/div\"); Auto leaves them at 1 s, where the wheel zooms as before (1.25 s)");

		/* held on a trigger's crossing: the labels count from T, its 0 under the crossing; the readout's clock time is T's */
		view->setWindow(0.01);
		view->setTrigger(chart.key(), 0.0, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Normal);
		for (int k = 0; k < 10; k++) frame();
		double h0, h1;
		view->viewSpan(h0, h1);
		const double crossing = view->triggeredAt();
		const double crossX = plot.left() + (crossing - h0) / (h1 - h0) * plot.width();
		const QStringList held = view->timeLabels();
		const QVector<double> heldX = view->timeLabelX();
		const int heldZero = int(held.indexOf(QStringLiteral("0")));
		const QString heldReadout = view->divisionReadout();
		const QString atT = QDateTime::fromMSecsSinceEpoch(view->epochMs() + qint64(std::llround(crossing * 1000)))
									.toString(QStringLiteral("HH:mm:ss.zzz"));
		const bool fromT = !view->live() && std::isfinite(crossing) && heldZero >= 0 && std::fabs(heldX[heldZero] - crossX) <= 1
				&& held.contains(QStringLiteral("+4 ms")) && held.contains(QStringLiteral("-2 ms"))
				&& view->timeGridX().size() == 9 && heldReadout == QStringLiteral("1 ms/div · ") + atT
				&& view->toolTipAt(view->divisionReadoutRect().center()).contains(QStringLiteral("(T)"));
		/* with the trigger's state in the corner the readout sits left of it, neither over the other nor over the legend */
		const QRectF heldRect = view->divisionReadoutRect(), heldState = view->stateRect();
		const bool beside = !heldState.isEmpty() && heldRect.right() <= heldState.left()
				&& heldRect.bottom() < plot.top() && !heldRect.intersects(view->legendViewport());
		std::printf("     (held on T at %.1f px: %d labels %s; the readout \"%s\" at %.0f..%.0f, the state from %.0f, the legend "
				"to %.0f; T at %s)\n", crossX, int(held.size()), qPrintable(held.join(QStringLiteral(", "))),
				qPrintable(heldReadout), heldRect.left(), heldRect.right(), heldState.left(), view->legendViewport().right(),
				qPrintable(atT));
		check(fromT && beside, "chart, time grid: held on a trigger's crossing the labels count from T (\"0\" under the "
				"crossing within a pixel, -2 ms, +4 ms), the readout's clock time is T's and its tooltip says so; "
				"the readout left of the trigger's state, over neither it nor the legend");
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) {
			const bool wasDark = Theme::isDark();
			windowBoxSays(QStringLiteral("10 ms")); /* the picture's Window box as the view (set here by the view itself) */
			for (const bool dark : { false, true }) {
				Theme::apply(*qApp, dark);
				(void) view->grab();
				chart.tab.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_timegrid_trigger_%1.png")
						.arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
			}
			Theme::apply(*qApp, wasDark);
		}
		view->stopTrigger();
		view->setLive(true);

		/* the setting's three choices, saved: Clock times at 10 ms, Divisions at 10 s, Auto at 10 s */
		clockGrid->trigger();
		frame();
		const bool clockChoice = view->timeGrid() == ChartView::TimeGrid::Clock && clockLabels(view->timeLabels())
				&& view->divisionReadout().isEmpty() && QSettings().value(QStringLiteral("chart/timeGrid")).toInt() == 1;
		divisionsGrid->trigger();
		view->setWindow(10);
		frame();
		const QStringList longLabels = view->timeLabels();
		const bool divisionsChoice = view->divisionsShown() && view->divisionReadout().startsWith(QStringLiteral("1 s/div · "))
				&& longLabels.contains(QStringLiteral("-5 s")) && longLabels.contains(QStringLiteral("0"))
				&& QSettings().value(QStringLiteral("chart/timeGrid")).toInt() == 2;
		bool restored = false;
		{
			LoneChart other(QStringLiteral("GRID2"), QStringLiteral("V"));
			restored = other.view->timeGrid() == ChartView::TimeGrid::Divisions;
		}
		autoGrid->trigger();
		frame();
		const bool autoChoice = !view->divisionsShown() && clockLabels(view->timeLabels()) && view->divisionReadout().isEmpty()
				&& QSettings().value(QStringLiteral("chart/timeGrid")).toInt() == 0;
		std::printf("     (Clock times at 10 ms %d, Divisions at 10 s %d (%s), kept by a new tab %d, Auto at 10 s %d)\n",
				int(clockChoice), int(divisionsChoice), qPrintable(longLabels.join(QStringLiteral(", "))), int(restored),
				int(autoChoice));
		check(clockChoice && divisionsChoice && restored && autoChoice, "chart, time grid: Display's Time grid: Clock times "
				"keeps clock labels at 10 ms, Divisions puts them at 10 s (\"1 s/div\", -5 s ... 0), Auto as by default; "
				"saved (chart/timeGrid) and taken by a new tab");
		chart.tab.hide();

		/* Arabic: each offset and the readout one left-to-right piece, the unit beside its number */
		language::apply(*qApp, QStringLiteral("ar"));
		bool arabic = false;
		QString arabicLabels;
		{
			LoneChart other(QStringLiteral("GRID3"), QStringLiteral("V"));
			other.tab.show();
			(void) QTest::qWaitForWindowExposed(&other.tab);
			other.view->setSmooth(false);
			other.view->setWindow(0.01);
			for (int k = 0; k < 5; k++) {
				feedTo(other.tab, other.now, other.def, index + 333);
				(void) other.view->grab();
			}
			const QStringList texts = other.view->timeLabels();
			const QString isolated = other.view->divisionReadout();
			arabic = texts.size() >= 3 && isolated.startsWith(QChar(0x2066)) && isolated.endsWith(QChar(0x2069))
					&& isolated.contains(QStringLiteral("1 ms/div"));
			for (const QString &text : texts)
				arabic = arabic && (text == QStringLiteral("0") || (text.startsWith(QChar(0x2066)) && text.endsWith(QChar(0x2069))
						&& text.contains(QStringLiteral(" ms"))));
			arabicLabels = texts.join(QStringLiteral(", "));
		}
		language::apply(*qApp, QStringLiteral("en"));
		std::printf("     (Arabic: %s)\n", qPrintable(arabicLabels));
		check(arabic, "chart, time grid: in Arabic each offset (\"-8 ms\") and the readout are one left-to-right piece "
				"(isolates), the unit beside its number");
		QSettings().remove(QStringLiteral("chart/timeGrid"));
		QSettings().remove(QStringLiteral("chart/autoShortWindows"));
		clearTriggerSettings();
	}

	/* U-7, times from T: while the view is held on a trigger's crossing, the hover box says "T +1.000 ms" beside the
	 * clock time, a cursor's tag says how far it is from T in its tooltip and the measure line "A: T -0.250 ms · B: T
	 * +1.750 ms" (B - A as before), as a scope's cursors read from the trigger point; live, clock times as before */
	void chartTimesFromT() {
		clearTriggerSettings();
		QSettings().setValue(QStringLiteral("chart/autoShortWindows"), false);
		QSettings().remove(QStringLiteral("chart/measure"));
		LoneChart chart(QStringLiteral("FROMT"), QStringLiteral("V"));
		ChartView *view = chart.view;
		view->setWindow(1);
		view->setSmooth(false);
		qint64 index = 0;
		const auto frame = [&] { /* a 60th of a second of a 70 Hz sine at 20 kHz */
			MathLines::Samples samples;
			for (const qint64 end = index + 333; index < end; index++) {
				const double t = 99.0 + double(index) / 20000;
				samples[regKey(chart.def)] << QPointF(t, std::sin(2 * M_PI * 70 * t));
			}
			chart.now = 99.0 + double(index - 1) / 20000;
			chart.tab.frame(samples);
			(void) view->grab();
		};
		for (int k = 0; k < 20; k++) frame();
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		auto *measure = chart.tab.findChild<QPushButton *>(QStringLiteral("measure"));
		auto *info = chart.tab.findChild<QLabel *>(QStringLiteral("measureInfo"));
		if (!measure || !info) {
			check(false, "chart, times from T: the Measure button and its line found");
			return;
		}
		view->setWindow(0.01);
		view->setTrigger(chart.key(), 0.0, ChartView::TriggerEdge::Rising, ChartView::TriggerMode::Normal);
		for (int k = 0; k < 60 && !(std::isfinite(view->triggeredAt()) && !view->live()); k++) frame();
		std::printf("     (the trigger: on %d, live %d, crossing %.4f, phase %d)\n", int(view->triggerOn()), int(view->live()),
				view->triggeredAt(), int(view->triggerPhase()));
		measure->setChecked(true);
		(void) view->grab();
		QRectF plot = view->lastPlot();
		double h0, h1;
		view->viewSpan(h0, h1);
		const double crossing = view->triggeredAt();
		const auto xOf = [&](double t) { return plot.left() + (t - h0) / (h1 - h0) * plot.width(); };
		const auto clockOf = [view](double t) {
			return QDateTime::fromMSecsSinceEpoch(view->epochMs() + qint64(std::llround(t * 1000))).toString(QStringLiteral("HH:mm:ss.zzz"));
		};
		/* the hover box a division after T */
		const auto hover = [view](QPointF at) {
			QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(view, &move);
			(void) view->grab();
			return view->readoutTimeText();
		};
		const double hoverX = std::round(xOf(crossing) + plot.width() / 10);
		const QString heldBox = hover(QPointF(hoverX, plot.center().y()));
		const QRegularExpression fromTForm(QStringLiteral("^\\d\\d:\\d\\d:\\d\\d\\.\\d{3}   T ([+-]\\d+\\.\\d{3}) ms$"));
		const QRegularExpressionMatch boxMatch = fromTForm.match(heldBox);
		const double expectedMs = (hoverX - xOf(crossing)) / plot.width() * (h1 - h0) * 1000;
		const bool boxFromT = std::isfinite(crossing) && std::isfinite(view->timeOrigin())
				&& view->timeOrigin() == crossing && boxMatch.hasMatch()
				&& std::fabs(boxMatch.captured(1).toDouble() - expectedMs) < 0.01;
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) {
			const bool wasDark = Theme::isDark();
			for (const bool dark : { false, true }) {
				Theme::apply(*qApp, dark);
				(void) hover(QPointF(hoverX + (dark ? 1 : 0), plot.center().y()));
				chart.tab.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_fromT_hover_%1.png")
						.arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
			}
			Theme::apply(*qApp, wasDark);
		}
		std::printf("     (held on T: the box's time \"%s\", %.3f ms expected)\n", qPrintable(heldBox), expectedMs);
		check(boxFromT, "chart, times from T: held on a trigger's crossing, the hover box says how far from T the mouse is "
				"(\"T +1.000 ms\") beside its clock time");

		/* the cursors 0.25 ms before T and 1.75 ms after: their tags' tooltips and the measure line from T */
		{ /* the mouse off the chart: the hover box gone from the cursors' picture */
			QEvent leave(QEvent::Leave);
			QApplication::sendEvent(view, &leave);
		}
		const double a = crossing - 0.00025, b = crossing + 0.00175;
		view->setCursors(a, b);
		(void) view->grab();
		const QString tipA = view->toolTipAt(QPointF(xOf(a), plot.top() + 6));
		const QString tipB = view->toolTipAt(QPointF(xOf(b), plot.top() + 6));
		const bool waited = QTest::qWaitFor([info] {
			return info->text().contains(QStringLiteral(" · A: T -0.250 ms · B: T +1.750 ms"));
		}, 3000);
		const QString heldInfo = info->text();
		const bool cursorsFromT = tipA == QStringLiteral("Cursor A at %1 · T -0.250 ms").arg(clockOf(a))
				&& tipB == QStringLiteral("Cursor B at %1 · T +1.750 ms").arg(clockOf(b)) && waited
				&& heldInfo.startsWith(QStringLiteral("Measured between the cursors: A → B = "));
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) {
			const bool wasDark = Theme::isDark();
			for (const bool dark : { false, true }) {
				Theme::apply(*qApp, dark);
				(void) view->grab();
				chart.tab.grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_fromT_cursors_%1.png")
						.arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
			}
			Theme::apply(*qApp, wasDark);
		}
		std::printf("     (the tags: \"%s\" | \"%s\"; the measure line \"%s\")\n", qPrintable(tipA), qPrintable(tipB),
				qPrintable(heldInfo));
		check(cursorsFromT, "chart, times from T: held on a crossing, a cursor's tag says its clock time and how far from T "
				"(\"Cursor A at ... · T -0.250 ms\"), the measure line \"A: T -0.250 ms · B: T +1.750 ms\" after A → B");

		/* live, the trigger off: clock times as before */
		view->stopTrigger();
		view->setLive(true);
		for (int k = 0; k < 3; k++) frame();
		plot = view->lastPlot(); /* the trigger's margins gone */
		view->viewSpan(h0, h1);
		const double la = h1 - 0.005, lb = h1 - 0.002;
		view->setCursors(la, lb);
		(void) view->grab();
		const QString liveTip = view->toolTipAt(QPointF(xOf(la), plot.top() + 6));
		const QString liveBox = hover(QPointF(plot.center().x(), plot.center().y()));
		(void) QTest::qWaitFor([info] { return !info->text().contains(QStringLiteral("A: T")); }, 3000);
		const bool liveClock = !std::isfinite(view->timeOrigin()) && liveTip.startsWith(QStringLiteral("Cursor A at "))
				&& !liveTip.contains(QStringLiteral("T ")) && liveBox.endsWith(QStringLiteral(" s"))
				&& liveBox.contains(QStringLiteral("   -")) && !info->text().contains(QStringLiteral("A: T"));
		std::printf("     (live: the tag \"%s\", the box \"%s\", the line \"%s\")\n", qPrintable(liveTip), qPrintable(liveBox),
				qPrintable(info->text()));
		check(liveClock, "chart, times from T: live with the trigger off the box says how long ago, the tags their clock "
				"time alone, the measure line no T");
		view->setCursors(NAN, NAN);
		measure->setChecked(false);
		measured(view);
		chart.tab.hide();
		QSettings().remove(QStringLiteral("chart/autoShortWindows"));
		QSettings().remove(QStringLiteral("chart/measure"));
		clearTriggerSettings();
	}

	/* O-14: the short window's lock watches the busiest line, not the first. A polled line of 10 polls a second plotted
	 * first and a fast line of 50 000 records a second (a 50 Hz sine, its crossings found as the engine finds them)
	 * second, at a 20 ms window: the lock watches the fast line and says "Auto (short window)" at each of 100 frames
	 * (on the polled line it flipped between free running and locked); the polled line alone has under
	 * SHORT_LOCK_SAMPLES in the window: no lock, nothing in the corner */
	void chartShortLockBusiest() {
		clearTriggerSettings();
		QSettings().remove(QStringLiteral("chart/autoShortWindows"));
		StreamDef def;
		def.name = QStringLiteral("ADC");
		StreamChannel channel;
		channel.name = QStringLiteral("I");
		channel.type = RegType::I16;
		def.channels << channel;
		constexpr double RATE = 50000;
		const auto runPair = [&](bool withFast, int frames, int &lockedFrames, int &watchedFast, QString &corner) {
			LoneChart chart(QStringLiteral("SLOW"), QStringLiteral("V"));
			ChartView *view = chart.view;
			view->setSmooth(false);
			const int fastKey = ChartView::fastKey(0, 0);
			if (withFast) {
				view->setFastStream(0, def);
				view->addSeries(fastKey, QStringLiteral("ADC.I"), QStringLiteral("A"), Qt::red);
			}
			view->setWindow(0.02);
			fast::TriggerScan scan;
			qint64 record = 0, polls = 0;
			double now = 99;
			lockedFrames = watchedFast = 0;
			for (int k = 0; k < frames; k++) {
				now += 1.0 / 60;
				MathLines::Samples samples; /* the polled line: a slow sine, 10 polls a second */
				for (; 99.0 + polls * 0.1 <= now; polls++) {
					const double t = 99.0 + polls * 0.1;
					samples[regKey(chart.def)] << QPointF(t, 5 + std::sin(2 * M_PI * 0.3 * t));
				}
				if (withFast) { /* the fast line's records up to now, and the engine's part: its crossings */
					const qint64 end = qint64((now - 99.0) * RATE);
					const qint64 n = end - record;
					QByteArray records(int(n * 2), '\0');
					for (qint64 i = 0; i < n; i++) {
						const qint16 v = qint16(std::lround(1000 * std::sin(2 * M_PI * 50 * (record + i) / RATE)));
						records[int(2 * i)] = char(v);
						records[int(2 * i + 1)] = char(v >> 8);
					}
					const qint64 at = view->appendFast(0, quint64(record), int(n), records, record == 0, 0);
					const double markTime = 99.0 + double(end) / RATE;
					view->markFast(0, quint64(end), markTime, 1 / RATE);
					int stream = -1;
					const fast::TriggerWatch watch = view->fastTriggerWatch(stream);
					if (watch.serial != scan.watch().serial) scan.set(watch);
					fast::BlockTaken taken;
					taken.first = quint64(record);
					taken.count = int(n);
					taken.newStart = record == 0;
					QVector<fast::Crossing> crossings;
					scan.scan(def, taken, records.constData(), { quint64(end), markTime }, 1 / RATE, crossings);
					view->fastCrossings(0, at, crossings);
					record = end;
				}
				chart.now = now;
				chart.tab.frame(samples);
				(void) view->grab();
				if (k >= 20) { /* the first frames fill the window */
					if (view->stateFullText() == QStringLiteral("Auto (short window)")) lockedFrames++;
					if (view->shortLockKey() == fastKey) watchedFast++;
				}
			}
			corner = view->stateFullText();
		};
		int locked = 0, watched = 0, aloneLocked = 0, aloneWatched = 0;
		QString corner, aloneCorner;
		runPair(true, 120, locked, watched, corner);
		runPair(false, 60, aloneLocked, aloneWatched, aloneCorner);
		std::printf("     (a polled line first, a fast one second, 20 ms: %d of 100 frames locked, %d on the fast line, the "
				"corner \"%s\"; the polled line alone: %d locked, the corner \"%s\")\n", locked, watched, qPrintable(corner),
				aloneLocked, qPrintable(aloneCorner));
		check(locked == 100 && watched == 100 && aloneLocked == 0 && aloneCorner.isEmpty(),
				"chart, short windows lock on the busiest line: a polled line plotted first and a fast line second at a "
				"20 ms window: the lock watches the fast line, \"Auto (short window)\" over 100 frames; a polled line of 10 "
				"polls a second alone (under 20 samples in the window): no lock, nothing in the corner");
	}

	/* U-17: below 100 ms a live view with the trigger off locks on its first line by itself (Auto, the line's middle,
	 * rising): a 50 Hz sine in a 20 ms window stands still (the view's end moves by whole periods), the corner says
	 * "Auto (short window)", the row stays hidden and the button says Hold; "Auto · free running" while it does not
	 * cross; off with Display's "Lock short windows" (saved), at 100 ms and with Hold; the user's trigger takes over */
	void chartShortLock() {
		clearTriggerSettings();
		QSettings().remove(QStringLiteral("chart/autoShortWindows"));
		LoneChart chart(QStringLiteral("LOCK"), QStringLiteral("V"));
		ChartView *view = chart.view;
		view->setWindow(1);
		view->setSmooth(false);
		qint64 index = 0;
		bool flat = false;
		const auto sampleTime = [](qint64 i) { return 99.0 + double(i) / 20000; };
		const auto frame = [&] { /* a 60th of a second of a 50 Hz sine (its middle 0.2) at 20 kHz */
			MathLines::Samples samples;
			const qint64 end = index + 333;
			for (; index < end; index++) {
				const double t = sampleTime(index);
				samples[regKey(chart.def)] << QPointF(t, flat ? 0.2 : 0.2 + std::sin(2 * M_PI * 50 * t));
			}
			chart.now = sampleTime(index - 1);
			chart.tab.frame(samples);
			(void) view->grab();
		};
		for (int k = 0; k < 20; k++) frame();
		chart.tab.show();
		(void) QTest::qWaitForWindowExposed(&chart.tab);
		auto *lock = chart.tab.findChild<QAction *>(QStringLiteral("chartShortLock"));
		auto *action = chart.tab.findChild<QAction *>(QStringLiteral("chartTrigger"));
		auto *row = chart.tab.findChild<QWidget *>(QStringLiteral("triggerRow"));
		auto *hold = chart.tab.findChild<QPushButton *>(QStringLiteral("hold"));
		if (!lock || !action || !row || !hold) {
			check(false, "chart, short windows lock: Display's entry, the trigger's row and the toolbar's button found");
			return;
		}
		const bool notAtOneSecond = !view->shortLocked() && view->live();
		view->setWindow(0.02);
		for (int k = 0; k < 10; k++) frame();
		QApplication::processEvents();
		const bool locked = lock->isChecked() && view->shortLocked() && !view->triggerOn() && view->live()
				&& !row->isVisible() && hold->text() == QStringLiteral("Hold") && view->stateFullText() == QStringLiteral("Auto (short window)")
				&& std::fabs(view->triggerLevel() - 0.2) < 0.05 && view->triggerLevelTag().isEmpty()
				&& view->triggerPositionMark().isEmpty() && view->triggerTag().isEmpty();
		/* the lock's words as a badge: the accent colour (not the warn amber of Stopped) on a tint of it, a tooltip */
		const QString badgeTip = QStringLiteral("The view locks on the busiest line's crossings at windows under 100 ms · "
				"Display → Lock short windows turns it off");
		const auto badgeSeen = [&](QString &why) {
			const QImage picture = view->grab().toImage();
			const qreal dpr = picture.devicePixelRatio();
			const QRectF badge = view->stateBadgeRect();
			if (badge.isEmpty() || !view->stateRect().adjusted(-0.5, -0.5, 0.5, 0.5).contains(badge)) {
				why = QStringLiteral("no badge");
				return false;
			}
			const QColor accent = Theme::colors().accent;
			const auto distance = [](QColor a, QColor b) {
				return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
			};
			int ink = 0;
			for (int y = int(badge.top() * dpr); y < int(badge.bottom() * dpr); y++)
				for (int x = int(badge.left() * dpr); x < int(badge.right() * dpr); x++)
					if (distance(picture.pixelColor(x, y), accent) < 90) ink++;
			const QColor tint = picture.pixelColor((QPointF(badge.left() + 2, badge.center().y()) * dpr).toPoint());
			const QColor ground = picture.pixelColor((QPointF(badge.left() - 3, badge.center().y()) * dpr).toPoint());
			const QString tip = view->toolTipAt(badge.center());
			why = QStringLiteral("%1 accent pixels, tint %2 ground %3, tooltip \"%4\"").arg(ink).arg(tint.name(), ground.name(), tip);
			return ink > 4 && distance(tint, accent) < distance(ground, accent) && tip == badgeTip;
		};
		QString badgeWhy;
		const bool badgeLocked = badgeSeen(badgeWhy);
		std::printf("     (the badge, locked: %s)\n", qPrintable(badgeWhy));
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* the badge in both themes, for a look */
			const bool wasDark = Theme::isDark();
			for (const bool dark : { false, true }) {
				Theme::apply(*qApp, dark);
				QApplication::processEvents();
				view->grab().save(qEnvironmentVariable("EVRE_TEST_SHOT")
						+ (dark ? QStringLiteral("_lock_badge_dark.png") : QStringLiteral("_lock_badge_light.png")));
			}
			Theme::apply(*qApp, wasDark);
		}
		/* 30 frames: the view's end moves by whole periods of 20 ms, so the wave stands still */
		double first = NAN, worst = 0;
		int moves = 0, texts = 0;
		double previous = NAN;
		for (int k = 0; k < 30; k++) {
			frame();
			double t0, t1;
			bool cursors;
			view->range(t0, t1, cursors);
			if (std::isnan(first)) first = t1;
			const double periods = (t1 - first) / 0.02;
			worst = std::max(worst, std::fabs(periods - std::round(periods)));
			if (std::isfinite(previous) && t1 != previous) moves++;
			previous = t1;
			if (view->stateFullText() != QStringLiteral("Auto (short window)")) texts++;
		}
		std::printf("     (at 1 s locked %d; at 20 ms: locked %d, the corner \"%s\", the level %.3f; 30 frames: %d moves, "
				"off whole periods by %.6f at most, %d other texts)\n", int(!notAtOneSecond), int(locked),
				qPrintable(view->stateFullText()), view->triggerLevel(), moves, worst, texts);
		check(notAtOneSecond && locked && moves >= 10 && worst < 1e-3 && texts == 0, "chart, short windows lock: a live 20 ms "
				"view of a 50 Hz sine with the trigger off locks by itself (Auto on its first line's middle, rising): across 30 "
				"frames the view's end moves by whole periods (the wave stands still), the corner says \"Auto (short "
				"window)\"; no row, tab or flag, the button says Hold");

		/* no crossing: free running, live */
		flat = true;
		for (int k = 0; k < 90; k++) frame();
		const bool free = view->shortLocked() && view->live() && view->stateFullText() == QStringLiteral("Auto · free running");
		const bool badgeFree = badgeSeen(badgeWhy);
		std::printf("     (the badge, free running: %s)\n", qPrintable(badgeWhy));
		flat = false;
		for (int k = 0; k < 10; k++) frame();
		const bool lockedAgain = view->stateFullText() == QStringLiteral("Auto (short window)");
		/* Display's entry off: live, no lock, saved; on again: locked */
		lock->setChecked(false);
		for (int k = 0; k < 5; k++) frame();
		const bool entryOff = !view->shortLocked() && view->live() && view->stateFullText().isEmpty()
				&& !QSettings().value(QStringLiteral("chart/autoShortWindows"), true).toBool();
		const bool badgeGone = view->stateBadgeRect().isEmpty();
		lock->setChecked(true);
		for (int k = 0; k < 10; k++) frame();
		const bool entryOn = view->shortLocked() && QSettings().value(QStringLiteral("chart/autoShortWindows")).toBool();
		/* 100 ms: not a short window */
		view->setWindow(0.1);
		for (int k = 0; k < 5; k++) frame();
		const bool offAt100 = !view->shortLocked() && view->live();
		view->setWindow(0.02);
		for (int k = 0; k < 10; k++) frame();
		/* Hold: the lock ends, the view held (the button Live), until Live */
		hold->click();
		for (int k = 0; k < 5; k++) frame();
		const bool held = !view->shortLocked() && !view->live() && hold->text() == QStringLiteral("Live");
		hold->click();
		for (int k = 0; k < 10; k++) frame();
		const bool liveLocks = view->shortLocked() && view->live() && hold->text() == QStringLiteral("Hold");
		/* the user's trigger takes over, its row shown; off again: the lock comes back once the view is live */
		action->setChecked(true);
		for (int k = 0; k < 5; k++) frame();
		const bool takesOver = view->triggerOn() && !view->shortLocked() && row->isVisible()
				&& view->stateFullText().startsWith(view->triggerMode() == ChartView::TriggerMode::Auto ? QStringLiteral("Auto")
					: view->triggerMode() == ChartView::TriggerMode::Normal ? QStringLiteral("Normal") : QStringLiteral("Single"))
				&& !view->triggerLevelTag().isEmpty();
		action->setChecked(false);
		view->setLive(true);
		for (int k = 0; k < 10; k++) frame();
		const bool backAfter = view->shortLocked() && !view->triggerOn();
		std::printf("     (free running %d, locked again %d; the entry off %d, on %d; at 100 ms off %d; Hold %d, Live locks %d; "
				"the user's trigger %d, after it %d)\n", int(free), int(lockedAgain), int(entryOff), int(entryOn), int(offAt100),
				int(held), int(liveLocks), int(takesOver), int(backAfter));
		check(free && lockedAgain && entryOff && entryOn && offAt100 && held && liveLocks && takesOver && backAfter,
				"chart, short windows lock: \"Auto · free running\" while the line does not cross, locked again when it does; "
				"off with Display's \"Lock short windows\" (saved as chart/autoShortWindows), at a 100 ms window and with "
				"Hold (until Live); the user's trigger takes over and the lock comes back after it");
		check(badgeLocked && badgeFree && badgeGone, "chart, short windows lock: \"Auto (short window)\" and \"Auto · free "
				"running\" as a badge, the accent colour on a tint of it, its tooltip what the lock does and where it is "
				"turned off; none without the lock");
		chart.tab.hide();
		view->setWindow(1);
		clearTriggerSettings();
	}

	/* Trigger v2, the flow (U-4, U-9) and the legend's chips: Off at the row's end and the chip's entry, ticked for the
	 * line watched, turn the trigger off as Display -> Trigger does; a line watched that leaves the chart stops the
	 * trigger ("no line to watch", its name kept, greyed) and it arms again on that line when it is back, never on
	 * another; each chip's "▾": a hand, lit, a tooltip, and a click opens the right-click's menu; in Arabic a number and
	 * its unit are one left-to-right piece (U+2066 ... U+2069) */
	void chartTriggerFlow() {
		clearTriggerSettings();
		TriggerPair pair;
		ChartView *view = pair.view;
		pair.feed(99.95);
		pair.tab.show();
		(void) QTest::qWaitForWindowExposed(&pair.tab);
		(void) view->grab();
		auto *action = pair.tab.findChild<QAction *>(QStringLiteral("chartTrigger"));
		auto *row = pair.tab.findChild<QWidget *>(QStringLiteral("triggerRow"));
		auto *line = pair.tab.findChild<QComboBox *>(QStringLiteral("triggerLine"));
		auto *mode = pair.tab.findChild<QComboBox *>(QStringLiteral("triggerMode"));
		auto *off = pair.tab.findChild<QPushButton *>(QStringLiteral("triggerOff"));
		auto *state = pair.tab.findChild<ElidedLabel *>(QStringLiteral("triggerState"));
		if (!action || !row || !line || !mode || !off || !state) {
			check(false, "chart, Trigger's flow: the row's controls found");
			return;
		}

		/* Off: beside the row's other buttons, before the state (not at the row's far end), with its tooltip; a click is
		 * Display -> Trigger unticked */
		pair.tab.triggerOnLine(pair.ampsKey());
		QApplication::processEvents();
		const bool offShown = off->isVisible() && off->text() == QStringLiteral("Off")
				&& off->toolTip() == QStringLiteral("Turn the trigger off (as Display → Trigger)")
				&& off->geometry().right() <= state->geometry().left() && state->geometry().left() - off->geometry().right() <= 24;
		/* the row in two lines where one does not hold its controls and some of the state (the main window's narrowest
		 * stays 1280 px with Linux's fonts and in Arabic): the line to the mode on the first, the hold-off to Off and the
		 * state on the second; every control shown, as wide as it asks, none over another, inside the row; one line
		 * where the tab is wide */
		bool twoLines = false, oneLine = false, whole = true;
		QString rowNotes;
		for (const bool narrow : { true, false }) {
			const int width = narrow ? pair.tab.minimumSizeHint().width() : 1900;
			pair.tab.resize(width, 700);
			QApplication::processEvents();
			QApplication::processEvents();
			QList<QWidget *> controls;
			for (QWidget *child : row->findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
				if (child->isVisible()) controls << child;
			for (int i = 0; i < controls.size(); i++) {
				const QRect at = controls[i]->geometry();
				bool fits = row->rect().contains(at) && (controls[i] == state || at.width() >= controls[i]->minimumSizeHint().width());
				for (int j = i + 1; j < controls.size(); j++) fits = fits && !at.intersects(controls[j]->geometry());
				if (!fits) rowNotes += QStringLiteral(" %1 at %2,%3 %4x%5;").arg(controls[i]->objectName(), QString::number(at.x()),
						QString::number(at.y()), QString::number(at.width()), QString::number(at.height()));
				whole = whole && fits;
			}
			const bool below = off->geometry().top() >= mode->geometry().bottom() && state->geometry().top() >= mode->geometry().bottom();
			const bool level = std::abs(off->geometry().center().y() - mode->geometry().center().y()) <= 2;
			if (narrow) twoLines = below;
			else oneLine = level;
			rowNotes += QStringLiteral(" the tab %1 px: the row %2 px high, %3 controls, the narrowest %4 px;").arg(width)
					.arg(row->height()).arg(controls.size()).arg(row->minimumSizeHint().width());
		}
		pair.tab.resize(1200, 700);
		QApplication::processEvents();
		std::printf("     (the trigger's row:%s two lines %d, one line %d, whole %d)\n", qPrintable(rowNotes), int(twoLines),
				int(oneLine), int(whole));
		check(twoLines && oneLine && whole, "chart, Trigger's row: in two lines where one does not hold its controls and some "
				"of the state (the line to the mode on the first, the hold-off to Off and the state on the second), one line "
				"where there is room; every control shown, as wide as it asks, none over another");

		QTest::mouseClick(off, Qt::LeftButton);
		const bool offWorks = !action->isChecked() && !row->isVisible() && !view->triggerOn();
		check(offShown && offWorks, "chart, Trigger's flow: an Off button beside the row's other buttons, before the state (tooltip \"Turn the trigger off "
				"(as Display → Trigger)\") turns the trigger off: Display -> Trigger unticked, the row hidden");

		/* the chip's entry: ticked for the line watched alone; unticked, the trigger is off; ticked on another line, on */
		const auto entry = [&pair](int key) { /* the menu as a right-click opens it, its entry taken at once */
			pair.tab.showLineMenu(key, QPoint(0, 0));
			QMenu *menu = pair.tab.lineMenu();
			QAction *watch = menu ? menu->findChild<QAction *>(QStringLiteral("triggerOnLine")) : nullptr;
			if (menu) menu->hide();
			return watch;
		};
		pair.tab.triggerOnLine(pair.ampsKey());
		QAction *watch = entry(pair.ampsKey());
		const bool tickedWatched = watch && watch->isCheckable() && watch->isChecked();
		watch = entry(pair.voltsKey());
		const bool otherUnticked = watch && watch->isCheckable() && !watch->isChecked();
		watch = entry(pair.ampsKey());
		if (watch) watch->trigger(); /* unticked */
		const bool untickedOff = !action->isChecked() && !row->isVisible() && !view->triggerOn();
		watch = entry(pair.voltsKey());
		if (watch) watch->trigger(); /* ticked */
		const bool tickedOn = action->isChecked() && view->triggerOn() && view->triggerKey() == pair.voltsKey();
		watch = entry(pair.voltsKey());
		bool inStep = watch && watch->isChecked();
		action->setChecked(false); /* the menu's own way: the entry follows */
		watch = entry(pair.voltsKey());
		inStep = inStep && watch && !watch->isChecked();
		check(tickedWatched && otherUnticked && untickedOff && tickedOn && inStep, "chart, Trigger's flow: the chip's "
				"Trigger on this line is ticked for the line watched only; unticking it turns the trigger off, ticking it on "
				"another line arms there; it follows Display -> Trigger");

		/* the line watched removed: the trigger stops, never another line, the line saved kept; back: armed on it again */
		pair.tab.triggerOnLine(pair.ampsKey());
		const int modeBefore = mode->currentData().toInt();
		pair.tab.plotRegister(pair.amps, false);
		pair.tab.refreshStatus();
		const QString savedLine = QSettings().value(QStringLiteral("chart/triggerLine")).toString();
		const bool stopped = action->isChecked() && row->isVisible() && !view->triggerOn() && view->triggerKey() == -1
				&& state->fullText() == QStringLiteral("no line to watch") && line->currentIndex() < 0
				&& line->placeholderText() == QStringLiteral("AMPS") && savedLine == QStringLiteral("AMPS");
		pair.tab.plotRegister(pair.amps, true);
		pair.feed(100.2);
		pair.tab.refreshStatus();
		const bool back = view->triggerOn() && view->triggerKey() == pair.ampsKey() && line->currentText() == QStringLiteral("AMPS")
				&& mode->currentData().toInt() == modeBefore
				&& QSettings().value(QStringLiteral("chart/triggerLine")).toString() == QStringLiteral("AMPS");
		if (!stopped || !back)
			std::printf("     (removed: on %d, key %d, \"%s\", index %d, placeholder \"%s\", saved \"%s\"; back: on %d, \"%s\")\n",
					int(view->triggerOn()), view->triggerKey(), qPrintable(state->fullText()), line->currentIndex(),
					qPrintable(line->placeholderText()), qPrintable(savedLine), int(view->triggerOn()),
					qPrintable(line->currentText()));
		check(stopped && back, "chart, Trigger's flow: the line watched removed stops the trigger (\"no line to watch\", "
				"its name greyed in the list, chart/triggerLine kept; not moved to another line); back, it is armed on it again "
				"in its mode");
		action->setChecked(false);

		/* each chip's menu button: at its right end; under the mouse a hand, lit, a tooltip; a click opens the menu */
		(void) view->grab();
		const int key = pair.voltsKey();
		QRectF chip;
		for (const QRectF &rect : view->legendChips())
			if (view->chipAt(rect.center()) == key) chip = rect;
		const QRectF button = view->chipButtonRect(key);
		const bool placed = !button.isEmpty() && chip.contains(button) && chip.right() - button.right() < 6
				&& button.width() >= 14 && button.height() >= 14;
		QEvent leave(QEvent::Leave);
		QApplication::sendEvent(view, &leave);
		const QImage rest = view->grab().toImage();
		const QPointF at = chip.center();
		QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(view, &move);
		const QImage lit = view->grab().toImage();
		const qreal dpr = lit.devicePixelRatio(); /* the picture in device pixels (225 % on the owner's screen) */
		const QRect area = QRectF(button.topLeft() * dpr, button.size() * dpr).toAlignedRect();
		const bool hover = view->hoveredChip() == key && view->cursor().shape() == Qt::PointingHandCursor
				&& rest.copy(area) != lit.copy(area);
		const QString tip = view->toolTipAt(at);
		view->setRecording(true); /* a recording's window: no trigger to offer */
		const QString recordingTip = view->toolTipAt(at);
		view->setRecording(false);
		const bool tipped = tip == QStringLiteral("Click or right-click: Histogram, Spectrum, Trigger on this line")
				&& recordingTip == QStringLiteral("Click or right-click: Histogram, Spectrum");
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, at.toPoint());
		QMenu *menu = pair.tab.lineMenu();
		const bool opened = menu && QTest::qWaitFor([&] { return menu->isVisible(); }, 2000)
				&& menu->actions().value(0) && menu->actions().value(0)->text() == QStringLiteral("Histogram of VOLTS")
				&& menu->findChild<QAction *>(QStringLiteral("triggerOnLine"))
				&& menu->geometry().top() >= view->mapToGlobal(chip.bottomLeft().toPoint()).y();
		if (menu) menu->close();
		if (!placed || !hover || !tipped || !opened)
			std::printf("     (the chip's button: placed %d, lit %d (hovered %d), tooltip \"%s\", opened %d)\n", int(placed),
					int(hover), view->hoveredChip(), qPrintable(tip), int(opened));
		check(placed && hover && tipped && opened, "chart, the legend: each chip has a \"▾\" at its right end; over the "
				"chip a pointing hand, the button lit and the tooltip \"Click or right-click: Histogram, Spectrum, Trigger "
				"on this line\" (a recording: without the trigger); a click opens the right-click's menu under the chip");
		pair.tab.hide();

		/* Arabic: a number and its unit one left-to-right piece where the formatters join them, and in the texts */
		language::apply(*qApp, QStringLiteral("ar"));
		const QChar lri(0x2066), pdi(0x2069);
		const auto piece = [lri, pdi](const QString &text) { return lri + text + pdi; };
		bool over = false;
		const QString need = ChartTab::ramNeedText(qint64(122) * 1024 * 1024, 2048, 30, over);
		const QString seconds = secondsText(0.5), span = durationText(72.34);
		QString info;
		{
			LoneChart arabic(QStringLiteral("AR"), QStringLiteral("V"));
			info = arabic.tab.infoText();
		}
		const QString fps = QCoreApplication::translate("ChartTab", " · %1 fps"); /* a tab never painted is idle */
		const QString fill = QCoreApplication::translate("ChartView", "held: filling, %1 s to come · Live to follow");
		const QString delay = QCoreApplication::translate("ChartTab", " · delay %1 ms");
		QString help;
		{
			HelpDialog dialog;
			auto *topics = dialog.findChild<QListWidget *>(QStringLiteral("helpTopics"));
			auto *page = dialog.findChild<QTextBrowser *>();
			for (int i = 0; topics && page && i < topics->count(); i++) {
				topics->setCurrentRow(i);
				help += page->toPlainText();
			}
		}
		const bool arabicPieces = seconds == piece(QStringLiteral("500 ms")) && span == piece(QStringLiteral("1 min 12.3 s"))
				&& need.contains(piece(QStringLiteral("122 MB"))) && !need.startsWith(lri) && parseSeconds(seconds) == 0.5
				&& fps.contains(piece(QStringLiteral("%1 fps"))) && fill.contains(piece(QStringLiteral("%1 s")))
				&& delay.contains(piece(QStringLiteral("%1 ms"))) && help.contains(piece(QStringLiteral("10 s")))
				&& help.contains(piece(QStringLiteral("2 V")));
		language::apply(*qApp, QStringLiteral("en"));
		const bool englishPlain = secondsText(0.5) == QStringLiteral("500 ms") && durationText(72.34) == QStringLiteral(
				"1 min 12.3 s");
		if (!arabicPieces || !englishPlain)
			std::printf("     (Arabic: \"%s\" \"%s\" \"%s\", info \"%s\", \"%s\", \"%s\", help 10 s %d, 2 V %d; English plain %d)\n",
					qPrintable(seconds), qPrintable(span), qPrintable(need), qPrintable(info), qPrintable(fill),
					qPrintable(delay), int(help.contains(piece(QStringLiteral("10 s")))),
					int(help.contains(piece(QStringLiteral("2 V")))), int(englishPlain));
		check(arabicPieces && englishPlain, "chart, Arabic numbers: a number and its unit one left-to-right piece (U+2066 "
				"... U+2069) in the window's times, durations, sizes, fps and ms, the fill time and the Help (10 s, 2 V); "
				"English as it was");
		clearTriggerSettings();
	}

	/* Recordings in windows of their own: opened from the file (with the map: names matched, a byte array left out, a
	 * math line computed from the file), held, titled with the name and span, notes read and saved; the RAM question;
	 * dropped on the window; several at once while the live chart goes on; a recording's notes written while it runs */
	/* EVRE_TEST_SHOT: pictures of a recording's window for a look (the viewer's review), in each theme and language,
	 * Lanes off and on, at its first size, smaller and bigger, and measured between two cursors */
	void recordingWindowPictures(const QString &file, const QString &tag) {
		if (!qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) return;
		const QString prefix = qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_viewer_") + tag;
		const QVariant lanesBefore = QSettings().value(QStringLiteral("recording/lanes"));
		const QVariant measureBefore = QSettings().value(QStringLiteral("recording/measure"));
		const auto picture = [&](const QString &name, bool lanes, const QSize &size, bool measure = false) {
			QSettings().setValue(QStringLiteral("recording/lanes"), lanes);
			QSettings().setValue(QStringLiteral("recording/measure"), measure);
			RecordingWindow *shown = nullptr;
			RecordingWindow::open(nullptr, file, map_.regs, 2048, [&](RecordingWindow *w) { shown = w; });
			(void) QTest::qWaitFor([&] { return shown != nullptr; }, 5000);
			if (!shown) return;
			(void) QTest::qWaitForWindowExposed(shown);
			if (size.isValid()) shown->resize(size);
			if (measure) {
				shown->chartTab()->view()->setCursors(shown->firstTime() + (shown->lastTime() - shown->firstTime()) * 0.3,
						shown->firstTime() + (shown->lastTime() - shown->firstTime()) * 0.6);
			}
			QTest::qWait(measure ? 1200 : 600);
			shown->grab().save(prefix + QLatin1Char('_') + name + QStringLiteral(".png"));
			delete shown;
		};
		for (const bool dark : { true, false }) {
			Theme::apply(*qApp, dark);
			const QString theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
			picture(theme, false, QSize());
			picture(theme + QStringLiteral("_lanes"), true, QSize());
			picture(theme + QStringLiteral("_measure"), false, QSize(), true);
		}
		Theme::apply(*qApp, true);
		picture(QStringLiteral("small"), false, QSize(900, 600));
		picture(QStringLiteral("big"), false, QSize(1600, 900));
		picture(QStringLiteral("big_lanes"), true, QSize(1600, 900));
		language::apply(*qApp, QStringLiteral("ar"));
		picture(QStringLiteral("ar_dark"), false, QSize());
		picture(QStringLiteral("ar_dark_lanes"), true, QSize());
		Theme::apply(*qApp, false);
		picture(QStringLiteral("ar_light_measure"), false, QSize(), true);
		Theme::apply(*qApp, true);
		language::apply(*qApp, QStringLiteral("en"));
		for (const auto &[key, before] : { std::pair{ QStringLiteral("recording/lanes"), lanesBefore },
					 std::pair{ QStringLiteral("recording/measure"), measureBefore } }) {
			if (before.isValid()) QSettings().setValue(key, before);
			else QSettings().remove(key);
		}
	}

	/* The recording's window revisited (P5): opened with Measure on, it showed an empty chart at 0..1 (the file's
	 * samples held back by the opening's measurement, and the change after the feed never painted: no frames come to
	 * it), the Y boxes "0" and "1" in Auto's grey; made bigger on the card, a frame the system let go left the layer at
	 * its old size beside a blank bar; its Y row spread over the window; a theme switch passed it by. */
	void recordingViewer(const QString &path) {
		const QVariant measureBefore = QSettings().value(QStringLiteral("recording/measure"));
		const QVariant lanesBefore = QSettings().value(QStringLiteral("recording/lanes"));
		QSettings().setValue(QStringLiteral("recording/measure"), true);
		QSettings().setValue(QStringLiteral("recording/lanes"), false);
		QSettings().setValue(QStringLiteral("recording/yAuto"), true);
		RecordingWindow::closeAll();
		RecordingWindow *opened = nullptr;
		QElapsedTimer since;
		since.start();
		RecordingWindow::open(nullptr, path, map_.regs, 2048, [&](RecordingWindow *w) { opened = w; });
		(void) QTest::qWaitFor([&] { return opened != nullptr; }, 5000);
		if (!opened) {
			check(false, "recording's window revisited: opened");
			return;
		}
		ChartView *view = opened->chartTab()->view();
		const int volts = int(regKey(opened->definitions().value(0)));
		const qint64 keptAtOnce = view->pointsKept(volts);
		const QString valueAtOnce = view->legendValue(volts);
		(void) QTest::qWaitForWindowExposed(opened);
		QTest::qWait(150); /* its first frame, under the status's 500 ms */
		auto *low = opened->findChild<QLineEdit *>(QStringLiteral("yMin"));
		auto *high = opened->findChild<QLineEdit *>(QStringLiteral("yMax"));
		const qint64 openedMs = since.elapsed();
		const bool boxes = low && high && low->text() == ChartTab::yFieldText(view->yLo(), false)
				&& high->text() == ChartTab::yFieldText(view->yHi(), false) && view->yLo() < 12 && view->yHi() > 17.9;
		std::printf("  opened with Measure on: %lld samples and \"%s\" at once; Y %g..%g, the boxes \"%s\" \"%s\" after %lld ms\n",
				(long long) keptAtOnce, qPrintable(valueAtOnce), view->yLo(), view->yHi(), low ? qPrintable(low->text()) : "",
				high ? qPrintable(high->text()) : "", (long long) openedMs);
		check(keptAtOnce == 600 && !valueAtOnce.isEmpty(), "recording's window, Measure on: the file's samples on the chart "
				"at once and the legend's values shown (none held back by the opening's measurement)");
		check(boxes && openedMs < 500, "recording's window: the Y boxes show the range of its first frame at once (Auto), "
				"not 0 and 1 until the status's next turn");

		/* a change just after a feed's frames (no frames come after them): painted all the same */
		opened->chartTab()->frame({});
		QApplication::processEvents(); /* the frame's own paint */
		const int paintsBefore = view->paints();
		view->showLastValues();
		const bool painted = QTest::qWaitFor([&] { return view->paints() > paintsBefore; }, 600);
		check(painted, "recording's window: a change just after a feed's frames is painted though no frame follows");

		/* the Y row: its groups packed, as in the live chart's (the room of the hidden Memory and RAM a stretch) */
		QLabel *minLabel = nullptr, *rangeLabel = nullptr;
		for (QLabel *label : opened->findChildren<QLabel *>()) {
			if (label->text() == QLatin1String("min")) minLabel = label;
			if (label->text() == QLatin1String("Y range")) rangeLabel = label;
		}
		auto *mode = opened->findChild<QComboBox *>(QStringLiteral("yMode"));
		/* each label and list as wide as it needs: the text of "min" beside its box, not across a stretched label */
		int spread = minLabel && rangeLabel && mode ? 0 : 1000;
		for (QWidget *w : std::initializer_list<QWidget *>{ minLabel, rangeLabel, mode })
			if (w) spread = std::max(spread, w->width() - w->sizeHint().width());
		std::printf("  the Y row: its label and list at most %d px wider than they need\n", spread);
		check(spread <= 4, "recording's window: the Y row packed, \"Y range\" beside its list and \"min\" beside its box "
				"(the room of the hidden Memory and RAM not spread over the labels)");

		/* made bigger on the CPU: the window's own pixels are the chart's picture at once */
		opened->resize(1000, 620);
		view->setDrawing(ChartView::Drawing::Cpu);
		QTest::qWait(300);
		opened->resize(1300, 800);
		QApplication::processEvents();
		const QImage own = opened->screen()->grabWindow(opened->winId()).toImage().convertToFormat(QImage::Format_RGB32);
		const QImage picture = opened->grab().toImage().convertToFormat(QImage::Format_RGB32);
		const double cpuAlike = own.size() == picture.size() ? blocksAlike(own, picture, 24) : 0;
		std::printf("  made bigger on the CPU: the window's pixels %.2f%% like its picture\n", cpuAlike * 100);
		check(cpuAlike >= 0.97, "recording's window made bigger on the CPU: all of it painted at once, the new part too");

		/* on the card: a frame the system lets go (busy) as the window grows is followed by another, the layer at the
		 * new size; on the screen the plot is the CPU's picture */
		const QVector<GpuLines::Adapter> adapters = GpuLines::adapters();
		if (adapters.isEmpty()) {
			check(true, "recording's window made bigger on a GPU: no adapter on this machine (Direct3D 11 on Windows "
					"only): the CPU draws, skipped");
		} else {
			opened->resize(1000, 620);
			view->setDrawing(adapters.first().dedicated ? ChartView::Drawing::Dedicated : ChartView::Drawing::Internal);
			(void) QTest::qWaitFor([&] { return !view->openingGpu(); }, 10000);
			for (int k = 0; k < 3; k++) {
				view->repaint();
				QApplication::processEvents();
			}
			QTest::qWait(200);
			const int dropped = view->gpuDropped();
			view->dropNextGpuFrame();
			opened->resize(1300, 800);
			QApplication::processEvents();
			QRect at;
			(void) view->gpuPicture(&at);
			const bool again = QTest::qWaitFor([&] { return view->gpuPresentedSize() == at.size(); }, 500);
			QTest::qWait(100);
			const QRect g = opened->geometry();
			const QImage screen = opened->screen()->grabWindow(0, g.x(), g.y(), g.width(), g.height()).toImage()
					.convertToFormat(QImage::Format_RGB32);
			const QImage cpu = opened->grab().toImage().convertToFormat(QImage::Format_RGB32);
			const double alike = screen.size() == cpu.size() ? blocksAlike(screen.copy(at), cpu.copy(at), 24) : 0;
			std::printf("  made bigger on %s: a frame let go (%d), the layer %dx%d for the plot's %dx%d; on the screen "
					"%.2f%% like the CPU's picture\n", qPrintable(view->drawingName()), view->gpuDropped() - dropped,
					view->gpuPresentedSize().width(), view->gpuPresentedSize().height(), at.width(), at.height(), alike * 100);
			check(view->plotOnCard() && view->gpuDropped() == dropped + 1 && again && alike >= 0.97,
					"recording's window made bigger on a GPU: a frame the system let go is followed by another, the card's "
					"layer at the new size (no blank bar over the new part), its plot the CPU's picture");
			view->setDrawing(ChartView::Drawing::Cpu);
		}

		/* the theme switched: its boxes in the new theme at once */
		auto *sidebarCard = window_.findChild<Sidebar *>();
		if (sidebarCard) emit sidebarCard->themeClicked();
		const bool themed = low && low->styleSheet() == QStringLiteral("color:%1").arg(Theme::colors().muted.name());
		if (sidebarCard) emit sidebarCard->themeClicked();
		check(sidebarCard && themed && Theme::isDark(), "recording's window: the theme switched reaches it too (its Y "
				"boxes in the new theme's grey at once)");

		RecordingWindow::closeAll();
		for (const auto &[key, before] : { std::pair{ QStringLiteral("recording/measure"), measureBefore },
					 std::pair{ QStringLiteral("recording/lanes"), lanesBefore } }) {
			if (before.isValid()) QSettings().setValue(key, before);
			else QSettings().remove(key);
		}
		QSettings().remove(QStringLiteral("recording/drawing"));
	}

	void recordingWindows() {
		QTemporaryDir folder;
		const QString path = folder.filePath(QStringLiteral("bench.csv"));
		QFile file(path);
		const QString volts = regs_.volts.name, amps = regs_.amps.name;
		if (file.open(QIODevice::WriteOnly)) {
			file.write(QStringLiteral("time_s,datetime,%1 [V],%2 [A],LED_MODE,MSG_BUFFER,CONFIG,UNKNOWN [bar]\n")
					.arg(volts, amps).toUtf8());
			for (int i = 0; i < 600; i++) /* 10 Hz for a minute; UNKNOWN empty at every tenth */
				file.write(QStringLiteral("%1,2026-10-05T09:00:%2.%3,%4,%5,%6,00ff00,%7,%8\n").arg(100 + i * 0.1, 0, 'f', 6)
						.arg(i / 10, 2, 10, QLatin1Char('0')).arg((i % 10) * 100, 3, 10, QLatin1Char('0'))
						.arg(12.0 + 0.01 * i).arg(2.0).arg(i % 3).arg(i % 8)
						.arg(i % 10 ? QString::number(i) : QString()).toUtf8());
			file.close();
		}
		QString error;
		recording::saveNotes(path, { { 130.0, QStringLiteral("half way") } }, 0, error);
		QSettings().setValue(QStringLiteral("recording/math"), QStringList{ QStringLiteral("P\tW\t%1 * %2\t1").arg(volts, amps) });
		RecordingWindow::closeAll();
		auto *liveView = window_.findChild<ChartView *>();
		window_.openRecording(path);
		RecordingWindow *opened = nullptr;
		(void) QTest::qWaitFor([&] { return !(RecordingWindow::windows().isEmpty() || !(opened = RecordingWindow::windows().first())); }, 5000);
		if (!opened) {
			check(false, "recording window: opened");
			return;
		}
		(void) QTest::qWaitForWindowExposed(opened);
		ChartView *view = opened->chartTab()->view();
		const QVector<RegDef> defs = opened->definitions();
		const auto keyOf = [&](const QString &name) {
			for (const RegDef &def : defs)
				if (def.name == name) return int(regKey(def));
			return -1;
		};
		auto *hold = opened->findChild<QPushButton *>(QStringLiteral("hold"));
		const bool title = opened->windowTitle().startsWith(QStringLiteral("bench.csv · 2026-10-05 09:00:00 – 09:00:59 (59.9 s)"));
		const bool held = !view->live() && hold && !hold->isVisible() && std::fabs(view->window() - 59.9) < 1e-6;
		const bool lines = defs.size() == 5 && keyOf(QStringLiteral("MSG_BUFFER")) < 0 && opened->skipped() == 1
				&& view->pointsKept(keyOf(volts)) == 600 && view->pointsKept(keyOf(QStringLiteral("UNKNOWN"))) == 540;
		bool matched = false, made = false;
		for (const RegDef &def : defs) {
			if (def.name == QLatin1String("LED_MODE")) matched = !def.enumValues.isEmpty();
			if (def.name == QLatin1String("UNKNOWN")) made = def.unit == QLatin1String("bar") && def.enumValues.isEmpty();
		}
		const int powerKey = lineKey(view, QStringLiteral("ƒ P"));
		const ChartView::Stats power = view->stats(powerKey);
		const bool math = powerKey >= 0 && view->pointsKept(powerKey) == 600 && std::fabs(power.max - 2 * (12.0 + 5.99)) < 1e-6;
		const bool notes = view->notes() == QVector<ChartNote>{ { 130.0, QStringLiteral("half way") } };
		if (!title || !held || !lines)
			std::printf("     (\"%s\"; window %.3f s; %d lines, %lld and %lld samples)\n", qPrintable(opened->windowTitle()),
					view->window(), int(defs.size()), (long long) view->pointsKept(keyOf(volts)),
					(long long) view->pointsKept(keyOf(QStringLiteral("UNKNOWN"))));
		check(title && held, "recording window: titled with the file's name and span, held on all of it (no Live)");
		check(lines && matched && made, "recording window: a line per column, the map's registers matched by name (LED_MODE "
				"with its value names), a byte array left out, an empty cell no sample");
		check(math && notes, "recording window: a math line of its own (recording/math) computed from the file; the notes "
				"beside it shown");
		recordingViewer(path);
		recordingWindowPictures(path, QStringLiteral("regs"));
		/* (closed by the revisit's checks: opened again for the rest) */
		opened = nullptr;
		window_.openRecording(path);
		(void) QTest::qWaitFor([&] { return !(RecordingWindow::windows().isEmpty() || !(opened = RecordingWindow::windows().first())); }, 5000);
		if (!opened) return;
		(void) QTest::qWaitForWindowExposed(opened);
		view = opened->chartTab()->view();
		/* held on a file, nothing comes after its end: no trigger, in a chip's menu or the Display menu */
		opened->chartTab()->showLineMenu(keyOf(volts), QPoint(0, 0));
		QMenu *chipMenu = opened->chartTab()->lineMenu();
		const bool noTriggerEntry = chipMenu && !chipMenu->actions().isEmpty()
				&& !chipMenu->findChild<QAction *>(QStringLiteral("triggerOnLine"));
		if (chipMenu) chipMenu->hide();
		auto *recordingTrigger = opened->chartTab()->findChild<QAction *>(QStringLiteral("chartTrigger"));
		check(noTriggerEntry && recordingTrigger && !recordingTrigger->isVisible(), "recording window: no trigger, neither "
				"in a line's chip menu nor in the Display menu (the recording is held, nothing comes after its end)");

		/* a field from the Lines menu; a note added: saved beside the file */
		auto *linesButton = opened->findChild<QPushButton *>(QStringLiteral("recordingLines"));
		QAction *field = nullptr;
		if (linesButton && linesButton->menu()) {
			emit linesButton->menu()->aboutToShow();
			for (QAction *action : linesButton->menu()->actions())
				if (action->menu() && action->text() == QLatin1String("Fields of CONFIG"))
					for (QAction *f : action->menu()->actions())
						if (!field) field = f;
		}
		if (field) field->trigger();
		const int fieldKey = field ? lineKey(view, QStringLiteral("ƒ CONFIG.") + field->text()) : -1;
		view->addNote(140.0, QStringLiteral("added"));
		QVector<ChartNote> saved;
		recording::loadNotes(path, saved, error);
		if (qEnvironmentVariableIsSet("EVRE_TEST_SHOT")) { /* a recording window, for a look */
			(void) view->grab();
			opened->grab().save(qEnvironmentVariable("EVRE_TEST_SHOT") + QStringLiteral("_recording.png"));
		}
		check(fieldKey >= 0 && view->pointsKept(fieldKey) == 600 && saved.size() == 2
						&& view->pointsKept(keyOf(volts)) == 600,
				"recording window: a register's field plotted from the Lines menu (from the file's values); a note added is "
				"saved beside the file");

		/* the live chart goes on; a second recording, dropped on the window */
		const int livePaints = liveView ? liveView->paints() : 0;
		const bool live = liveView && liveView->live();
		auto *tabs = window_.findChild<QTabWidget *>();
		if (tabs) tabs->setCurrentIndex(1);
		const QString copy = folder.filePath(QStringLiteral("copy.csv"));
		QFile::copy(path, copy);
		QMimeData mime;
		mime.setUrls({ QUrl::fromLocalFile(copy) });
		QDragEnterEvent enter(QPoint(200, 200), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(&window_, &enter);
		QDropEvent drop(QPointF(200, 200), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(&window_, &drop);
		const bool two = enter.isAccepted() && QTest::qWaitFor([&] { return RecordingWindow::windows().size() == 2; }, 5000);
		QTest::qWait(300);
		const bool goesOn = live && liveView->live() && liveView->paints() > livePaints;
		if (tabs) tabs->setCurrentIndex(0);
		check(two && goesOn, "recording window: a .csv dropped on the window opens a second; the live chart goes on");

		/* more than the RAM: the last part, if wanted */
		RecordingWindow *part = nullptr;
		const QString asked = answerDialog(QStringLiteral("Keep the last part"), [&] {
			RecordingWindow::open(nullptr, path, {}, 0, [&](RecordingWindow *w) { part = w; });
		});
		(void) QTest::qWaitFor([&] { return part != nullptr; }, 5000);
		const qint64 kept = part ? part->chartTab()->view()->pointsKept(int(regKey(part->definitions().value(0)))) : -1;
		bool none = true;
		const QString refused = answerDialog(QStringLiteral("Cancel"), [&] {
			RecordingWindow::open(nullptr, path, {}, 0, [&](RecordingWindow *) { none = false; });
		});
		QTest::qWait(300);
		check(asked == QLatin1String("Open recording") && kept == 0 && refused == asked && none,
				"recording window: a file bigger than the chart's RAM asks to keep the last part (none of it at RAM 0), "
				"Cancel opens nothing");
		RecordingWindow::closeAll();

		/* the last 8, newest first; the sidebar's Open recording beside Record CSV */
		for (int i = 0; i < 10; i++) RecordingWindow::remember(folder.filePath(QStringLiteral("r%1.csv").arg(i)));
		const QStringList recent = RecordingWindow::recentFiles();
		auto *openButton = window_.findChild<QPushButton *>(QStringLiteral("openRecording"));
		QPushButton *record = buttonWithText(QStringLiteral("●  Record CSV"));
		check(recent.size() == 8 && recent.first().endsWith(QLatin1String("r9.csv")) && openButton && openButton->menu()
						&& record && openButton->isVisible() && std::abs(openButton->geometry().center().y()
								- record->geometry().center().y()) <= 2,
				"recording: Open recording beside Record CSV, its menu the last 8 recordings, newest first");

		/* a recording's notes written while it runs */
		const QString recorded = folder.filePath(QStringLiteral("live.csv"));
		MainWindow::Startup start;
		start.record = recorded;
		window_.applyStartup(start);
		QPushButton *stop = nullptr;
		(void) QTest::qWaitFor([&] { return (stop = buttonWithText(QStringLiteral("■  Stop recording"))) != nullptr; }, 3000);
		QTest::qWait(400);
		auto *chartTab = window_.findChild<ChartTab *>();
		if (chartTab) chartTab->view()->addNote(chartTab->view()->timeNow(), QStringLiteral("while recording"));
		QVector<ChartNote> written;
		const bool during = recording::loadNotes(recorded, written, error) && written.size() == 1;
		QTest::qWait(400);
		if (stop) stop->click();
		(void) QTest::qWaitFor([&] { return !buttonWithText(QStringLiteral("■  Stop recording")); }, 3000);
		RecordingWindow *again = nullptr;
		RecordingWindow::open(nullptr, recorded, map_.regs, 2048, [&](RecordingWindow *w) { again = w; });
		(void) QTest::qWaitFor([&] { return again != nullptr; }, 5000);
		const bool shownThere = again && again->chartTab()->view()->notes().size() == 1
				&& again->chartTab()->view()->notes()[0].text == QLatin1String("while recording");
		check(stop && during && shownThere, "recording: a note added while recording is written beside the file at once, "
				"and the recording opened shows it");
		if (chartTab) chartTab->view()->setNotes({});
		RecordingWindow::closeAll();
		QSettings().remove(QStringLiteral("recording/math"));
	}

	/* Plot shown with more registers than the example map has (70 numeric, on a Registers tab of its own; the first
	 * one marked not plottable in the map): no question, the first 64 it may plot on the chart, the rest left off and
	 * said; a 65th Plot ticked: refused. Then the limit by the rate: 64,000 samples a second for all the lines, a lower
	 * limit taking the newest off. */
	void plotShownWithoutQuestion() {
		RegisterModel many;
		MapDocument doc;
		RegistersTab tab(&many, &doc);
		QVector<RegDef> defs;
		for (int i = 0; i < 70; i++) {
			RegDef def;
			def.addr = uint16_t(0xD000 + 2 * i);
			def.name = QStringLiteral("R%1").arg(i);
			def.uid = quint32(i + 1);
			def.plottable = i != 0; /* "plot": false: a fixed value */
			defs << def;
		}
		many.setDefinitions(defs);
		bool asked = false;
		QTimer::singleShot(300, [&] {
			if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
				asked = true;
				dialog->reject();
			}
		});
		QString said;
		QObject::connect(&tab, &RegistersTab::statusMessage, &tab, [&](const QString &text, int) { said = text; });
		int refusals = 0;
		QObject::connect(&many, &RegisterModel::plotLimitReached, &tab, [&] { refusals++; });
		auto *button = tab.findChild<QPushButton *>(QStringLiteral("plotShown"));
		if (button) button->click();
		QTest::qWait(400);
		const bool first64 = many.plottedCount() == RegisterModel::MAX_PLOTTED && !many.rows()[0].plot
				&& many.rows()[64].plot && !many.rows()[65].plot;
		const bool ticked = many.setData(many.index(69, RegisterModel::ColPlot), Qt::Checked, Qt::CheckStateRole);
		const bool fixedNoBox = !many.data(many.index(0, RegisterModel::ColPlot), Qt::CheckStateRole).isValid()
				&& many.data(many.index(0, RegisterModel::ColPlot), Qt::ToolTipRole).toString().contains(QLatin1String("fixed"));
		std::printf("     (Plot shown said: \"%s\")\n", qPrintable(said));
		check(button && !asked && first64 && fixedNoBox && said.contains(QLatin1String("5 left off")) && !ticked
						&& refusals == 1 && many.plottedCount() == RegisterModel::MAX_PLOTTED,
				"Plot shown with 70 registers, one not plottable (no box, why in its tooltip): no question, the first 64 "
				"plottable on the chart, \"5 left off\"; a 65th Plot ticked is refused");
		/* the chart full with more shown than it holds: the button is Unplot shown (it could never be otherwise) */
		const bool unplot = button && button->text() == QLatin1String("Unplot shown");
		if (button) button->click();
		check(unplot && many.plottedCount() == 0 && button->text() == QLatin1String("Plot shown"),
				"Plot shown, the chart full: the button is Unplot shown and takes them all off, then Plot shown again");
		if (button) button->click(); /* the 64 back, for the limit below */

		/* the limit by the rate */
		const bool limits = RegisterModel::plotLimitFor(0) == 64 && RegisterModel::plotLimitFor(500) == 64
				&& RegisterModel::plotLimitFor(1000) == 64 && RegisterModel::plotLimitFor(1500) == 42
				&& RegisterModel::plotLimitFor(2000) == 32 && RegisterModel::plotLimitFor(4000) == 16;
		QStringList off;
		QObject::connect(&many, &RegisterModel::plotsTakenOff, &tab, [&](const QStringList &names) { off = names; });
		many.setPlotLimit(RegisterModel::plotLimitFor(2000));
		const bool newestOff = many.plottedCount() == 32 && many.rows()[32].plot && !many.rows()[33].plot
				&& off.size() == 32 && off.first() == QLatin1String("R64");
		check(limits && newestOff,
				"the plot limit by the rate: 64,000 samples a second (1000 Hz 64, 1500 Hz 42, 2000 Hz 32, 4000 Hz 16); "
				"2000 Hz with 64 plotted takes the 32 newest off, named");
	}

	/* 12. the oscilloscope: a math line, cursors, the measurements, hold / live, memory */
	void chart() {
		auto *view = window_.findChild<ChartView *>();
		auto *tabs = window_.findChild<QTabWidget *>();
		const int voltsKey = plotRegister(regs_.volts.name);
		const int ampsKey = plotRegister(regs_.amps.name);
		if (tabs) tabs->setCurrentIndex(1);
		QTest::qWait(2500);
		const int powerKey = view ? lineKey(view, powerLine) : -1;
		const bool unresolvedDrawn = view && lineKey(view, unresolvedLine) >= 0;
		check(view && powerKey >= 0 && voltsKey >= 0 && ampsKey >= 0 && !unresolvedDrawn,
				"math line drawn: ƒ P = V × I (a line naming no register of the map is not)");
		check(view && lineKey(view, withoutOnFlagLine) >= 0,
				"a math line saved without its on flag (three fields, as an older entry) is loaded and drawn");
		if (view && powerKey >= 0) {
			mathLine(view, powerKey, voltsKey, ampsKey);
			measurements(view);
		}
		holdAndLive(view);
		memoryFollowsView(view);
		legend(view);
		valuePace(view);
		for (int r = 0; r < model_->rows().size(); r++) model_->setPlot(r, false);
		/* back to the Registers tab: a Value column left too narrow while it was hidden (the values grew
		 * meanwhile) is wide enough at once, not only at the next status tick */
		QHeaderView *columns = table_->horizontalHeader();
		columns->resizeSection(RegisterModel::ColValue, 40);
		if (tabs) tabs->setCurrentIndex(0);
		check(columns->sectionSize(RegisterModel::ColValue) >= 110,
				"the Registers tab shown again: its Value column fits the values at once");
		QSettings().remove(QStringLiteral("chart/math"));
	}

	/* puts a register on the chart: its line's key (its address), -1 if the map has no such register */
	int plotRegister(const QString &name) {
		const QVector<RegisterModel::Row> &rows = model_->rows();
		for (int r = 0; r < rows.size() && !name.isEmpty(); r++) {
			if (rows[r].def.name != name) continue;
			model_->setPlot(r, true);
			return rows[r].def.addr;
		}
		return -1;
	}

	/* P against V and I: its value at cursor A, and its area over A..B */
	void mathLine(ChartView *view, int powerKey, int voltsKey, int ampsKey) {
		const double cursorA = view->timeNow() - 1.8;
		const double cursorB = cursorA + 1.2;
		view->setCursors(cursorA, cursorB);
		const ChartView::Stats power = view->stats(powerKey);
		const double want = view->stats(voltsKey).atA * view->stats(ampsKey).atA;
		check(power.ok && power.n >= 3 && std::fabs(power.atA - want) <= 0.03 * std::max(1.0, std::fabs(want)),
				"math line: at cursor A, P = V × I (within the interpolation between polls)");
		check(power.integral > 0 && std::fabs(power.integral / power.mean - (cursorB - cursorA)) < 0.25,
				"the area under P over A..B = its mean × the time (J)");
		check(std::isfinite(view->total(powerKey)) && view->total(powerKey) > 0 && std::isfinite(view->totalsSince()),
				"math line: its total since Clear is summed too, as a register's");
	}

	/* Measure: the table under the chart, off by default; P's area in J and Wh. The cursors go at the end. */
	void measurements(ChartView *view) {
		auto *table = window_.findChild<QTableWidget *>(QStringLiteral("measures"));
		auto *button = window_.findChild<QPushButton *>(QStringLiteral("measure"));
		check(button && !button->isChecked() && table && !table->isVisible(),
				"Measure is off by default: no table under the chart");
		if (button) button->click();
		check(QTest::qWaitFor([&] { return table && table->isVisible(); }, 2000), "Measure on: the table shows");
		QApplication::processEvents();
		QTest::qWait(400);
		check(table && powerAreaInJoulesAndWattHours(table), "the measurements table: P's area in J and Wh");
		view->clearCursors();
		if (button) button->click();
		QApplication::processEvents();
		check(table && !table->isVisible(), "Measure off again: the table hidden");
		QSettings().remove(QStringLiteral("chart/measure"));
	}

	/* Hold stops the view and Live follows now again; the button keeps its place and size */
	void holdAndLive(ChartView *view) {
		auto *hold = window_.findChild<QPushButton *>(QStringLiteral("hold"));
		const QRect geometry = hold ? hold->geometry() : QRect();
		if (hold) hold->click();
		check(QTest::qWaitFor([&] { return view && !view->live() && hold && hold->text().contains(QLatin1String("Live")); }, 2000),
				"Hold: the view stops, the button says Live");
		check(hold && hold->geometry() == geometry, "... the button stays in place, the same size");
		if (hold) hold->click();
		check(QTest::qWaitFor([&] {
			return view && view->live() && hold && hold->text().contains(QLatin1String("Hold")) && hold->geometry() == geometry;
		}, 2000), "Live: the view follows now again (the button still the same size)");
	}

	void memoryFollowsView(ChartView *view) {
		if (!view) return;
		const double window = view->window();
		view->setMemory(10);
		view->setWindow(40);
		check(view->memory() >= 40, "a view longer than the memory makes the memory grow to it");
		view->setMemory(60);
		view->setWindow(window);
	}

	/* The legend: a chip for every line, each at a fixed place while the values change. More lines than
	 * the row holds: the others scroll in with the wheel (which then leaves the time zoom alone) or the
	 * bar under the chips. */
	void legend(ChartView *view) {
		if (!view) return;
		const QVector<QRectF> before = view->legendChips();
		check(!before.isEmpty() && before.size() == view->lines().size(), "the legend: a chip for every line");
		QTest::qWait(600); /* the fake device moves the values meanwhile */
		check(view->legendChips() == before, "... the chips keep their places and widths while the values change");
		constexpr int EXTRA = 24, EXTRA_KEY = 0x20000;
		for (int i = 0; i < EXTRA; i++)
			view->addSeries(EXTRA_KEY + i, QStringLiteral("LEGEND_TEST_%1").arg(i), QStringLiteral("V"), Qt::gray);
		const QRectF row = view->legendViewport();
		check(view->legendChips().size() == view->lines().size() && view->legendChips().last().right() > row.right(),
				"more lines than the row holds: still a chip for every line, the last one beyond the row");
		view->setLegendScroll(1e9);
		check(view->legendScroll() > 0 && std::fabs(view->legendChips().last().right() - row.right()) < 0.5,
				"scrolled to the end: the last chip ends where the row ends");
		const double window = view->window(), scroll = view->legendScroll();
		QWheelEvent wheel(row.center(), view->mapToGlobal(row.center()), QPoint(), QPoint(0, 120), Qt::NoButton,
				Qt::NoModifier, Qt::NoScrollPhase, false);
		QApplication::sendEvent(view, &wheel);
		check(view->legendScroll() < scroll && view->window() == window,
				"the wheel over the legend scrolls the chips; the time zoom stays");
		const QPoint barStart(int(row.left()) + 1, int(row.bottom()) + 5); /* the bar, just under the chips */
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, barStart);
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, barStart);
		check(view->legendScroll() == 0 && std::fabs(view->legendChips().first().left() - row.left()) < 0.5,
				"a click at the bar's start: back to the first chip");
		for (int i = 0; i < EXTRA; i++) view->removeSeries(EXTRA_KEY + i);
		check(view->legendChips() == before, "the test lines removed: the chips back where they were");
	}

	/* Show values: the numbers change at the pace chosen in the sidebar, the lines at every frame */
	void valuePace(ChartView *view) {
		ValuePacer pacer;
		pacer.setPerSecond(10);
		bool paced = pacer.due(1000) && !pacer.due(1050) && pacer.due(1097) && !pacer.due(1100);
		pacer.setPerSecond(ValuePace::EVERY_FRAME);
		paced = paced && pacer.due(1101) && pacer.due(1102);
		check(paced, "values pace: 10 / s is due at most every 100 ms (a refresh a few ms early counts); "
				"every frame: at every call");
		auto *box = window_.findChild<QComboBox *>(QStringLiteral("valuePace"));
		if (!view || !box) {
			check(false, "Show values: the choice is in the sidebar");
			return;
		}
		const int savedBefore = ValuePace::saved();
		QSettings().remove(QStringLiteral("ui/valueRate"));
		check(box->currentData().toInt() == savedBefore && ValuePace::saved() == ValuePace::DEFAULT_PER_SECOND,
				"Show values: the box shows what is saved; nothing saved: 10 / s");
		/* the most changes of any chip's text in 1.6 s; the fake device moves its values at every poll */
		auto mostChanges = [&] {
			QHash<int, QString> shown;
			QHash<int, int> changes;
			QElapsedTimer clock;
			clock.start();
			while (clock.elapsed() < 1600) {
				for (const ChartView::Info &line : view->lines()) {
					const QString text = view->legendValue(line.key);
					if (shown.contains(line.key) && shown.value(line.key) != text) changes[line.key]++;
					shown[line.key] = text;
				}
				QTest::qWait(10);
			}
			int most = 0;
			for (const int n : changes) most = std::max(most, n);
			return most;
		};
		box->setCurrentIndex(box->findData(2));
		const int slow = mostChanges();
		check(ValuePace::saved() == 2 && slow >= 2 && slow <= 4,
				qPrintable(QStringLiteral("2 / s chosen: saved, and the legend's values change 2..4 times in 1.6 s "
						"(%1)").arg(slow)));
		box->setCurrentIndex(box->findData(ValuePace::EVERY_FRAME));
		const int fast = mostChanges();
		check(fast > 4, qPrintable(QStringLiteral("every frame: they change at every poll (%1 times)").arg(fast)));
		box->setCurrentIndex(box->findData(ValuePace::DEFAULT_PER_SECOND));
		check(ValuePace::saved() == ValuePace::DEFAULT_PER_SECOND, "back to 10 / s: saved");
	}

	/* 13. the login, refused and skipped */

	/* the Monitor tab's widgets: its Log frames box, its Clear button and the frames */
	struct MonitorWidgets {
		QCheckBox *logFrames = nullptr;
		QPushButton *clear = nullptr;
		QPlainTextEdit *frames = nullptr;

		bool complete() const { return logFrames && clear && frames; }
	};

	MonitorWidgets findMonitor() const {
		MonitorWidgets monitor;
		auto *tab = window_.findChild<MonitorTab *>();
		if (!tab) return monitor;
		for (QCheckBox *box : tab->findChildren<QCheckBox *>())
			if (box->text() == QLatin1String("Log frames")) monitor.logFrames = box;
		for (QPushButton *button : tab->findChildren<QPushButton *>())
			if (button->text() == QLatin1String("Clear")) monitor.clear = button;
		monitor.frames = tab->findChild<QPlainTextEdit *>();
		return monitor;
	}

	/* the hex bytes of the first frame sent in the Monitor's lines ("time  TX  bytes"), empty if none */
	static QString firstFrameSent(const QPlainTextEdit *frames) {
		const QLatin1String sent("  TX  ");
		for (const QString &line : frames->toPlainText().split(QLatin1Char('\n'))) {
			const int at = int(line.indexOf(sent));
			if (at >= 0) return line.mid(at + sent.size()).trimmed();
		}
		return {};
	}

	/* Disconnect, then Connect. In between, once the old link is silent, the Monitor's frames are
	 * cleared: the first frame it shows after that is the first request on the new link. */
	bool reconnect(const MonitorWidgets &monitor) {
		QPushButton *disconnectButton = buttonWithText(QStringLiteral("Disconnect"));
		if (disconnectButton) disconnectButton->click();
		QTest::qWait(300); /* the old link's last frames reach the Monitor */
		if (monitor.complete()) monitor.clear->click();
		QPushButton *connectButton = buttonWithText(QStringLiteral("Connect"));
		if (connectButton) connectButton->click();
		return disconnectButton && connectButton;
	}

	/* a wrong token in the token box: the device sees it and refuses it, the Log says so. The
	 * Monitor logs the frames meanwhile: the login is the first request after connecting. */
	void wrongTokenRefused() {
		QLineEdit *tokenBox = nullptr;
		for (QLineEdit *line : window_.findChildren<QLineEdit *>())
			if (line->placeholderText() == QLatin1String("Token (not stored)")) tokenBox = line;
		if (tokenBox) tokenBox->setText(QString::fromUtf8(wrongToken));
		const MonitorWidgets monitor = findMonitor();
		if (monitor.complete()) monitor.logFrames->setChecked(true);
		QTest::qWait(100); /* the engine starts logging frames */
		const bool reconnected = tokenBox && reconnect(monitor);
		const QByteArray want = loginBytes(wrongToken, map_.loginSize);
		const bool sawIt = reconnected && map_.loginAddr != 0
				&& QTest::qWaitFor([&] { return deviceLogin() == want; }, 3000);
		const bool logged = QTest::qWaitFor([&] { return logText().contains(tokenRefusedText); }, 3000);
		check(tokenBox && sawIt && logged,
				"a wrong token: the device received it and refused it, the Log says \"token refused: permission denied\"");

		/* the login frame as STUDIO.md section 3.6 gives it: WRITE with acknowledge of the whole register */
		const QString loginFrame = evre::hex(evre::build(map_.slave, evre::WRITE_ACK, map_.loginAddr,
				uint16_t(map_.loginSize), want));
		QString first;
		if (monitor.complete())
			(void) QTest::qWaitFor([&] { return !(first = firstFrameSent(monitor.frames)).isEmpty(); }, 3000);
		if (!first.isEmpty() && first != loginFrame)
			std::printf("  first frame sent: %s\n  the login frame:  %s\n", qPrintable(first), qPrintable(loginFrame));
		check(reconnected && map_.loginAddr != 0 && first == loginFrame,
				"the login is the first request after connecting (the Monitor's first frame sent)");
		if (monitor.complete()) {
			monitor.logFrames->setChecked(false);
			monitor.clear->click();
		}
	}

	/* the token set, but a map without "login": nothing is sent, the Log warns. The window
	 * starts again from this map (the same one without its login), connected. The other
	 * client first writes a marker to the login register (the fake device keeps it, though
	 * refused): a token sent all the same would replace it. */
	void tokenWithoutLoginRegister() {
		QTemporaryDir folder;
		DeviceMap withoutLogin = map_;
		withoutLogin.loginAddr = 0;
		const QString file = folder.filePath(QStringLiteral("without_login.json"));
		QString error;
		const bool saved = folder.isValid() && withoutLogin.save(file, error);
		loginSavedAndLoaded(folder, file);
		const QByteArray marker = loginBytes(noLoginMarker, map_.loginSize);
		if (map_.loginAddr != 0) (void) other_.write(map_.loginAddr, marker); /* refused, but kept */
		const bool marked = map_.loginAddr != 0 && deviceLogin() == marker;
		MainWindow::Startup startup;
		startup.tcp = QStringLiteral("127.0.0.1:%1").arg(FAKE_DEVICE_PORT);
		startup.map = file;
		startup.connect = true;
		if (saved) window_.applyStartup(startup); /* EVRE_TOKEN fills the token box again */
		const bool warned = saved && QTest::qWaitFor([&] { return logText().contains(noLoginRegisterText); }, 5000);
		const bool polled = cellShows(valueCell(table_, regs_.u8.name), QStringLiteral("0"), 5000);
		check(warned && polled && marked && deviceLogin() == marker,
				"a token with a map that declares no login register: the Log warns, nothing is written to the device");
	}

	/* the map file keeps "login" through a save and a load, and leaves it out when there is none */
	void loginSavedAndLoaded(const QTemporaryDir &folder, const QString &withoutLoginFile) {
		const QString withLoginFile = folder.filePath(QStringLiteral("with_login.json"));
		QString error;
		DeviceMap withLogin, withoutLogin;
		const bool kept = map_.loginAddr != 0 && map_.save(withLoginFile, error) && withLogin.load(withLoginFile, error)
				&& withLogin.loginAddr == map_.loginAddr && withLogin.loginSize == map_.loginSize;
		const bool leftOut = withoutLogin.load(withoutLoginFile, error) && withoutLogin.loginAddr == 0;
		check(kept && leftOut, "the map file: \"login\" saved and loaded again, left out when the map has none");
	}

	MainWindow &window_;
	OtherClient &other_;
	const DeviceMap map_;
	const TestRegisters regs_;
	const QString mapName_;
	QTableView *table_ = nullptr;
	RegisterModel *model_ = nullptr;
	QCheckBox *allowWrites_ = nullptr, *poll_ = nullptr;
	QModelIndex u8Cell_;
};

/* the last check: no Qt warning about objects used across threads came during the test or the teardown */
void checkThreadWarnings() {
	const int seen = threadWarnings.load();
	const QByteArray what = QStringLiteral("no Qt warnings about objects used across threads, the window's"
			" teardown included (%1 seen)").arg(seen).toUtf8();
	check(seen == 0, what.constData());
}

} // namespace

int main(int argc, char **argv) {
	previousHandler = qInstallMessageHandler(countThreadWarnings);
	QApplication app(argc, argv);
	QApplication::setOrganizationName(QStringLiteral("teknile"));
	QApplication::setApplicationName(QStringLiteral("EVReStudioTest")); /* not the user's settings */
	Theme::apply(app, true);

	const QString mapName = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("example_device.json");
	const QString mapPath = QCoreApplication::applicationDirPath() + QStringLiteral("/maps/") + mapName;
	DeviceMap map;
	QString error;
	if (!map.load(mapPath, error)) {
		std::printf("map %s: %s\n", qPrintable(mapPath), qPrintable(error));
		return 2;
	}
	prepareSettings(TestRegisters::find(map));

	OtherClient other;
	if (!other.open()) {
		std::printf("no fake device on 127.0.0.1:%u - start tests/fake_device.py first\n", FAKE_DEVICE_PORT);
		return 2;
	}
	/* the login register cleared (the fake device keeps even a refused token), then the token for the window */
	if (map.loginAddr != 0) other.write(map.loginAddr, QByteArray(map.loginSize, '\0'));
	qputenv("EVRE_TOKEN", fakeDeviceToken);

	{ /* the window and its I/O thread are gone before the last check: warnings at teardown count too */
		MainWindow window(mapPath); /* as main() does: the command line's map, opened once */
		window.show();
		MainWindow::Startup startup;
		startup.tcp = QStringLiteral("127.0.0.1:%1").arg(FAKE_DEVICE_PORT);
		startup.map = mapPath;
		startup.connect = true;
		window.applyStartup(startup);

		/* a dialog no step answered (it came after answerDialog stopped waiting): a failure that names it, and closed,
		 * never left on the screen waiting for a person */
		QTimer unanswered;
		QPointer<QWidget> modalSeen;
		QElapsedTimer modalFor;
		bool awaitedWhenSeen = false;
		QObject::connect(&unanswered, &QTimer::timeout, [&] {
			QWidget *modal = QApplication::activeModalWidget();
			if (modal != modalSeen) {
				modalSeen = modal;
				modalFor.restart();
				awaitedWhenSeen = AwaitingDialog::count > 0;
				return;
			}
			if (!modal || modalFor.elapsed() < UNANSWERED_DIALOG_MS) return;
			auto *box = qobject_cast<QMessageBox *>(modal);
			/* no step waiting: it came early (an editor committed on focus out) or after its step gave up */
			std::printf("FAIL a dialog no step answered in %d s (a step waiting for one when it came: %s), closed: "
					"\"%s\" %s\n", UNANSWERED_DIALOG_MS / 1000, awaitedWhenSeen ? "yes" : "no",
					qPrintable(modal->windowTitle()), box ? qPrintable(box->text()) : "");
			std::fflush(stdout);
			failed++;
			if (auto *dialog = qobject_cast<QDialog *>(modal)) dialog->reject();
			modalSeen = nullptr;
		});
		unanswered.start(500);

		GuiTest test(window, other, map, mapName);
		if (!test.run()) return 1;
	}
	QApplication::processEvents(); /* what the teardown posted */
	checkThreadWarnings();
	std::printf("\n%d passed, %d failed\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
