/* SPDX-License-Identifier: Apache-2.0 */
/* The sidebar's place in the window: the sidebar itself, or folded to a thin bar that leaves the tabs the width.
 *
 *  - SidebarRail, the thin bar (RAIL_W): at its top the open button (a FoldButton, the fold button's shape pointing
 *    away from the window's edge), under it the link's dot in the pill's colours (its tooltip: the pill's text and
 *    the poll rate), then an icon for each card in the sidebar's order (Fast streams only while that card is there),
 *    and Help and Language at its foot. A card's icon opens the sidebar at that card (Sidebar::showCard); Help opens
 *    the Help as F1 does and leaves the sidebar folded. Every icon has its tooltip, a lit look under the mouse and the
 *    pointing hand.
 *  - SidebarDock holds the two and slides between them (SLIDE_MS): the sidebar moves out over the window's edge while
 *    the bar comes out from under it, the tabs taking the width as it goes. The button of the side arriving (the
 *    sidebar's fold button, the bar's open button) is hidden while it slides and fades in (FADE_MS) once it is there:
 *    a button that shows before its sidebar has arrived looks lost. No slide when the system asks for less motion
 *    (reducedMotion, ui_helpers.h): it is at its end at once. Folded or not is saved at once ("ui/sidebarFolded")
 *    and restored without a slide; a recording's window has no sidebar.
 *
 * Folding only hides: the link, the polls, a recording, the fast streams and the API go on, every control of the
 * sidebar still there; the window and the tests read them as before. The keyboard's Ctrl+B (the window's) toggles. */
#pragma once

#include <QWidget>

#include "ui/sidebar.h"

class FoldButton;
class QAbstractButton;
class QVariantAnimation;

class SidebarRail : public QWidget {
	Q_OBJECT
public:
	explicit SidebarRail(QWidget *parent = nullptr);

	FoldButton *openButton() const { return open_; }
	QAbstractButton *cardButton(Sidebar::Card card) const { return cards_[int(card)]; } /* Language included */
	QAbstractButton *helpButton() const { return help_; }
	QWidget *dot() const { return dot_; }
	/* the pill's state ("idle", "busy", "ok", "listen", "error"), its text and the poll rate's line */
	void showLink(const QString &state, const QString &text, const QString &rate);
	void setCardShown(Sidebar::Card card, bool shown);

signals:
	void openClicked();
	void cardClicked(Sidebar::Card card);
	void helpClicked();

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	FoldButton *open_;
	QWidget *dot_;
	QAbstractButton *cards_[7] = {};
	QAbstractButton *help_;
};

class SidebarDock : public QWidget {
	Q_OBJECT
public:
	static constexpr int RAIL_W = 40, SLIDE_MS = 220, FADE_MS = 160;
	/* the sidebar becomes the dock's child; folded as saved */
	explicit SidebarDock(Sidebar *sidebar, QWidget *parent = nullptr);

	Sidebar *sidebar() const { return sidebar_; }
	SidebarRail *rail() const { return rail_; }
	bool folded() const { return folded_; }
	bool sliding() const;
	/* folded or open, sliding there (or at once with less motion, or while the window is not shown); saved */
	void setFolded(bool folded);
	void toggle() { setFolded(!folded_); }
	/* the sidebar opened (if folded) with that card in view and lit */
	void openAt(Sidebar::Card card);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	void helpClicked();             /* the bar's Help */
	void foldedChanged(bool folded);

protected:
	void resizeEvent(QResizeEvent *event) override;
	void changeEvent(QEvent *event) override;

private:
	int widthAt(double progress) const;
	void setProgress(double progress); /* 0 open .. 1 folded: the width and the two children's places */
	void place();
	void arrived(bool fade);           /* at the end: the side left hidden, the arriving side's button shown */

	Sidebar *sidebar_;
	SidebarRail *rail_;
	QVariantAnimation *slide_, *fade_;
	FoldButton *fading_ = nullptr;     /* the button fading in */
	double progress_ = 0;
	bool folded_ = false;
	int refocus_ = 0;                  /* the focus was on the side leaving: 1 from the mouse, 2 from the keyboard */
};
