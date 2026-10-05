/* SPDX-License-Identifier: Apache-2.0 */
/* The theme: the colours of the dark and the light look, and applying one to the
 * whole application (Fusion style, a palette for what Qt draws natively, the font,
 * and one style sheet filled in with the colours). */
#include "ui/theme.h"

#include <QApplication>
#include <QDir>
#include <QFocusEvent>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>
#include <utility>

namespace {

/* the theme applied last (Theme::apply), for colors() and isDark() */
ThemeColors g_colors;
bool g_dark = true;

ThemeColors darkColors() {
	ThemeColors c;
	c.bg = QColor(0x0F, 0x12, 0x17);
	c.surface = QColor(0x17, 0x1B, 0x22);
	c.surface2 = QColor(0x1E, 0x23, 0x2C);
	c.border = QColor(0x2A, 0x31, 0x3C);
	c.text = QColor(0xE6, 0xE9, 0xEF);
	c.muted = QColor(0x8B, 0x93, 0xA1);
	c.accent = QColor(0x4F, 0x8C, 0xFF);
	c.accentFill = QColor(0x25, 0x63, 0xEB); /* white on it 5.2:1 (on accent: 3.2) */
	c.badFill = QColor(0xDC, 0x26, 0x26);    /* white on it 4.8:1 (on bad: 3.0) */
	c.control = QColor(0x6B, 0x75, 0x86);    /* 3.4:1 or more to the window, panels and cards */
	c.good = QColor(0x3E, 0xCF, 0x8E);
	c.warn = QColor(0xFF, 0xB0, 0x20);
	c.bad = QColor(0xFF, 0x5C, 0x5C);
	c.grid = QColor(0x24, 0x2A, 0x33);
	c.series = { QColor(0x4F, 0x8C, 0xFF), QColor(0x3E, 0xCF, 0x8E), QColor(0xFF, 0xB0, 0x20),
		QColor(0xFF, 0x6B, 0x9A), QColor(0xA7, 0x8B, 0xFA), QColor(0x2D, 0xD4, 0xD4),
		QColor(0xF9, 0x73, 0x16), QColor(0xE8, 0xE8, 0x5A) };
	return c;
}

ThemeColors lightColors() {
	ThemeColors c;
	c.bg = QColor(0xF3, 0xF5, 0xF8);
	c.surface = QColor(0xFF, 0xFF, 0xFF);
	c.surface2 = QColor(0xEE, 0xF1, 0xF5);
	c.border = QColor(0xD9, 0xDE, 0xE5);
	c.text = QColor(0x17, 0x1B, 0x22);
	c.muted = QColor(0x64, 0x6D, 0x7A);
	c.accent = QColor(0x25, 0x63, 0xEB);
	c.accentFill = c.accent;                 /* white on it 5.2:1 */
	c.badFill = QColor(0xC8, 0x1E, 0x1E);    /* white on it 5.7:1 */
	c.control = QColor(0x7F, 0x89, 0x98);    /* 3.1:1 or more to the window, panels and cards */
	/* as text, 4.5:1 or more on white, the window and their tints (the ones of D9 77 06 and 16 A3 4A were 3.2) */
	c.good = QColor(0x13, 0x77, 0x36);
	c.warn = QColor(0x9A, 0x4F, 0x06);
	c.bad = QColor(0xC8, 0x1E, 0x1E);
	c.grid = QColor(0xE6, 0xE9, 0xEE);
	c.series = { QColor(0x25, 0x63, 0xEB), QColor(0x16, 0xA3, 0x4A), QColor(0xD9, 0x77, 0x06),
		QColor(0xDB, 0x27, 0x77), QColor(0x7C, 0x3A, 0xED), QColor(0x08, 0x91, 0xB2),
		QColor(0xEA, 0x58, 0x0C), QColor(0x65, 0xA3, 0x0D) };
	return c;
}

QString cssColor(const QColor &c) { return c.name(QColor::HexRgb); }

/* A style sheet shows an image only from a file: the theme's small drawings (a line in the given colour, at 4x so
 * they stay sharp at any display scaling) go once into the cache folder, one file per drawing and colour. Empty:
 * it could not be written, and the style sheet goes without that image. */
QString drawnImageFile(const char *name, const QColor &color, QSize size, const QPolygonF &line, qreal width) {
	QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
	if (dir.isEmpty() || !QDir().mkpath(dir)) dir = QDir::tempPath();
	const QString file = QDir(dir).filePath(QStringLiteral("%1-%2.png").arg(QLatin1String(name), color.name().mid(1)));
	if (!QFileInfo::exists(file)) {
		QImage image(size, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter p(&image);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawPolyline(line);
		p.end();
		if (!image.save(file, "PNG")) return QString();
	}
	return QDir::fromNativeSeparators(file);
}

/* The arrow of every drop-down: the combo boxes', and a menu button's (a QPushButton with a menu, see
 * setButtonMenu) in place of Fusion's triangle. Once the style sheet styles their drop-down, Qt draws no arrow of
 * its own: a small chevron, 40 x 24 shown at 10 x 6. */
QString comboArrowFile(const QColor &color) {
	return drawnImageFile("combo-arrow", color, QSize(40, 24),
			QPolygonF({ QPointF(8, 6), QPointF(20, 18), QPointF(32, 6) }), 4.5);
}

/* A check box ticked: a tick, not only the fill (the state is not told by colour alone); partly (registers that
 * differ): a dash. 64 x 64 shown at 16 x 16. */
QString checkTickFile() {
	return drawnImageFile("check-tick", Qt::white, QSize(64, 64),
			QPolygonF({ QPointF(16, 33), QPointF(27, 44), QPointF(48, 21) }), 8);
}
/* The choice ticked in a menu of choices (one of them: Drawing): a tick in the accent colour, no box. A menu's
 * check mark was Fusion's, drawn small and blurred at 225 %. */
QString menuTickFile(const QColor &color) {
	return drawnImageFile("menu-tick", color, QSize(64, 64),
			QPolygonF({ QPointF(12, 34), QPointF(26, 48), QPointF(52, 18) }), 8);
}
QString checkDashFile(const QColor &color) {
	return drawnImageFile("check-dash", color, QSize(64, 64), QPolygonF({ QPointF(18, 32), QPointF(46, 32) }), 8);
}

/* [keyFocus="true"] on the widget that has the focus from the keyboard, for the focus ring (theme.h) */
class KeyFocusMarker : public QObject {
public:
	using QObject::QObject;
	bool eventFilter(QObject *watched, QEvent *event) override {
		const QEvent::Type type = event->type();
		if ((type == QEvent::FocusIn || type == QEvent::FocusOut) && watched->isWidgetType()) {
			const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
			const bool keyboard = type == QEvent::FocusIn
					&& (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason
							|| reason == Qt::ShortcutFocusReason);
			auto *widget = static_cast<QWidget *>(watched);
			if (widget->property("keyFocus").toBool() != keyboard) {
				widget->setProperty("keyFocus", keyboard);
				widget->style()->unpolish(widget);
				widget->style()->polish(widget);
				widget->update();
			}
		}
		return QObject::eventFilter(watched, event);
	}
};

/* alpha 0 to 255: the tints behind selections and state pills */
QString cssColorWithAlpha(const QColor &c, int alpha) {
	return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

/* The style sheet, with %NAME% placeholders for the colours (filled in by styleSheet()).
 * Later rules win over earlier ones of the same weight, so the order matters. */
QString styleSheetTemplate() {
	return QStringLiteral(
		/* every widget, and tooltips */
		R"(
* { outline: none; }
QMainWindow, QDialog { background: %BG%; }
QWidget { color: %TEXT%; font-size: 10pt; }
QToolTip { background: %S2%; color: %TEXT%; border: 1px solid %BORDER%; border-radius: 6px; padding: 6px; }
)"
		/* panels and labels that opt in by object name */
		R"(
#sidebar { background: %S1%; }
QScrollArea#sideScroll { background: %S1%; border: none; border-right: 1px solid %BORDER%; }
QScrollArea#formScroll, QScrollArea#formScroll > QWidget > QWidget { background: transparent; }
QTextBrowser { background: %S1%; border: 1px solid %BORDER%; border-radius: 10px; padding: 8px; }
QListWidget#helpTopics { background: %S1%; border: 1px solid %BORDER%; border-radius: 10px; padding: 6px; }
QListWidget#helpTopics::item { padding: 6px 10px; border-radius: 6px; color: %TEXT%; }
QListWidget#helpTopics::item:hover { background: %S2%; }
QListWidget#helpTopics::item:selected { background: %ACCENTA%; color: %TEXT%; }
QListWidget#busDevices { background: %S1%; border: 1px solid %BORDER%; border-radius: 8px; padding: 3px; }
QListWidget#busDevices::item { padding: 2px 6px; border-radius: 5px; color: %TEXT%; }
QListWidget#busDevices::item:hover { background: %S2%; }
QListWidget#busDevices::item:selected { background: %ACCENTA%; color: %TEXT%; }
#appTitle { font-size: 15pt; font-weight: 700; }
#appSub { color: %MUTED%; font-size: 9pt; }
#card { background: %S2%; border: 1px solid %BORDER%; border-radius: 10px; }
#cardTitle { color: %MUTED%; font-size: 8pt; font-weight: 700; letter-spacing: 1px; }
#muted, #chartInfo, #measureInfo { color: %MUTED%; }
QLabel#ramNeed { color: %MUTED%; }
QLabel#ramNeed[warn="true"] { color: %WARN%; }
QLabel#editorEmpty { color: %WARN%; background: %WARNA%; border: 1px solid %WARN%; border-radius: 10px;
  padding: 10px 18px; font-weight: 600; }
QLabel[chip="true"] { background: %BG%; border: 1px solid %BORDER%; border-radius: 6px; padding: 1px 8px;
  color: %MUTED%; }
QLabel#liveDot { min-width: 8px; max-width: 8px; min-height: 8px; max-height: 8px; border-radius: 4px; }
QLabel#liveDot[state="ok"] { background: %GOOD%; }
QLabel#liveDot[state="warn"] { background: %WARN%; }
QLabel#liveDot[state="none"] { background: %MUTED%; }
)"
		/* input boxes; spin boxes without their arrow buttons. The edge in the accent while focused, and while the mouse
		 * is over one, as a button's (the rules after them keep a disabled, read-only or out-of-range edge) */
		R"(
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
  background: %BG%; border: 1px solid %CONTROL%; border-radius: 7px; padding: 5px 8px;
  selection-background-color: %ACCENT%; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: %ACCENT%; }
QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover { border-color: %ACCENT%; }
QSpinBox[outOfRange="true"] { border-color: %WARN%; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { border-color: %BORDER%;
  color: %MUTED%; }
QLineEdit:read-only { border-color: %BORDER%; color: %MUTED%; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { min-height: 20px; max-height: 20px; }
QAbstractItemView QLineEdit, QAbstractItemView QSpinBox, QAbstractItemView QDoubleSpinBox,
QAbstractItemView QComboBox, QComboBox QLineEdit { min-height: 0px; max-height: 16777215px; }
QComboBox::drop-down { border: none; width: 20px; }
%COMBOARROW%
QComboBox QAbstractItemView { background: %S2%; border: 1px solid %BORDER%; selection-background-color: %ACCENTA%; }
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; border: none; }
)"
		/* buttons: plain, checked, #primary, #danger, the chart's Hold, segmented choices, tool buttons */
		R"(
QPushButton { background: %S2%; border: 1px solid %BORDER%; border-radius: 7px; padding: 6px 12px; }
QPushButton:hover { border-color: %ACCENT%; }
QPushButton:pressed { background: %BG%; }
QPushButton:disabled { color: %MUTED%; }
QPushButton:checked { background: %ACCENTA%; border-color: %ACCENT%; }
QPushButton#primary { background: %AFILL%; border: 1px solid %AFILL%; color: white; font-weight: 600;
  padding: 7px 11px; }
