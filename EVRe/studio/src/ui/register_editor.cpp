/* SPDX-License-Identifier: Apache-2.0 */
/* The form of the registers selected in the Map editor tab: see register_editor.h. */
#include "ui/register_editor.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionTab>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "model/map_document.h"
#include "ui/field_editor.h"
#include "ui/name_table.h"
#include "ui/map_table_model.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

/* one merge key per box: typing into it is one undo step */
enum Key { KeyAddr = 1, KeyName, KeyType, KeySize, KeyUnit, KeyAccess, KeyWrite, KeyGroup, KeyDesc, KeyScale,
	KeyOffset, KeyDecimals, KeyMin, KeyMax, KeyDefault, KeyPersist, KeyDanger, KeyHex, KeyNotes, KeyEnum, KeySpecial,
	KeyFields, KeyPlot, KeyPastLimits, KeyClosed, KeyReservedZero };

/* the pages, in their order */
enum Page { PageGeneral, PageValues, PageFields, PageNotes };

const char *SEVERAL = QT_TRANSLATE_NOOP("RegisterEditor", "(several)");

/* a number as short as it can be written; NaN: empty */
QString numberText(double value) {
	if (std::isnan(value)) return {};
	return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

QString accessOf(const RegDef &def) { return accessText(def); }

QString pastLimitsOf(const RegDef &def) { return def.clamps ? QStringLiteral("clamp") : QStringLiteral("refuse"); }

QString writeOf(const RegDef &def) {
	return MapTableModel::writeChoices().value(int(def.write));
}

/* the value `get` gives every register, or nothing if they differ */
template <typename T, typename Get>
bool allSame(const QVector<const RegDef *> &defs, Get get, T &value) {
	value = get(*defs.front());
	for (const RegDef *def : defs)
		if (!(get(*def) == value)) return false;
	return true;
}

/* a value "default" may be given as: a number, or a name the register gives one of its values */
bool defaultFrom(const RegDef &def, const QString &text, double &value) {
	bool ok = false;
	value = QLocale::c().toDouble(text, &ok);
	if (ok) return true;
	for (const SpecialValue &special : def.special)
		if (special.name.compare(text, Qt::CaseInsensitive) == 0) {
			value = special.value;
			return true;
		}
	for (auto it = def.enumValues.begin(); it != def.enumValues.end(); ++it)
		if (it.value().compare(text, Qt::CaseInsensitive) == 0) {
			value = double(it.key()) * def.scale + def.offset;
			return true;
		}
	return false;
}

/* The pages' tabs: a tab that may get a warning sign (its data true) is as wide without it as with it, so the
 * sign coming and going moves no tab. */
class PageTabBar : public QTabBar {
protected:
	QSize tabSizeHint(int index) const override {
		QSize size = QTabBar::tabSizeHint(index);
		if (!tabData(index).toBool() || !tabIcon(index).isNull()) return size;
		/* what an icon adds: the tab bar's part (its width and 4 px), and the style's (the style sheet's spacing) */
		QStyleOptionTab plain;
		initStyleOption(&plain, index);
		QStyleOptionTab withIcon = plain;
		QPixmap dot(1, 1);
		dot.fill(Qt::transparent);
		withIcon.icon = QIcon(dot);
		const int styleExtra = style()->sizeFromContents(QStyle::CT_TabBarTab, &withIcon, QSize(), this).width()
				- style()->sizeFromContents(QStyle::CT_TabBarTab, &plain, QSize(), this).width();
		size.rwidth() += iconSize().width() + 4 + styleExtra;
		return size;
	}
};

class PageTabs : public QTabWidget {
public:
	/* as QTabWidget makes its own tab bar: no base line (Fusion draws a light one across the tabs), and its name */
	PageTabs() {
		auto *bar = new PageTabBar;
		bar->setObjectName(QStringLiteral("qt_tabwidget_tabbar"));
		bar->setDrawBase(false);
		setTabBar(bar);
	}
};

} // namespace

