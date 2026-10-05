/* SPDX-License-Identifier: Apache-2.0 */
/* The event log: what happened (connecting, writes, read errors, ...), for
 * the user now and for later.
 *
 * EventLog is the Log tab. Every event goes to logs/studio_<yyyyMMdd>.log
 * beside the program (in the user's data folder when that is not writable),
 * a new file each day, and to the tab, coloured by level. The same event
 * again and again (a reconnect every 2 s) is counted, not repeated. The tab
 * counts the warnings and errors that came while it was not shown, for its
 * title, and hands them to the Notice when Pop-ups is ticked.
 *
 * Notice is the pop-up: one line in the free space right of the tab bar, so
 * it never covers any tab's content, gone after 5 s (an error: 8 s). */
#pragma once

#include <QHash>
#include <QLabel>
#include <QVector>
#include <QWidget>
#include <memory>

class DailyLogFile;
class QCheckBox;
class QDateTime;
class QPlainTextEdit;
class QTabWidget;
class QTimer;

enum class LogLevel { Info, Warning, Error };

class EventLog : public QWidget {
	Q_OBJECT
public:
	explicit EventLog(QWidget *parent = nullptr);
	~EventLog() override;

	/* the tab is the one shown (or no longer): shown, it has no warnings left unseen */
	void setShown(bool shown);

public slots:
	void add(LogLevel level, const QString &text);

signals:
	/* the warnings and errors logged since the tab was last shown */
	void unseenChanged(int count);
	/* a warning or an error, Pop-ups ticked: for the Notice */
	void popUp(LogLevel level, const QString &text);

protected:
	void changeEvent(QEvent *event) override; /* the theme changed: every line again, in its colours */

private:
	/* a line as the tab shows it: its colour comes from its level when it is drawn, not when it was logged */
	struct Line {
		LogLevel level;
		bool repeat;      /* "… the line above N more time(s)": muted */
		QString text;
	};
	void writeToFile(const QDateTime &now, const QStringList &lines);
	void showInTab(LogLevel level, const QStringList &lines);
	void appendLine(const Line &line);
	void redraw();
	void openFolder();

	std::unique_ptr<DailyLogFile> file_;
	QString lastEvent_;       /* level and text of the last line, to count repeats */
	int repeats_ = 0;         /* of the last line, not written yet */
	bool shown_ = false;
	int unseen_ = 0;

	QLabel *fileInfo_;        /* where the file is */
	QCheckBox *showInfo_, *popUps_;
	QPlainTextEdit *view_ = nullptr;
	QVector<Line> lines_;     /* what the tab shows (MAX_LINES at most), to draw it again in another theme */
	bool drawnDark_ = true;   /* the look the lines are drawn in */
};

/* No spam: the same message (digits ignored) pops up at most every 30 s; one
 * notice at a time, the newest, with "+N more" for the others while it is
 * shown. Its full text is in its tooltip and in the Log; "Show in Log" goes
 * there and ends the notice. Too narrow beside the tabs: the status bar shows
 * it instead. */
class Notice : public QLabel {
	Q_OBJECT
public:
	/* it sits in the row of the tabs' tab bar; window: its parent, over everything */
	Notice(QTabWidget *tabs, QWidget *window);

	void post(LogLevel level, const QString &text);
	void place(); /* again after the window changed size */

signals:
	void showLogClicked();
	void noRoom(const QString &message, int ms); /* for the status bar, for ms */

private:
	void dismiss(); /* gone until the next message: its time is up, or Show in Log was clicked */

	QTabWidget *tabs_;
	QTimer *hideTimer_;               /* runs while it is up, shown or (no room) on the status bar */
	LogLevel level_ = LogLevel::Info;
	QString text_;
	int more_ = 0;                  /* messages posted while it was shown */
	QHash<QString, qint64> posted_; /* per message (digits ignored): when it last popped up (ms) */
};