QPushButton#primary:hover { background: %AFILLH%; border-color: %AFILLH%; }
QPushButton#hold[live="false"] { background: %AFILL%; border-color: %AFILL%; color: white; }
QPushButton#danger { background: %BFILL%; border: 1px solid %BFILL%; color: white; font-weight: 600;
  padding: 7px 11px; }
QPushButton[segment="true"] { border-radius: 0; padding: 6px 10px; }
QPushButton[segment="true"]:checked { background: %AFILL%; color: white; border-color: %AFILL%; }
QPushButton[segmentPos="first"] { border-top-left-radius: 7px; border-bottom-left-radius: 7px; }
QPushButton[segmentPos="last"] { border-top-right-radius: 7px; border-bottom-right-radius: 7px; }
QPushButton[menuButton="true"] { padding-right: 30px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 3px; }
QToolButton:hover { background: %S2%; }
)"
		/* check boxes */
		R"(
QCheckBox { spacing: 8px; border-radius: 5px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid %CONTROL%; background: %BG%; }
QCheckBox::indicator:hover { border-color: %ACCENT%; }
QCheckBox::indicator:checked { background: %AFILL%; border-color: %AFILL%; %TICK% }
QCheckBox::indicator:indeterminate { background: %ACCENTA%; border-color: %ACCENT%; %DASH% }
QCheckBox::indicator:disabled { border-color: %BORDER%; }
QCheckBox::indicator:checked:disabled { background: %MUTED%; border-color: %MUTED%; }
)"
		/* the link state pill: round ends. Qt draws square corners when the radius is more than half the height, so
		 * the radius (12) stays under half the least height (18 + 2 x 6 + 2 = 32), whatever the state's text */
		R"(