RegisterEditor::RegisterEditor(MapDocument *doc, QWidget *parent) : QWidget(parent), doc_(doc) {
	pages_ = new PageTabs;
	pages_->setObjectName(QStringLiteral("editorPages"));
	pages_->addTab(buildGeneral(), tr("General"));
	pages_->addTab(gate(buildValues(), valuesGate_, valuesNote_), tr("Values"));
	fields_ = new FieldEditor;
	pages_->addTab(gate(fields_, fieldsGate_, fieldsNote_), tr("Bit fields"));
	pages_->addTab(buildNotes(), tr("Notes"));
	for (int page : { PageValues, PageFields }) pages_->tabBar()->setTabData(page, true); /* room for the sign */
	form_ = new QWidget;
	auto *formLayout = new QVBoxLayout(form_);
	formLayout->setContentsMargins(0, 0, 0, 0);
	formLayout->addWidget(buildHeader());
	formLayout->addSpacing(4);
	formLayout->addWidget(pages_, 1);
	stack_ = new QStackedWidget;
	stack_->addWidget(buildEmptyNote());
	stack_->addWidget(form_);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(stack_);
	connectBoxes();
	connect(doc_, &MapDocument::changed, this, &RegisterEditor::reload);
	/* the live value moves while the device is polled */
	auto *liveTimer = new QTimer(this);
	liveTimer->setInterval(250);
	connect(liveTimer, &QTimer::timeout, this, [this] {
		if (isVisible()) refreshLive();
	});
	liveTimer->start();
	reload();
}

/* ------------------------------------------------------------------ building */

/* The header: a card (theme: #card) with the name, the address, type and access as chips (theme: [chip]), and
 * the LIVE value with its dot (theme: #liveDot[state]) and the decoded text under it. Each line is one line high
 * whatever it shows, so the card keeps its size and the pages under it never move. */
QWidget *RegisterEditor::buildHeader() {
	auto chip = [](const char *name) {
		auto *label = new QLabel;
		label->setObjectName(QLatin1String(name));
		label->setProperty("chip", true);
		return label;
	};
	auto singleLine = [](QLabel *label) {
		label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); /* as wide as the card: cut short, not wider */
		label->setFixedHeight(label->fontMetrics().height() + 2);
		label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	};
	heading_ = new QLabel;
	heading_->setObjectName(QStringLiteral("editorHeading"));
	QFont headingFont = heading_->font();
	headingFont.setBold(true);
	headingFont.setPointSizeF(headingFont.pointSizeF() * 1.2);
	heading_->setFont(headingFont);
	heading_->setMinimumWidth(0);
	addrChip_ = chip("editorAddr");
	addrChip_->setFont(monospaceFont());
	typeChip_ = chip("editorType");
	accessChip_ = chip("editorAccess");
	/* each chip as wide as its longest text (and the theme's 8 px padding and 1 px border a side): the chips keep
	 * their places from one register to the next, whatever its name, type or access */
	auto fixWidth = [](QLabel *label, const QStringList &texts) {
		int widest = 0;
		for (const QString &text : texts) widest = std::max(widest, label->fontMetrics().horizontalAdvance(text));
		label->setFixedWidth(widest + 2 * (8 + 1) + 4);
		label->setAlignment(Qt::AlignCenter);
	};
	fixWidth(addrChip_, { QStringLiteral("0xFFFF") }); /* addrText: 0x and four hex digits */
	fixWidth(typeChip_, MapTableModel::typeChoices());
	fixWidth(accessChip_, MapTableModel::accessChoices());
	auto *titleRow = new QHBoxLayout;
	titleRow->setSpacing(8);
	titleRow->addWidget(heading_, 1);
	titleRow->addWidget(addrChip_);
	titleRow->addWidget(typeChip_);
	titleRow->addWidget(accessChip_);

	auto *caption = new QLabel(tr("LIVE"));
	caption->setObjectName(QStringLiteral("cardTitle"));
	/* a round dot the theme draws (a glyph such as a bullet sits below the middle of its line) */
	liveDot_ = new QLabel;
	liveDot_->setObjectName(QStringLiteral("liveDot"));
	liveDot_->setProperty("state", QStringLiteral("none"));
	liveLine_ = new QLabel;
	liveLine_->setObjectName(QStringLiteral("editorLive"));
	QFont valueFont = monospaceFont();
	valueFont.setBold(true);
	valueFont.setPointSizeF(liveLine_->font().pointSizeF() * 1.3);
	liveLine_->setFont(valueFont);
	singleLine(liveLine_);
	auto *liveRow = new QHBoxLayout;
	liveRow->setSpacing(8);
	liveRow->addWidget(caption, 0, Qt::AlignVCenter);
	liveRow->addWidget(liveDot_, 0, Qt::AlignVCenter);
	liveRow->addWidget(liveLine_, 1, Qt::AlignVCenter);

	liveDetail_ = new QLabel;
	liveDetail_->setObjectName(QStringLiteral("editorLiveDetail"));
	singleLine(liveDetail_);

	auto *card = new QFrame;
	card->setObjectName(QStringLiteral("card"));
	auto *layout = new QVBoxLayout(card);
	layout->setContentsMargins(14, 10, 14, 10);
	layout->setSpacing(6);
	layout->addLayout(titleRow);
	layout->addLayout(liveRow);
	layout->addWidget(liveDetail_);
	return card;
}

