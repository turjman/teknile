/* SPDX-License-Identifier: Apache-2.0 */
/* The event log, its file and the pop-up notice: see event_log.h. */
#include "ui/event_log.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

constexpr int MAX_LINES = 5000;          /* the tab keeps this many; the file keeps everything */
constexpr qint64 SAME_NOTICE_MS = 30000; /* the same message pops up at most this often */
constexpr int WARNING_SHOWN_MS = 5000;
constexpr int ERROR_SHOWN_MS = 8000;
constexpr int NOTICE_MIN_WIDTH = 260;    /* less room beside the tabs: the status bar instead */

/* the level in the file and the tab, all as wide */
const char *levelTag(LogLevel level) {
	return level == LogLevel::Error ? "ERROR" : level == LogLevel::Warning ? "WARN " : "INFO ";
}

} // namespace

/* ----------------------------------------------------------------- the file */

/* logs/studio_<yyyyMMdd>.log, beside the program or, when that folder is not
 * writable, in the user's data folder */
class DailyLogFile {
public:
	/* the file of this day: opened with the first line and again when the day changes (true then) */
	bool openFor(const QDateTime &now) {
		const QString day = now.toString(QStringLiteral("yyyyMMdd"));
		if (file_ && day == day_) return false;
		day_ = day;
		file_ = openIn(QCoreApplication::applicationDirPath() + QStringLiteral("/logs"));
		if (!file_) {
			file_ = openIn(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
					+ QStringLiteral("/logs"));
		}
		return true;
	}

	void write(const QStringList &lines) {
		if (!file_) return;
		for (const QString &line : lines) file_->write(line.toUtf8() + '\n');
		file_->flush();
	}

	QString path() const { return file_ ? file_->fileName() : QString(); } /* empty: not saved */

private:
	std::unique_ptr<QFile> openIn(const QString &folder) const {
		auto file = std::make_unique<QFile>(folder + QStringLiteral("/studio_%1.log").arg(day_));
		if (!QDir().mkpath(folder) || !file->open(QIODevice::Append | QIODevice::Text)) return nullptr;
		return file;
	}

	std::unique_ptr<QFile> file_; /* null: no writable folder (tried again with the next line) */
	QString day_;
};

/* -------------------------------------------------------------- the Log tab */

/* a bar (where the file is, Pop-ups, Show info, Open folder, Clear) over the lines */
EventLog::EventLog(QWidget *parent) : QWidget(parent), file_(std::make_unique<DailyLogFile>()) {
	drawnDark_ = Theme::isDark();
	fileInfo_ = mutedLabel(QString());
	fileInfo_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	fileInfo_->setWordWrap(true);
	showInfo_ = new QCheckBox(tr("Show info"));
	showInfo_->setChecked(true);
	showInfo_->setToolTip(tr("Off: only warnings and errors are shown here (the file keeps everything)"));
	popUps_ = new QCheckBox(tr("Pop-ups"));
	popUps_->setChecked(QSettings().value(QStringLiteral("ui/popups"), true).toBool());
	popUps_->setToolTip(tr("Warnings and errors also pop up in the corner of the window for a few seconds"));
	connect(popUps_, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue(QStringLiteral("ui/popups"), on); });
	auto *openFolderButton = new QPushButton(tr("Open folder"));
	auto *clear = new QPushButton(tr("Clear"));
	auto *bar = new QHBoxLayout;
	bar->addWidget(fileInfo_, 1);
	bar->addWidget(popUps_);
	bar->addWidget(showInfo_);
	bar->addWidget(openFolderButton);
	bar->addWidget(clear);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 12, 0, 0);
	layout->setSpacing(10);
	layout->addLayout(bar);
	view_ = new QPlainTextEdit;
	view_->setObjectName(QStringLiteral("eventLog"));
	view_->setReadOnly(true);
	view_->setMaximumBlockCount(MAX_LINES);
	view_->setFont(monospaceFont());
	/* one line per event, its columns under each other: a long path scrolls, it does not wrap mid-path */
	view_->setLineWrapMode(QPlainTextEdit::NoWrap);
	layout->addWidget(view_, 1);
	connect(clear, &QPushButton::clicked, this, [this] {
		view_->clear();
		lines_.clear();
	});
	connect(openFolderButton, &QPushButton::clicked, this, &EventLog::openFolder);
}

EventLog::~EventLog() = default; /* here, where DailyLogFile is complete */

void EventLog::setShown(bool shown) {
	shown_ = shown;
	if (!shown) return;
	unseen_ = 0;
	emit unseenChanged(0);
}

void EventLog::add(LogLevel level, const QString &text) {
	const QDateTime now = QDateTime::currentDateTime();
	const QLatin1String tag(levelTag(level));
	/* the same event again and again (a reconnect every 2 s): counted, not repeated */
	const QString event = tag + text;
	if (event == lastEvent_) {
		repeats_++;
		return;
	}
	QStringList lines;
	if (repeats_ > 0) lines << tr("        … the line above %1 more time(s)").arg(repeats_);
	repeats_ = 0;
	lastEvent_ = event;
	lines << now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")) + QLatin1String("  ") + tag + QLatin1String("  ")
					+ text;
	writeToFile(now, lines);

	if (level == LogLevel::Info && !showInfo_->isChecked()) return;
	showInTab(level, lines);
	if (level == LogLevel::Info) return;
	if (!shown_) emit unseenChanged(++unseen_);
	if (popUps_->isChecked()) emit popUp(level, text);
}

void EventLog::writeToFile(const QDateTime &now, const QStringList &lines) {
	if (file_->openFor(now)) {
		const QString path = file_->path();
		fileInfo_->setText(path.isEmpty() ? tr("Not saved to a file (no writable folder)")
										  : tr("Also saved to %1").arg(QDir::toNativeSeparators(path)));
	}
	file_->write(lines);
}