QLabel#pill { border-radius: 12px; padding: 6px 12px; font-weight: 600; border: 1px solid transparent;
  min-height: 18px; }
QLabel#pill[state="idle"] { background: transparent; color: %MUTED%; }
QLabel#pill[state="busy"] { background: %WARNA%; color: %WARN%; }
QLabel#pill[state="ok"] { background: %GOODA%; color: %GOOD%; }
QLabel#pill[state="error"] { background: %BADA%; color: %BAD%; }
)"
		/* tabs: text with an accent underline */
		R"(
QTabWidget::pane { border: none; }
QTabBar::tab { background: transparent; color: %MUTED%; padding: 8px 16px; margin-right: 4px;
  border: none; border-bottom: 2px solid transparent; font-weight: 600; }
QTabBar::tab:selected { color: %TEXT%; border-bottom: 2px solid %ACCENT%; }
QTabBar::tab:hover { color: %TEXT%; }
)"
		/* tables and their headers */
		R"(
QTableView { background: %S1%; alternate-background-color: %S1ALT%; border: 1px solid %BORDER%;
  border-radius: 10px; gridline-color: transparent; selection-background-color: %ACCENTA%; selection-color: %TEXT%; }
QTableView::item { padding: 2px 6px; border: none; }
QTableView::indicator { width: 14px; height: 14px; border-radius: 4px; border: 1px solid %CONTROL%; background: %BG%; }
QTableView::indicator:checked { background: %AFILL%; border-color: %AFILL%; %TICK% }
QTableView::indicator:indeterminate { background: %ACCENTA%; border-color: %ACCENT%; %DASH% }
QHeaderView { background: transparent; }
QHeaderView::section { background: %S1%; color: %MUTED%; border: none; border-bottom: 1px solid %BORDER%;
  padding: 7px 6px; font-weight: 700; font-size: 8pt; }