/* A page the selection may not have (Values, Bit fields): the page, or in its place a note in the middle in the
 * warning colour (theme: #editorEmpty) that says why. Its tab stays on (a tab turned off makes Qt jump to the
 * next one): showGates gives it a warning sign and the reason as its tooltip. */
QWidget *RegisterEditor::gate(QWidget *page, QStackedWidget *&gate, QLabel *&note) {
	note = new QLabel;
	note->setObjectName(QStringLiteral("editorEmpty"));
	note->setAlignment(Qt::AlignCenter);
	note->setWordWrap(true);
	auto *notePage = new QWidget;
	auto *layout = new QVBoxLayout(notePage);
	layout->setContentsMargins(12, 0, 12, 0);
	layout->addStretch();
	layout->addWidget(note); /* the page's width: a wrapped label given an alignment here gets too little height */
	layout->addStretch();
	gate = new QStackedWidget;
	gate->addWidget(page);
	gate->addWidget(notePage);
	return gate;
}

void RegisterEditor::showGates() {
	const QIcon sign = warningIcon(Theme::colors().warn);
	auto show = [&](int index, QStackedWidget *gate, QLabel *note, const QString &why) {
		gate->setCurrentIndex(why.isEmpty() ? 0 : 1);
		note->setText(QStringLiteral("⚠  ") + why);
		pages_->setTabIcon(index, why.isEmpty() ? QIcon() : sign);
		pages_->setTabToolTip(index, why);
	};
	show(PageValues, valuesGate_, valuesNote_, valuesWhy_);
	show(PageFields, fieldsGate_, fieldsNote_, fieldsWhy_);
}

/* the theme changed: the signs again, in its warning colour */
void RegisterEditor::changeEvent(QEvent *event) {
	QWidget::changeEvent(event);
	if (fieldsGate_ && (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)) showGates();
}

/* none selected: a note in the middle, in the warning colour (theme: #editorEmpty), and how to pick one */
QWidget *RegisterEditor::buildEmptyNote() {
	auto *note = new QLabel(QStringLiteral("⚠  ") + tr("No register selected"));
	note->setObjectName(QStringLiteral("editorEmpty"));
	note->setAlignment(Qt::AlignCenter);
	QLabel *hint = mutedLabel(tr("Click a register in the table to edit it, or select several to edit them together."));
	hint->setAlignment(Qt::AlignCenter);
	hint->setWordWrap(true);
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->addStretch();
	layout->addWidget(note, 0, Qt::AlignHCenter);
	layout->addSpacing(8);
	layout->addWidget(hint);
	layout->addStretch();
	return page;
}

