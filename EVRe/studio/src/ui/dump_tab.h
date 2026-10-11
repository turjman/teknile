/* SPDX-License-Identifier: Apache-2.0 */
/* The Dump tab: the bytes of every read, decoded by the map (DumpView paints them).
 *
 *   +------------------------------------------------------------------------------------+
 *   | Device [D1 v]   [Follow|Hold]   Colour [All registers|Plotted]  [x] Fields  [Copy v] |
 *   +------------------------------------------------------------------------------------+
 *   | Held at 14:03:12 . the cards show the bytes of that moment; ...  (only when held)  |
 *   +------------------------------------------------------------------------------------+
 *   | the cards: one per block read, in address order (DumpView)                         |
 *   +------------------------------------------------------------------------------------+
 *
 * The window hands the engine's replies over at the values' pace (applyDumps, only while the tab is shown and
 * following: wantsReplies) and calls tick() at each of those beats, so the replies' ages move with one repaint a
 * beat. Hold freezes the picture: the replies are not taken, the cards say "held" and get the amber stripe, and a
 * banner over them says so once for the whole tab (as it does when the link is closed: both are the whole tab's). The
 * manual READ typed in the Monitor shows as a card at the top until the next one; the fast streams switched on are a
 * note each (their records are on the Chart). On a bus the device chooser shows one device's cards at a time.
 *
 * The menu of a register or a card (a right-click on the view, and the same items in the Copy button, so nothing is
 * reachable only by a right-click): Show in Registers, Plot or Unplot, Write... (the Registers tab's editor, with
 * Allow writes on), and the four Copy forms. Colour (all registers or plotted only) is kept in "dump/colour", Fields
 * (the frames' fields named under their bytes, or each frame one line) in "dump/fields". */
#pragma once

#include <QWidget>

#include "ui/dump_view.h"
#include "ui/ui_helpers.h"

class ElidedLabel;
class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class RegisterModel;

class DumpTab : public QWidget {
	Q_OBJECT
public:
	explicit DumpTab(RegisterModel *model, QWidget *parent = nullptr);

	DumpView *view() const { return view_; }
	/* this tab is the one shown, or no longer (dumpWanted follows): hidden, it keeps no replies */
	void setShown(bool shown);
	bool shown() const { return shown_; }
	/* the replies are wanted now: the tab is shown and follows (not held) */
	bool wantsReplies() const { return shown_ && !view_->hold(); }
	/* the engine's blocks (every device's), as the window made them from IoEngine::DumpReply */
	void applyDumps(const QVector<DumpBlock> &blocks);
	/* the fast streams switched on: a note card each (DumpBlock::FastStream) */
	void setStreamNotes(const QVector<DumpBlock> &notes);
	/* the manual READ's answer from the Monitor: a card at the top, kept until the next one */
	void showManualRead(const DumpBlock &block);
	/* a beat of the values' pace: the ages move (one repaint) */
	void tick();
	void setFollow(bool follow); /* false: Hold */
	bool following() const { return !view_->hold(); }
	void setConnected(bool connected);

	/* a bus: its devices (fillDevicePicker), the cards of one at a time; none: one device, no chooser */
	void setDevices(const QVector<PickerDevice> &devices);
	void setSlave(int slave);  /* the device whose cards are shown (the selected one, as the Monitor's) */
	int slave() const { return slave_; }
	/* Show in dump: the register's device shown, its chip selected and scrolled into view */
	void showRegister(RegKey key);

	/* the menu of the register (NO_REGISTER: none) and card (-1: none): the right-click's and the Copy button's */
	void fillMenu(QMenu *menu, RegKey key, int card);
	QPushButton *copyButton() const { return copyButton_; }
	/* the banner over the cards: Hold or the link closed (the whole tab's reasons); empty and hidden when live */
	QString bannerText() const;

signals:
	void dumpWanted(bool on);                       /* the engine keeps the replies (IoEngine::setDump) */
	void slaveChosen(int slave);                    /* the chooser: another device's cards */
	void registerPicked(RegKey key, bool switchTab); /* a click (false), a double-click or Show in Registers (true) */
	void writeRequested(RegKey key);                /* Write...: the Registers tab's editor on it */

private:
	void compose();            /* the cards from the engine's blocks, the notes and the manual read */
	void updateEmptyText();
	void updateBanner();
	int rowOf(RegKey key) const; /* the register's row in the model; -1: none */

	RegisterModel *model_;
	DumpView *view_;
	QLabel *deviceLabel_;
	QComboBox *device_;
	QPushButton *followButton_, *holdButton_;
	QPushButton *allColoursButton_, *plottedColoursButton_;
	QPushButton *copyButton_;
	QCheckBox *fields_;
	ElidedLabel *banner_;
	QWidget *bannerBox_;  /* the banner and its line */
	QVector<DumpBlock> engineBlocks_, streamNotes_;
	DumpBlock manual_;
	bool hasManual_ = false;
	bool shown_ = false;
	bool connected_ = false;
	int slave_ = 0;
	bool bus_ = false;
};