QTableCornerButton::section { background: %S1%; border: none; }
)"
		/* thin scroll bars without arrow buttons; the handle in the control colour, 3:1 to every background
		 * (WCAG 1.4.11): a border-coloured handle was barely there. Darker while hovered and dragged. */
		R"(
QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar::handle:vertical { background: %CONTROL%; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover, QScrollBar::handle:vertical:pressed { background: %MUTED%; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle:horizontal { background: %CONTROL%; border-radius: 4px; min-width: 30px; }
QScrollBar::handle:horizontal:hover, QScrollBar::handle:horizontal:pressed { background: %MUTED%; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)"
		/* status bar, menus, message boxes, splitters */
		R"(
QStatusBar { background: %S1%; border-top: 1px solid %BORDER%; color: %MUTED%; }
QStatusBar QLabel { color: %MUTED%; padding: 0 8px; }
QStatusBar QLabel#statusSlow { color: %WARN%; }
QLabel#problem { color: %BAD%; }
QMenu { background: %S2%; border: 1px solid %BORDER%; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 6px 18px; border-radius: 5px; }
QMenu::item:selected { background: %ACCENTA%; }
QMenu::indicator { width: 16px; height: 16px; padding-left: 6px; }
QMenu::indicator:non-exclusive { border-radius: 4px; border: 1px solid %CONTROL%; background: %BG%; margin-left: 6px; padding: 0; }
QMenu::indicator:non-exclusive:checked { background: %AFILL%; border-color: %AFILL%; %TICK% }
QMenu::indicator:exclusive:checked { %MENUTICK% }
QMenu QLabel#menuTitle { color: %MUTED%; font-weight: 600; padding: 4px 18px 2px 18px; }
QMessageBox { background: %S1%; }
QSplitter::handle { background: transparent; }
)"
		/* the focus ring: only for the focus given from the keyboard ([keyFocus], theme.h); last, so it wins */
		R"(
QPushButton[keyFocus="true"], QToolButton[keyFocus="true"] { border: 1px solid %FOCUS%; }
QPushButton#primary[keyFocus="true"], QPushButton#danger[keyFocus="true"] { border: 1px solid %FOCUS%; }
QCheckBox[keyFocus="true"] { background: %ACCENTA%; }
QCheckBox[keyFocus="true"]::indicator { border-color: %FOCUS%; }
QTabBar[keyFocus="true"]::tab:selected { background: %ACCENTA%; border-top-left-radius: 6px;
  border-top-right-radius: 6px; }
QTableView[keyFocus="true"], QListWidget[keyFocus="true"], QTextBrowser[keyFocus="true"] { border-color: %FOCUS%; }
QListWidget#busDevices[keyFocus="true"] { border-color: %FOCUS%; }
)");
}