QWidget *RegisterEditor::buildGeneral() {
	address_ = new QLineEdit;
	name_ = new QLineEdit;
	type_ = new QComboBox;
	type_->addItems(MapTableModel::typeChoices());
	size_ = new QSpinBox;
	size_->setRange(1, 0xFFFF);
	unit_ = new QLineEdit;
	access_ = new QComboBox;
	access_->addItem(tr("read-only"), QStringLiteral("ro"));
	access_->addItem(tr("read-write"), QStringLiteral("rw"));
	access_->addItem(tr("write-only (never read)"), QStringLiteral("wo"));
	write_ = new QComboBox;
	write_->addItem(tr("normal"), QStringLiteral("normal"));
	write_->addItem(tr("action: does something, reads back idle"), QStringLiteral("action"));
	write_->addItem(tr("w1c: a 1 written clears that bit"), QStringLiteral("w1c"));
	group_ = new QComboBox;
	group_->setEditable(true);
	desc_ = new QLineEdit;
	persist_ = new QCheckBox(tr("persist: kept across a reset"));
	danger_ = new QCheckBox(tr("danger: confirm every write"));
	hex_ = new QCheckBox(tr("show in hex"));
	plot_ = new QCheckBox(tr("plot: a line on the chart"));
	plot_->setToolTip(tr("Untick for a value that does not change with time (an ID, a version, a setting): it gets "
			"no Plot box, and Plot shown passes it by"));
	scale_ = new QLineEdit;
	offset_ = new QLineEdit;
	decimals_ = new QSpinBox;
	decimals_->setRange(-1, 15);
	decimals_->setSpecialValueText(tr("auto"));
	min_ = new QLineEdit;
	max_ = new QLineEdit;
	default_ = new QLineEdit;
	pastLimits_ = new QComboBox;
	pastLimits_->setObjectName(QStringLiteral("pastLimits"));
	pastLimits_->addItem(tr("refused: the device refuses a value past them"), QStringLiteral("refuse"));
	pastLimits_->addItem(tr("clamped: the device takes it and clamps it"), QStringLiteral("clamp"));
	pastLimits_->setToolTip(tr("What the device does with a value past min or max (\"past_limits\"). Refused: hosts "
			"ask before they send one, and a device with EVRe Guard refuses it (value refused, 15). Clamped: hosts "
			"send it as it is, and EVRe Guard checks only that it is a number of the type"));
	closed_ = new QCheckBox(tr("closed: only its value names and special values"));
	closed_->setObjectName(QStringLiteral("closedSet"));
	closed_->setToolTip(tr("\"closed\": a host writes only the register's value names, its special values and, for an "
			"action, its idle value; a device with EVRe Guard refuses any other value (15), whatever min and max say"));
	reservedZero_ = new QCheckBox(tr("reserved_zero: bits no field covers are 0"));
	reservedZero_->setObjectName(QStringLiteral("reservedZero"));
	reservedZero_->setToolTip(tr("\"reserved_zero\": the bits no bit field covers must be written 0; a device with EVRe "
			"Guard refuses a value with one of them set (15). For a register with bit fields"));
	for (QLineEdit *box : { scale_, offset_, min_, max_, default_ }) box->setClearButtonEnabled(true);
	min_->setPlaceholderText(tr("none"));
	max_->setPlaceholderText(tr("none"));
	default_->setPlaceholderText(tr("none: a number or a value name"));
	/* each box's hint when empty, kept: reload shows "(several)" in its place and this again after */
	for (QLineEdit *box : { address_, name_, unit_, desc_, scale_, offset_, min_, max_, default_ })
		box->setProperty("emptyHint", box->placeholderText());
	address_->setToolTip(tr("0x0000 … 0xFFFF, or decimal"));
	scale_->setToolTip(tr("shown = raw × scale + offset"));
	min_->setToolTip(tr("the lowest value a write may set, in shown units (special values are always allowed)"));
	max_->setToolTip(tr("the highest value a write may set, in shown units"));
	default_->setToolTip(tr("the value after a reset (with persist: the factory value)"));

	auto *form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignRight);
	form->setVerticalSpacing(6);
	form->addRow(tr("Address"), address_);
	form->addRow(tr("Name"), name_);
	form->addRow(tr("Type"), type_);
	form->addRow(tr("Size (bytes)"), size_);
	form->addRow(tr("Unit"), unit_);
	form->addRow(tr("Access"), access_);
	form->addRow(tr("Write"), write_);
	form->addRow(tr("Group"), group_);
	form->addRow(tr("Description"), desc_);
	form->addRow(QString(), persist_);
	form->addRow(QString(), danger_);
	form->addRow(QString(), hex_);
	form->addRow(QString(), plot_);
	form->addRow(tr("Scale"), scale_);
	form->addRow(tr("Offset"), offset_);
	form->addRow(tr("Decimals"), decimals_);
	form->addRow(tr("Min"), min_);
	form->addRow(tr("Max"), max_);
	form->addRow(tr("Past limits"), pastLimits_);
	form->addRow(QString(), closed_);
	form->addRow(QString(), reservedZero_);
	form->addRow(tr("Default"), default_);
	auto *page = new QWidget;
	page->setLayout(form);
	/* in a scroll area: its 21 rows would make the window taller than a 768-line screen (theme: #formScroll) */
	auto *scroll = new QScrollArea;
	scroll->setObjectName(QStringLiteral("formScroll"));
	scroll->setWidget(page);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	return scroll;
}

QWidget *RegisterEditor::buildValues() {
	enum_ = new NameTable(NameTable::Keys::Integers, QStringLiteral("enumNames"));
	special_ = new NameTable(NameTable::Keys::Numbers, QStringLiteral("specialValues"));
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	/* the notes wrap: on one line the longer would make the panel 860 px wide */
	QLabel *enumNote = mutedLabel(tr("Names of values (enum): the raw number, shown by its name"));
	QLabel *specialNote = mutedLabel(tr("Special values: a name for single values of a number, in shown units "
			"(\"-1\" not measured); a write may always set them, whatever min and max say"));
	for (QLabel *note : { enumNote, specialNote }) note->setWordWrap(true);
	layout->addWidget(enumNote);
	layout->addWidget(enum_, 3);
	layout->addWidget(specialNote);
	layout->addWidget(special_, 2);
	return page;
}

QWidget *RegisterEditor::buildNotes() {
	notes_ = new QPlainTextEdit;
	notes_->setPlaceholderText(tr("Longer text for the reader of the map and of its export: what a write does, "
			"sequences, examples. Markdown is fine."));
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->addWidget(notes_);
	return page;
}

