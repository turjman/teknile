/* SPDX-License-Identifier: Apache-2.0 */
/* The look: Fusion style, a dark (default) and a light palette, one accent
 * colour, rounded cards. Widgets opt in by object name or property (see theme.cpp):
 *   #sidebar, #sideScroll, #card, #cardTitle, #appTitle, #appSub, #muted (and the Chart tab's #chartInfo, #measureInfo),
 *   QListWidget #helpTopics (the Help's list: a card), QScrollArea #formScroll (a form that scrolls: no frame),
 *   QLabel #editorEmpty (the Map editor's note when no register is selected),
 *   QLabel [chip="true"] (a small framed tag: the Map editor's address, type, access),
 *   QLabel #liveDot with [state="ok" / "warn" / "none"] (the Map editor's live value),
 *   QPushButton #primary, #danger, #hold[live="false"] (and [stopped="true"]: the trigger stopped, in amber),
 *   QPushButton[menuButton="true"] (a button that opens a menu: room for the chevron, see setButtonMenu),
 *   QPushButton[segment="true"] with [segmentPos="first" / "last"] for segmented choices,
 *   QLabel #pill with [state="idle" / "busy" / "ok" / "error"],
 *   QLabel #problem (what is wrong in a dialog, in red), QLabel #statusSlow (the status bar's hint, in amber),
 *   QLabel #ramNeed with [warn="true"] (the Chart tab's memory note: muted, amber when more than the RAM),
 *   QLabel #menuTitle (a title inside a menu, muted: the style draws no section text),
 *   QListWidget #busDevices (the Devices card's list).
 * Every widget given the focus from the keyboard (Tab, Shift+Tab, a shortcut) has
 * [keyFocus="true"] until it loses it, and the theme draws a focus ring by it; a
 * click gives none (as a browser's :focus-visible).
 *
 * The colours meet WCAG 2.1 AA in both looks: text 4.5:1 on what it sits on (the
 * white text of a filled button too: accentFill, badFill), and the edge of a box,
 * a check box or a focus ring 3:1 (control). */
#pragma once

#include <QColor>
#include <QString>
#include <QVector>

class QApplication;
class QEvent;

struct ThemeColors {
	QColor bg;                /* the window */
	QColor surface;           /* panels: the sidebar, tables, the status bar */
	QColor surface2;          /* raised on a panel: cards, buttons, menus, tooltips */
	QColor border;
	QColor text;
	QColor muted;             /* secondary text, disabled things */
	QColor accent;            /* selection, focus, checked, links */
	QColor accentFill;        /* behind white text: the primary button, a checked segment */
	QColor badFill;           /* behind white text: the danger button (Disconnect) */
	QColor control;           /* the edge of an input box or a check box (3:1 to what is around it) */
	QColor good, warn, bad;   /* states: ok, busy or a warning, an error (as text: 4.5:1) */
	QColor grid;              /* chart grid lines */
	QVector<QColor> series;   /* chart lines, in the order they are handed out */
};

namespace Theme {
void apply(QApplication &app, bool dark); /* style, palette, font, style sheet, no popup animations; again to switch */
const ThemeColors &colors();              /* of the theme applied last */
bool isDark();
/* For a widget that writes the theme's colours into what it shows (a log line, a cell, a highlight): true when
 * `event` (its changeEvent) is the palette change of a switch to the other look, so it shows them again.
 * drawnDark: the look it drew in last, updated. A palette change for any other reason gives false. */
bool switched(const QEvent *event, bool &drawnDark);
}