QString styleSheet(const ThemeColors &c, bool dark) {
	/* table rows alternate with a shade just off the panel colour */
	const QColor alternateRow = dark ? c.surface.lighter(108) : c.surface2.lighter(102);
	/* the tints behind coloured text: lighter in the light look, so that text keeps 4.5:1 on them */
	const int tint = dark ? 40 : 24;
	const std::pair<const char *, QString> placeholders[] = {
		{ "%BG%", cssColor(c.bg) },
		{ "%S1%", cssColor(c.surface) },
		{ "%S1ALT%", cssColor(alternateRow) },
		{ "%S2%", cssColor(c.surface2) },
		{ "%BORDER%", cssColor(c.border) },
		{ "%TEXT%", cssColor(c.text) },
		{ "%MUTED%", cssColor(c.muted) },
		{ "%ACCENT%", cssColor(c.accent) },
		{ "%ACCENTH%", cssColor(c.accent.lighter(112)) }, /* hovered */
		{ "%ACCENTA%", cssColorWithAlpha(c.accent, 60) },
		{ "%AFILLH%", cssColor(c.accentFill.lighter(106)) }, /* hovered: white on it still 4.5:1 */
		{ "%AFILL%", cssColor(c.accentFill) },
		{ "%BFILL%", cssColor(c.badFill) },
		{ "%CONTROL%", cssColor(c.control) },
		{ "%FOCUS%", cssColor(c.text) }, /* the ring: the text colour, 15:1 */
		{ "%GOOD%", cssColor(c.good) },
		{ "%GOODA%", cssColorWithAlpha(c.good, tint) },
		{ "%WARN%", cssColor(c.warn) },
		{ "%WARNA%", cssColorWithAlpha(c.warn, tint) },
		{ "%BAD%", cssColor(c.bad) },
		{ "%BADA%", cssColorWithAlpha(c.bad, tint) },
	};
	QString sheet = styleSheetTemplate();
	for (const auto &[placeholder, value] : placeholders) sheet.replace(QLatin1String(placeholder), value);
	const QString tick = checkTickFile(), dash = checkDashFile(c.accent), menuTick = menuTickFile(c.accent);
	sheet.replace(QLatin1String("%TICK%"), tick.isEmpty() ? QString() : QStringLiteral("image: url(\"%1\");").arg(tick));
	sheet.replace(QLatin1String("%MENUTICK%"), menuTick.isEmpty() ? QString()
			: QStringLiteral("image: url(\"%1\");").arg(menuTick));
	sheet.replace(QLatin1String("%DASH%"), dash.isEmpty() ? QString() : QStringLiteral("image: url(\"%1\");").arg(dash));
	const QString arrow = comboArrowFile(c.muted);
	sheet.replace(QLatin1String("%COMBOARROW%"), arrow.isEmpty() ? QString()
			: QStringLiteral("QComboBox::down-arrow { image: url(\"%1\"); width: 10px; height: 6px; }\n"
					"QPushButton::menu-indicator { image: url(\"%1\"); width: 10px; height: 6px;"
					" subcontrol-origin: padding; subcontrol-position: center right; right: 10px; }")
					.arg(arrow));
	return sheet;
}