void RegisterEditor::connectBoxes() {
	/* the steps' names: "Unit of SUPPLY_V" or "Unit of 3 registers" */
	auto step = [this](const QString &what) {
		if (uids_.size() == 1) {
			const RegDef *def = doc_->reg(uids_.front());
			return tr("%1 of %2").arg(what, def ? def->name : QString());
		}
		return tr("%1 of %n registers", nullptr, int(uids_.size())).arg(what);
	};

	connect(address_, &QLineEdit::textEdited, this, [this, step](const QString &text) {
		bool ok = false;
		const uint addr = parseAddress(text, &ok);
		markInvalid(address_, !ok || addr > 0xFFFF);
		if (ok && addr <= 0xFFFF) apply(step(tr("Address")), KeyAddr, [addr](RegDef &d) { d.addr = uint16_t(addr); });
	});
	connect(name_, &QLineEdit::textEdited, this, [this, step](const QString &text) {
		const QString name = text.trimmed();
		markInvalid(name_, name.isEmpty());
		if (!name.isEmpty()) apply(step(tr("Name")), KeyName, [name](RegDef &d) { d.name = name; });
	});
	connect(type_, &QComboBox::activated, this, [this, step] {
		RegType type;
		if (!parseType(type_->currentText(), type)) return;
		apply(step(tr("Type")), KeyType, [type](RegDef &d) {
			d.type = type;
			if (type != RegType::Bytes) d.size = typeSize(type);
		});
	});
	connect(size_, &QSpinBox::valueChanged, this, [this, step](int size) {
		if (!loading_) apply(step(tr("Size")), KeySize, [size](RegDef &d) {
			if (d.type == RegType::Bytes) d.size = size;
		});
	});
	connect(unit_, &QLineEdit::textEdited, this, [this, step](const QString &text) {
		const QString unit = text.trimmed();
		apply(step(tr("Unit")), KeyUnit, [unit](RegDef &d) { d.unit = unit; });
	});
	connect(desc_, &QLineEdit::textEdited, this, [this, step](const QString &text) {
		const QString desc = text.trimmed();
		apply(step(tr("Description")), KeyDesc, [desc](RegDef &d) { d.desc = desc; });
	});
	connect(access_, &QComboBox::activated, this, [this, step] {
		const QString access = access_->currentData().toString();
		apply(step(tr("Access")), KeyAccess, [access](RegDef &d) {
			d.rw = access != QLatin1String("ro");
			d.readable = access != QLatin1String("wo");
		});
	});
	connect(pastLimits_, &QComboBox::activated, this, [this, step] {
		const bool clamps = pastLimits_->currentData().toString() == QLatin1String("clamp");
		apply(step(tr("Past limits")), KeyPastLimits, [clamps](RegDef &d) { d.clamps = clamps; });
	});
	connect(write_, &QComboBox::activated, this, [this, step] {
		const WriteKind kind = WriteKind(write_->currentIndex());
		apply(step(tr("Write")), KeyWrite, [kind](RegDef &d) { d.write = kind; });
	});
	auto setGroup = [this, step](const QString &text) {
		const QString group = text.trimmed();
		if (!group.isEmpty()) apply(step(tr("Group")), KeyGroup, [group](RegDef &d) { d.group = group; });
	};
	connect(group_->lineEdit(), &QLineEdit::textEdited, this, setGroup);
	connect(group_, &QComboBox::activated, this, [this, setGroup] { setGroup(group_->currentText()); });

	auto numberBox = [this, step](QLineEdit *box, const QString &what, int key, bool emptyAllowed,
			std::function<void(RegDef &, double)> set) {
		connect(box, &QLineEdit::textEdited, this, [this, box, what, key, emptyAllowed, set, step] {
			double value;
			if (!readNumber(box, value, emptyAllowed)) return;
			apply(step(what), key, [set, value](RegDef &d) { set(d, value); });
		});
	};
	numberBox(scale_, tr("Scale"), KeyScale, true, [](RegDef &d, double v) {
		d.scale = std::isnan(v) || v == 0 ? 1.0 : v;
	});
	numberBox(offset_, tr("Offset"), KeyOffset, true, [](RegDef &d, double v) { d.offset = std::isnan(v) ? 0.0 : v; });
	numberBox(min_, tr("Min"), KeyMin, true, [](RegDef &d, double v) { d.min = v; });
	numberBox(max_, tr("Max"), KeyMax, true, [](RegDef &d, double v) { d.max = v; });
	connect(default_, &QLineEdit::textEdited, this, [this, step](const QString &typed) {
		const QString text = typed.trimmed();
		/* a number, or a name one of the registers gives a value */
		bool known = text.isEmpty();
		double value = NO_LIMIT;
		for (quint32 uid : uids_) {
			const RegDef *def = doc_->reg(uid);
			if (def && !text.isEmpty() && defaultFrom(*def, text, value)) known = true;
		}
		markInvalid(default_, !known);
		if (!known) return;
		apply(step(tr("Default")), KeyDefault, [text](RegDef &d) {
			double v = NO_LIMIT;
			if (text.isEmpty() || defaultFrom(d, text, v)) d.defaultValue = v;
		});
	});
	connect(decimals_, &QSpinBox::valueChanged, this, [this, step](int decimals) {
		if (!loading_) apply(step(tr("Decimals")), KeyDecimals, [decimals](RegDef &d) { d.decimals = decimals; });
	});
	auto flag = [this, step](QCheckBox *box, const QString &what, int key, bool RegDef::*member) {
		connect(box, &QCheckBox::clicked, this, [this, box, what, key, member, step](bool on) {
			box->setTristate(false);
			box->setChecked(on);
			apply(step(what), key, [member, on](RegDef &d) { d.*member = on; });
		});
	};
	flag(persist_, tr("Persist"), KeyPersist, &RegDef::persist);
	flag(danger_, tr("Danger"), KeyDanger, &RegDef::danger);
	flag(hex_, tr("Hex"), KeyHex, &RegDef::hex);
	flag(plot_, tr("Plot"), KeyPlot, &RegDef::plottable);
	flag(closed_, tr("Closed"), KeyClosed, &RegDef::closed);
	flag(reservedZero_, tr("Reserved bits"), KeyReservedZero, &RegDef::reservedZero);
	connect(notes_, &QPlainTextEdit::textChanged, this, [this, step] {
		if (loading_) return;
		const QString notes = notes_->toPlainText();
		apply(step(tr("Notes")), KeyNotes, [notes](RegDef &d) { d.notes = notes; });
	});

	connect(enum_, &NameTable::edited, this, [this, step] {
		QMap<qint64, QString> names;
		for (const NameTable::Entry &entry : enum_->entries()) names.insert(qint64(entry.value), entry.name);
		const bool hex = enum_->hex();
		apply(step(tr("Value names")), KeyEnum, [names, hex](RegDef &d) {
			d.enumValues = names;
			d.enumHex = hex;
		});
	});
	connect(special_, &NameTable::edited, this, [this, step] {
		QVector<SpecialValue> special;
		for (const NameTable::Entry &entry : special_->entries()) special.push_back({ entry.value, entry.name });
		apply(step(tr("Special values")), KeySpecial, [special](RegDef &d) { d.special = special; });
	});
	connect(fields_, &FieldEditor::edited, this, [this, step](const QVector<BitField> &fields) {
		apply(step(tr("Bit fields")), KeyFields, [fields](RegDef &d) { d.fields = fields; });
	});

	/* a box left: the next change is a new undo step */
	for (QLineEdit *box : { address_, name_, unit_, desc_, scale_, offset_, min_, max_, default_, group_->lineEdit() })
		connect(box, &QLineEdit::editingFinished, doc_, &MapDocument::endMerge);
}