/* the new line in the colour of its level; the repeat count before it muted */
void EventLog::showInTab(LogLevel level, const QStringList &lines) {
	for (int i = 0; i < lines.size(); i++) {
		const Line line{ level, i < lines.size() - 1, lines[i] };
		lines_.append(line);
		appendLine(line);
	}
	if (lines_.size() > MAX_LINES) lines_.remove(0, lines_.size() - MAX_LINES); /* as the view drops them */
}

/* in the colours of the theme now: a line logged in the dark one stays readable in the light one */
void EventLog::appendLine(const Line &line) {
	const ThemeColors &c = Theme::colors();
	const QColor color = line.repeat ? c.muted
			: line.level == LogLevel::Error ? c.bad : line.level == LogLevel::Warning ? c.warn : c.text;
	view_->appendHtml(QStringLiteral("<span style='color:%1; white-space:pre'>%2</span>")
							  .arg(color.name(), line.text.toHtmlEscaped()));
}

void EventLog::redraw() {
	view_->setUpdatesEnabled(false);
	view_->clear();
	for (const Line &line : std::as_const(lines_)) appendLine(line);
	view_->setUpdatesEnabled(true);
}

/* a palette change comes for other reasons too: drawn again only when the look is another one */
void EventLog::changeEvent(QEvent *event) {
	QWidget::changeEvent(event);
	if (view_ && Theme::switched(event, drawnDark_)) redraw();
}

void EventLog::openFolder() {
	const QString path = file_->path();
	if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

/* --------------------------------------------------------------- the pop-up */

Notice::Notice(QTabWidget *tabs, QWidget *window) : QLabel(window), tabs_(tabs), hideTimer_(new QTimer(this)) {
	setObjectName(QStringLiteral("notice"));
	setTextFormat(Qt::RichText);
	setCursor(Qt::PointingHandCursor);
	hide(); /* until the first message, also once the window is shown */
	connect(this, &QLabel::linkActivated, this, [this] {
		emit showLogClicked();
		dismiss(); /* read: a resize must not bring it back */
	});
	hideTimer_->setSingleShot(true);
	connect(hideTimer_, &QTimer::timeout, this, &Notice::dismiss);
}

void Notice::dismiss() {
	hideTimer_->stop();
	text_.clear();
	more_ = 0;
	hide();
}

void Notice::post(LogLevel level, const QString &text) {
	static const QRegularExpression digits(QStringLiteral("[0-9]+"));
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const QString key = QString(text).remove(digits);
	const auto last = posted_.constFind(key);
	if (last != posted_.constEnd() && now - last.value() < SAME_NOTICE_MS) return;
	posted_[key] = now;
	more_ = isVisible() ? more_ + 1 : 0;
	if (!isVisible() || level == LogLevel::Error || level_ != LogLevel::Error) level_ = level; /* an error stays red */
	text_ = text;
	hideTimer_->start(level_ == LogLevel::Error ? ERROR_SHOWN_MS : WARNING_SHOWN_MS);
	place();
}

/* In the tab bar's row, right of the tabs: nothing of any tab is there, so it
 * covers nothing, whatever the tab and the window size. One line, elided. */
void Notice::place() {
	if (text_.isEmpty() || !hideTimer_->isActive()) return;
	QWidget *window = parentWidget();
	const QTabBar *bar = tabs_->tabBar();
	const QPoint barEnd = bar->mapTo(window, QPoint(bar->width(), 0));
	const int left = barEnd.x() + 20;
	const int right = tabs_->mapTo(window, QPoint(tabs_->width(), 0)).x();
	const int rowHeight = bar->height();

	const ThemeColors &c = Theme::colors();
	const QColor edge = level_ == LogLevel::Error ? c.bad : c.warn;
	const QString tag = level_ == LogLevel::Error ? tr("Error") : tr("Warning");
	const QString more = more_ > 0 ? tr("  +%1 more").arg(more_) : QString();
	const QString link = tr("Show in Log");
	setToolTip(text_);
	if (right - left < NOTICE_MIN_WIDTH) {
		hide();
		emit noRoom(QStringLiteral("%1: %2%3").arg(tag, text_, more), hideTimer_->remainingTime());
		return;
	}

	/* the message gets the room the tag, the count and the link leave */
	QFont bold = font();
	bold.setBold(true);
	const QFontMetrics plainMetrics(font()), boldMetrics(bold);
	const int padding = 2 * 12 + 4 + 2;
	const int fixedWidth = boldMetrics.horizontalAdvance(tag + QStringLiteral("   "))
			+ plainMetrics.horizontalAdvance(more + QStringLiteral("    ") + link) + padding;
	const QString shown = plainMetrics.elidedText(text_, Qt::ElideRight, std::max(60, right - left - fixedWidth));
	setText(QStringLiteral("<b style='color:%1'>%2</b>&nbsp; %3<span style='color:%4'>%5</span> &nbsp;"
						   "<a href='log' style='color:%6'>%7</a>")
					.arg(edge.name(), tag, shown.toHtmlEscaped(), c.muted.name(), more.toHtmlEscaped(),
							c.accent.name(), link));
	setStyleSheet(QStringLiteral("QLabel#notice { background:%1; color:%2; border:1px solid %3;"
								 " border-left:4px solid %4; border-radius:7px; padding:0px 12px; }")
						  .arg(c.surface2.name(), c.text.name(), c.border.name(), edge.name()));
	const int height = std::max(24, std::min(rowHeight - 6, plainMetrics.height() + 12));
	const int width = std::min(right - left, sizeHint().width());
	setGeometry(right - width, barEnd.y() + (rowHeight - height) / 2, width, height);
	show();
	raise();
}