/* for what Qt draws without the style sheet: native parts, the text browser, item views */
QPalette paletteFor(const ThemeColors &c) {
	QPalette p;
	p.setColor(QPalette::Window, c.bg);
	p.setColor(QPalette::WindowText, c.text);
	p.setColor(QPalette::Base, c.surface);
	p.setColor(QPalette::AlternateBase, c.surface2);
	p.setColor(QPalette::Text, c.text);
	p.setColor(QPalette::Button, c.surface2);
	p.setColor(QPalette::ButtonText, c.text);
	p.setColor(QPalette::Highlight, c.accentFill); /* white text on it (HighlightedText) */
	p.setColor(QPalette::HighlightedText, Qt::white);
	p.setColor(QPalette::ToolTipBase, c.surface2);
	p.setColor(QPalette::ToolTipText, c.text);
	p.setColor(QPalette::PlaceholderText, c.muted);
	p.setColor(QPalette::Disabled, QPalette::Text, c.muted);
	p.setColor(QPalette::Disabled, QPalette::ButtonText, c.muted);
	return p;
}

QFont applicationFont() {
	QFont f(QStringLiteral("Segoe UI Variable Text"));
	f.setPointSize(10);
	f.setStyleStrategy(QFont::PreferAntialias);
	return f;
}

} // namespace

namespace Theme {

void apply(QApplication &app, bool dark) {
	g_dark = dark;
	g_colors = dark ? darkColors() : lightColors();
	app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
	app.setPalette(paletteFor(g_colors));
	app.setFont(applicationFont());
	app.setStyleSheet(styleSheet(g_colors, dark));
	/* Qt's popups open and close without animations (the system's "animate controls" turns them on): a drop-down's
	 * slide went away before the drop-down itself was drawn (it flashed), and the fades copy the screen the old way
	 * (GDI), which does not hold the chart's plot drawn by a card (chart_widget.h) */
	for (const Qt::UIEffect effect : { Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip,
				 Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox })
		QApplication::setEffectEnabled(effect, false);
	static KeyFocusMarker *marker = nullptr; /* once, for every apply */
	if (!marker) {
		marker = new KeyFocusMarker(&app);
		app.installEventFilter(marker);
	}
}

const ThemeColors &colors() { return g_colors; }
bool isDark() { return g_dark; }

bool switched(const QEvent *event, bool &drawnDark) {
	if (event->type() != QEvent::PaletteChange || drawnDark == g_dark) return false;
	drawnDark = g_dark;
	return true;
}

} // namespace Theme