/* ------------------------------------------------------------------- values */

void RegisterEditor::setTargets(const QVector<quint32> &uids) {
	if (uids == uids_) return;
	uids_ = uids;
	doc_->endMerge();
	for (QWidget *box : std::initializer_list<QWidget *>{ address_, name_, default_ }) markInvalid(box, false);
	reload();
}

void RegisterEditor::reload() {
	QVector<const RegDef *> defs;
	for (quint32 uid : uids_)
		if (const RegDef *def = doc_->reg(uid)) defs << def;
	loading_ = true;
	stack_->setCurrentWidget(defs.isEmpty() ? stack_->widget(0) : form_);
	form_->setEnabled(!defs.isEmpty());
	if (defs.isEmpty()) {
		heading_->clear();
		liveLine_->clear();
		liveDetail_->clear();
		loading_ = false;
		return;
	}
	const bool one = defs.size() == 1;
	const RegDef &first = *defs.front();
	heading_->setText(one ? first.name : tr("%n registers selected", nullptr, int(defs.size())));
	addrChip_->setText(addrText(first.addr));
	typeChip_->setText(typeName(first.type));
	accessChip_->setText(accessText(first));
	for (QLabel *chip : { addrChip_, typeChip_, accessChip_ }) chip->setVisible(one);

	/* a text box: the value, or empty with "(several)" (not the box being typed into); empty: its own hint */
	auto text = [&](QLineEdit *box, auto get) {
		if (box->hasFocus()) return;
		QString value;
		const bool same = allSame<QString>(defs, get, value);
		box->setText(same ? value : QString());
		box->setPlaceholderText(same ? box->property("emptyHint").toString() : tr(SEVERAL));
	};
	address_->setEnabled(one);
	name_->setEnabled(one);
	text(address_, [](const RegDef &d) { return addrText(d.addr); });
	text(name_, [](const RegDef &d) { return d.name; });
	text(unit_, [](const RegDef &d) { return d.unit; });
	text(desc_, [](const RegDef &d) { return d.desc; });
	text(scale_, [](const RegDef &d) { return numberText(d.scale); });
	text(offset_, [](const RegDef &d) { return numberText(d.offset); });
	text(min_, [](const RegDef &d) { return numberText(d.min); });
	text(max_, [](const RegDef &d) { return numberText(d.max); });
	text(default_, [](const RegDef &d) { return numberText(d.defaultValue); });

	/* a list: the value, or nothing picked */
	auto pick = [&](QComboBox *box, auto get, bool byData) {
		QString value;
		const bool same = allSame<QString>(defs, get, value);
		box->setCurrentIndex(same ? (byData ? box->findData(value) : box->findText(value)) : -1);
	};
	pick(type_, [](const RegDef &d) { return typeName(d.type); }, false);
	pick(access_, accessOf, true);
	pick(write_, writeOf, true);
	pick(pastLimits_, pastLimitsOf, true);
	if (!group_->lineEdit()->hasFocus()) {
		group_->clear();
		group_->addItems(doc_->groups());
		QString group;
		const bool same = allSame<QString>(defs, [](const RegDef &d) { return d.group; }, group);
		group_->setEditText(same ? group : QString());
		group_->lineEdit()->setPlaceholderText(same ? QString() : tr(SEVERAL));
	}
	int size = 0;
	const bool sameSize = allSame<int>(defs, [](const RegDef &d) { return d.size; }, size);
	bool bytes = true;
	for (const RegDef *def : defs) bytes = bytes && def->type == RegType::Bytes;
	size_->setEnabled(bytes);
	size_->setValue(sameSize ? size : 1);
	int decimals = -1;
	allSame<int>(defs, [](const RegDef &d) { return d.decimals; }, decimals);
	decimals_->setValue(decimals);

	/* a check box: ticked, not, or partly (they differ) */
	auto tick = [&](QCheckBox *box, bool RegDef::*member) {
		bool value;
		const bool same = allSame<bool>(defs, [member](const RegDef &d) { return d.*member; }, value);
		box->setTristate(!same);
		box->setCheckState(!same ? Qt::PartiallyChecked : value ? Qt::Checked : Qt::Unchecked);
	};
	tick(persist_, &RegDef::persist);
	tick(danger_, &RegDef::danger);
	tick(hex_, &RegDef::hex);
	tick(plot_, &RegDef::plottable);
	tick(closed_, &RegDef::closed);
	tick(reservedZero_, &RegDef::reservedZero);

	/* names and fields: one register at a time; fields: an integer one. Otherwise the page says why (showGates) */
	/* two lines each: what is wrong, then what is needed */
	valuesWhy_ = !one ? tr("Value names are kept per register.\nSelect one register to edit them.") : QString();
	fieldsWhy_ = !one ? tr("Bit fields are kept per register.\nSelect one register to edit them.")
			: first.type == RegType::Bytes ? tr("No bit fields: a bytes register is not a number.\nThey are for "
					"integer registers: u8 … u64, i8 … i64.")
			: first.type == RegType::F32 ? tr("No bit fields: an f32 is a float.\nThey are for integer registers: "
					"u8 … u64, i8 … i64.")
			: QString();
	showGates();
	if (one) {
		const RegDef &def = *defs.front();
		QVector<NameTable::Entry> names, special;
		for (auto it = def.enumValues.begin(); it != def.enumValues.end(); ++it) names.push_back({ double(it.key()), it.value() });
		for (const SpecialValue &value : def.special) special.push_back({ value.value, value.name });
		/* not while a cell is being typed into: it would close */
		if (!enum_->isAncestorOf(QApplication::focusWidget())) enum_->setEntries(names, def.enumHex);
		if (!special_->isAncestorOf(QApplication::focusWidget())) special_->setEntries(special, false);
		if (!fields_->isAncestorOf(QApplication::focusWidget()) || fields_->property("uid").toUInt() != def.uid) {
			fields_->setRegister(def);
			fields_->setProperty("uid", def.uid);
		}
	}
	refreshLive();

	if (!notes_->hasFocus()) {
		QString notes;
		const bool same = allSame<QString>(defs, [](const RegDef &d) { return d.notes; }, notes);
		notes_->setPlainText(same ? notes : QString());
		notes_->setPlaceholderText(same ? tr("Longer text for the reader of the map and of its export: what a "
				"write does, sequences, examples. Markdown is fine.") : tr(SEVERAL));
	}
	loading_ = false;
}

