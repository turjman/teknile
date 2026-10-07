/* SPDX-License-Identifier: Apache-2.0 */
/* Small helpers the parts of the window share: how time lengths are written,
 * and builders of the widgets the theme styles by object name or property
 * (theme.h): cards, muted labels, segmented buttons. How addresses are written
 * is in model/device_map.h (addrText, parseAddress): the model and the API use it too. */
#pragma once

#include <QColor>
#include <QString>
#include <QVector>

class QComboBox;
class QFont;
class QIcon;
class QMenu;
class QLabel;
class QLayout;
class QPushButton;
class QWidget;

/* --------------------------------------------------------------------- text */

/* a time length as the chart's Window and Memory boxes show it: "50 µs", "500 ms", "30 s", "2 min", "1.5 h" */
QString secondsText(double seconds);

/* a span of time as read off the chart: "123 µs", "3.525 ms", "12.35 s" (4 significant digits below a minute),
 * "1 min 23.4 s", "2 h 05 min"; the sign is dropped */
QString durationText(double seconds);

/* a time length as typed: "45" (seconds), "2.5 s", "500 ms", "50 us" (or µs), "3 min", "1 h"; <= 0 if it is not one */
double parseSeconds(const QString &text);

/* text from the map on a button, a check box or a menu: "&" would mark a
 * keyboard shortcut there ("Inputs & outputs" showed as "Inputs _outputs") */
QString noMnemonic(QString text);

/* rich text in a colour: <span style='color:..'>html</span> */
QString coloredSpan(const QString &html, const QColor &color);

/* the maps beside the program: where Open and Save start, and where a first start looks */
QString mapsFolder();

/* ------------------------------------------------------------------ widgets */

/* a card of the sidebar: its title in capitals over the content */
QWidget *card(const QString &title, QLayout *content);

/* secondary text: hints, units, the names in front of boxes */
QLabel *mutedLabel(const QString &text);

/* one checkable part of a segmented choice; position: "first" or "last" (the rounded ends) */
QPushButton *segmentButton(const QString &text, const char *position);

/* a button that opens a menu: the menu, and the theme's chevron for it (theme: QPushButton[menuButton]),
 * the same arrow as the combo boxes' (not Fusion's triangle). Every menu button is made this way. */
void setButtonMenu(QPushButton *button, QMenu *menu);

/* a warning with Cancel (the default) and one button that goes ahead: true when that one was clicked */
bool confirmed(QWidget *parent, const QString &title, const QString &html, const QString &goAhead);

/* a modal dialog over the window without the compositor's (DWM's) open and close animations on Windows: closed with
 * its title bar's X, a dialog fades out while the window's live chart waits behind it (STUDIO.md 27). It also marks
 * the dialog (the property "noAnimation", for the tests); nothing else on Linux. Called where the dialog is made. */
void noWindowAnimation(QWidget *dialog);

/* the style sheet again, after a change of the object name or of a property it selects on */
void repolish(QWidget *widget);

/* a switch that lets something risky happen (writes): in bold and in colour while it is on; always as wide as in
 * bold, so the row it is in does not move when it turns on. Call it once (off) when the switch is made: the first
 * call reserves the bold width, and made at the first tick the row would move that once. */
void setHighlighted(QWidget *widget, bool on, const QColor &color);

/* the monospace font of the frame monitor and the event log */
QFont monospaceFont();

/* a pause (two bars) or play (a triangle) icon, both the same size and weight: the font's
 * glyphs for them are not (a big pause beside a small play) */
enum class MediaIcon { Pause, Play };
QIcon mediaIcon(MediaIcon kind, const QColor &color, int size = 14);

/* a warning sign: a triangle with an exclamation mark cut out of it (a tab's icon: the page is not for this) */
QIcon warningIcon(const QColor &color, int size = 14);

/* "look again": a circular arrow, drawn (a font's arrow glyph comes out small and differs from font to font) */
QIcon refreshIcon(const QColor &color, int size = 18);

/* ------------------------------------------------------------ device pickers */

/* a state's dot: a filled circle in its colour, sharp on any screen (beside a device's name) */
QIcon stateDot(const QColor &color, qreal ratio);

/* The Studio's icon, the teknile mark (packaging/icons), in every size the resources hold (16 to 256 px): every
 * window's, set for the whole application by the main window */
QIcon studioIcon();

/* A row of a device picker: a device of a bus (its name, its slave, its state as the dot's colour and in words), or
 * a choice of another kind ("All devices": no dot, the name alone). The row's data is the slave. */
struct PickerDevice {
	QString name;
	int slave = 0;
	QColor dot;    /* invalid: not a device */
	QString state; /* the row's tooltip */
};

/* The rows in a device picker, one look everywhere (the Registers tab, the Map editor, the Monitor, as in the Devices
 * card): the state's dot, then "D1 · slave 1". Rows already there are changed in place, so a list that is open when
 * a device goes offline stays open. current: the data of the row shown. The box has one width whatever the names
 * (devices added or renamed move nothing beside it): a long name is cut ("..."), the row's tooltip has it whole. */
void fillDevicePicker(QComboBox *box, const QVector<PickerDevice> &devices, int current);