/* The LIVE reading: the value as the register is now defined and its unit, the dot (ok, a limit passed, no
 * value), and under it the decoded text ("MODE=run READY") and a limit passed. What does not fit is cut short
 * (a 255-byte register would not fit any panel); the tooltips hold it whole. */
void RegisterEditor::refreshLive() {
	const RegDef *def = uids_.size() == 1 ? doc_->reg(uids_.front()) : nullptr;
	QByteArray raw;
	const bool valid = def && live_ && live_(def->uid, raw) && raw.size() >= def->size;
	const ThemeColors &colors = Theme::colors();
	auto showState = [this](const QString &state) {
		if (liveDot_->property("state").toString() == state) return;
		liveDot_->setProperty("state", state);
		repolish(liveDot_);
	};
	liveLine_->setToolTip(QString());
	liveDetail_->setToolTip(QString());
	if (!valid) {
		showState(QStringLiteral("none"));
		liveLine_->setText(coloredSpan(QStringLiteral("—"), colors.muted));
		const QString why = uids_.size() > 1 ? tr("a change goes to all of them; live values: select one register")
				: def ? tr("no value: not connected, or not read yet") : QString();
		liveDetail_->setText(coloredSpan(why.toHtmlEscaped(), colors.muted));
		if (def) fields_->setLiveValue(0, false);
		return;
	}
	QString value = formatValue(*def, raw);
	const QString unit = !def->unit.isEmpty() && def->isNumeric() ? QStringLiteral(" ") + def->unit : QString();
	QString decoded = formatDecoded(*def, raw);
	const QString limit = def->isNumeric() ? limitProblem(*def, decodeNumber(*def, raw)) : QString();
	showState(limit.isEmpty() ? QStringLiteral("ok") : QStringLiteral("warn"));

	/* the value, cut short to leave room for its unit */
	liveLine_->setToolTip(value + unit);
	const QFontMetrics valueMetrics(liveLine_->font());
	const int valueRoom = liveLine_->contentsRect().width() - valueMetrics.horizontalAdvance(unit);
	value = valueMetrics.elidedText(value, Qt::ElideRight, std::max(0, valueRoom));
	liveLine_->setText(value.toHtmlEscaped() + coloredSpan(unit.toHtmlEscaped(), colors.muted));

	/* under it: the decoded text, cut short to leave room for a limit passed */
	const QString separator = QStringLiteral("  ·  ");
	const QString limitPart = limit.isEmpty() ? QString() : (decoded.isEmpty() ? limit : separator + limit);
	liveDetail_->setToolTip(decoded + limitPart);
	const QFontMetrics detailMetrics(liveDetail_->font());
	const int detailRoom = liveDetail_->contentsRect().width() - detailMetrics.horizontalAdvance(limitPart);
	decoded = detailMetrics.elidedText(decoded, Qt::ElideRight, std::max(0, detailRoom));
	liveDetail_->setText(coloredSpan(decoded.toHtmlEscaped(), colors.muted)
			+ (limitPart.isEmpty() ? QString() : coloredSpan(limitPart.toHtmlEscaped(), colors.warn)));
	fields_->setLiveValue(def->isNumeric() ? quint64(decodeRaw(*def, raw)) : 0, def->isNumeric());
}

void RegisterEditor::apply(const QString &step, int key, const std::function<void(RegDef &)> &change) {
	if (loading_ || uids_.isEmpty()) return;
	const QVector<quint32> uids = uids_;
	doc_->edit(step, [uids, change](DeviceMap &map) {
		for (RegDef &def : map.regs)
			if (uids.contains(def.uid)) change(def);
	}, key);
}

bool RegisterEditor::readNumber(QLineEdit *box, double &value, bool emptyAllowed) {
	const QString text = box->text().trimmed();
	bool ok = true;
	value = text.isEmpty() ? NO_LIMIT : QLocale::c().toDouble(text, &ok);
	if (text.isEmpty() && !emptyAllowed) ok = false;
	markInvalid(box, !ok);
	if (!ok) emit refused(tr("not a number: \"%1\"").arg(text));
	return ok;
}

void RegisterEditor::markInvalid(QWidget *box, bool invalid) {
	box->setStyleSheet(invalid ? QStringLiteral("border: 1px solid %1;").arg(Theme::colors().bad.name()) : QString());
}
