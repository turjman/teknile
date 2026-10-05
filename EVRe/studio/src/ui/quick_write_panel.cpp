/* SPDX-License-Identifier: Apache-2.0 */
/* The quick-write panel under the register table: see quick_write_panel.h. */
#include "ui/quick_write_panel.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QLocale>
#include <QVBoxLayout>
#include <cmath>
#include <initializer_list>

#include "model/register_model.h"
#include "ui/bit_view.h"
#include "ui/ui_helpers.h"

namespace {

/* a register's raw bytes (little endian) as an unsigned integer */
quint64 rawBits(const QByteArray &raw) {
	quint64 bits = 0;
	for (int i = 0; i < raw.size() && i < 8; i++) bits |= quint64(uint8_t(raw[i])) << (8 * i);
	return bits;
}

bool isSigned(RegType type) { return type == RegType::I8 || type == RegType::I16 || type == RegType::I32; }

/* the register's bits as the text encodeValue takes: negative when the top bit of a signed register is set */
QString bitsText(const RegDef &def, quint64 bits) {
	const int width = 8 * def.size;
	bits &= bitMask(width);
	if (isSigned(def.type) && width < 64 && (bits >> (width - 1)) & 1)
		return QString::number(qint64(bits) - qint64(1ULL << width));
	return QString::number(bits);
}

/* a shown value as the text encodeValue takes: 30, -1, 3.65 */
QString numberText(double value) {
	if (value == std::floor(value) && std::fabs(value) < 1e15) return QString::number(qint64(value));
	return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

/* The list's entries: an enum name's data is its raw number; a special value's is "s" and its shown
 * value (a text, so the two kinds never match each other). listKey: the entry of the value held. */
QVariant listKey(const RegDef &def, const QByteArray &raw) {
	const double shown = decodeNumber(def, raw);
	if (!specialName(def, shown).isEmpty()) return QStringLiteral("s") + numberText(shown);
	return QVariant(qint64(decodeRaw(def, raw)));
}

/* the bit view is for integer registers whose value shown is the raw integer (no scale, no offset) */
bool bitsWritable(const RegDef &def) { return def.isNumeric() && def.type != RegType::F32 && !def.isFloat(); }

} // namespace

QuickWritePanel::QuickWritePanel(const RegisterModel *model, QWidget *parent) : QFrame(parent), model_(model) {
	setObjectName(QStringLiteral("quickWrite"));
	name_ = new QLabel;
	value_ = new QLineEdit;
	value_->setObjectName(QStringLiteral("qwValue"));
	value_->setPlaceholderText(tr("value, 0x1F, 0b101, or a name"));
	value_->setMaximumWidth(260);
	namedValues_ = new QComboBox;
	namedValues_->setObjectName(QStringLiteral("qwEnum"));
	namedValues_->setMinimumWidth(160);
	write_ = new QPushButton(tr("Write"));
	write_->setObjectName(QStringLiteral("primary"));
	write_->setCursor(Qt::PointingHandCursor);
	default_ = new QPushButton(tr("Default"));
	default_->setObjectName(QStringLiteral("qwDefault"));
	broadcast_ = new QPushButton(tr("To all devices"));
	broadcast_->setObjectName(QStringLiteral("qwBroadcast"));
	broadcast_->hide();
	bits_ = new QCheckBox(tr("Bits"));
	bits_->setToolTip(tr("A button for every bit of the register: click one to flip it"));
	bits_->setChecked(QSettings().value(QStringLiteral("ui/quickBits"), false).toBool());
	hint_ = mutedLabel(QString());
	bitArea_ = new QWidget;
	auto *bitLayout = new QVBoxLayout(bitArea_);
	bitLayout->setContentsMargins(0, 0, 0, 0);

	auto *row = new QHBoxLayout;
	row->setSpacing(6);
	row->addWidget(name_);
	row->addWidget(value_, 1);
	row->addWidget(namedValues_);
	row->addWidget(write_);
	row->addWidget(default_);
	row->addWidget(broadcast_);
	row->addSpacing(8);
	row->addWidget(bits_);
	row->addStretch();
	row->addWidget(hint_);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(10, 0, 10, 6);
	layout->setSpacing(6);
	layout->addLayout(row);
	layout->addWidget(bitArea_);

	connect(write_, &QPushButton::clicked, this, &QuickWritePanel::writeTyped);
	connect(value_, &QLineEdit::returnPressed, this, &QuickWritePanel::writeTyped);
	connect(value_, &QLineEdit::textChanged, this, &QuickWritePanel::updateBroadcast);
	connect(namedValues_, &QComboBox::activated, this, &QuickWritePanel::writeNamedValue);
	connect(broadcast_, &QPushButton::clicked, this, [this] {
		const QString text = value_->text().trimmed();
		if (row_ >= 0 && !text.isEmpty()) emit broadcastRequested(row_, text);
	});
	connect(default_, &QPushButton::clicked, this, [this] {
		if (row_ < 0 || row_ >= model_->rows().size()) return;
		const RegDef &def = model_->rows()[row_].def;
		if (def.hasDefault()) requestWrite(numberText(def.defaultValue));
	});
	connect(bits_, &QCheckBox::toggled, this, [this](bool on) {
		QSettings().setValue(QStringLiteral("ui/quickBits"), on);
		rebuild();
	});
	/* new values, and Allow writes switched (the model tells every value cell then) */
	connect(model_, &QAbstractItemModel::dataChanged, this, &QuickWritePanel::onValuesChanged);
	/* a new map: its rows are new, none is selected */
	connect(model_, &QAbstractItemModel::modelReset, this, [this] { showRegister(-1); });
	showRegister(-1);
}

void QuickWritePanel::showRegister(int row) {
	row_ = row;
	rebuild();
}

void QuickWritePanel::setConnected(bool connected) {
	connected_ = connected;
	refresh();
}

void QuickWritePanel::setBroadcastRule(BroadcastRule rule) {
	broadcastRule_ = std::move(rule);
	refresh();
}

void QuickWritePanel::rebuild() {
	delete bitView_;
	bitView_ = nullptr;
	if (row_ < 0 || row_ >= model_->rows().size() || !model_->rows()[row_].def.rw) {
		hide();
		return;
	}
	show();
	const RegDef def = model_->rows()[row_].def;
	const QString dangerMark = def.danger ? QStringLiteral(" ⚠") : QString();
	name_->setText(tr("Write <b>%1</b>%2").arg(def.name.toHtmlEscaped(), dangerMark));
	value_->clear();
	/* the range the map gives, in the box until something is typed */
	QString range;
	const QString unit = def.unit.isEmpty() ? QString() : QStringLiteral(" ") + def.unit;
	if (def.hasMin() || def.hasMax())
		range = tr("%1 … %2%3").arg(def.hasMin() ? numberText(def.min) : QString(),
				def.hasMax() ? numberText(def.max) : QString(), unit);
	value_->setPlaceholderText(range.isEmpty() ? tr("value, 0x1F, 0b101, or a name")
			: tr("value (%1), or a name").arg(range));
	default_->setVisible(def.hasDefault());
	default_->setToolTip(def.hasDefault() ? tr("Write the default: %1%2").arg(numberText(def.defaultValue), unit)
			: QString());
	/* the names: special values first (in shown units), then the enum's */
	namedValues_->clear();
	for (const SpecialValue &special : def.special)
		namedValues_->addItem(QStringLiteral("%1  (%2)").arg(special.name, numberText(special.value)),
				QStringLiteral("s") + numberText(special.value));
	for (auto it = def.enumValues.begin(); it != def.enumValues.end(); ++it)
		namedValues_->addItem(QStringLiteral("%1  (%2)").arg(it.value()).arg(it.key()), it.key());
	namedValues_->setVisible(namedValues_->count() > 0);
	const bool withBits = bitsWritable(def);
	bits_->setVisible(withBits && def.fields.isEmpty()); /* with fields the bits are always shown */
	/* the register as in a datasheet: always for one with fields, with "Bits" for any other integer register */
	if (withBits && (!def.fields.isEmpty() || bits_->isChecked())) {
		bitView_ = new BitView;
		bitView_->setObjectName(QStringLiteral("bitView"));
		bitView_->setRegister(def);
		bitArea_->layout()->addWidget(bitView_);
		connect(bitView_, &BitView::writeField, this, &QuickWritePanel::writeField);
	}
	refresh();
}

void QuickWritePanel::refresh() {
	if (row_ < 0 || row_ >= model_->rows().size()) return;
	const RegisterModel::Row &row = model_->rows()[row_];
	const bool writesAllowed = model_->writesEnabled();
	const bool enabled = writesAllowed && connected_;
	for (QWidget *control : std::initializer_list<QWidget *>{ value_, namedValues_, write_, default_, bitArea_ })
		control->setEnabled(enabled);
	hint_->setText(!writesAllowed ? tr("tick Allow writes to write") : !connected_ ? tr("not connected") : QString());
	updateBroadcast();
	if (!row.valid) return;
	/* the list shows the device's value, except while the user is choosing in it */
	if (!namedValues_->hasFocus() && !namedValues_->view()->isVisible()) {
		const QSignalBlocker blocker(namedValues_);
		namedValues_->setCurrentIndex(namedValues_->findData(listKey(row.def, row.raw)));
	}
	if (bitView_) bitView_->setValue(rawBits(row.raw), true);
}

/* a bus: the same value to every device at once, when the rule allows it for this register, and once a value is
 * typed (there is nothing to send before) */
void QuickWritePanel::updateBroadcast() {
	broadcast_->setVisible(bool(broadcastRule_));
	if (!broadcastRule_ || row_ < 0 || row_ >= model_->rows().size()) return;
	const QString refusal = broadcastRule_(model_->rows()[row_].def);
	const bool typed = !value_->text().trimmed().isEmpty();
	broadcast_->setEnabled(model_->writesEnabled() && connected_ && refusal.isEmpty() && typed);
	broadcast_->setToolTip(!refusal.isEmpty() ? tr("No broadcast: %1").arg(refusal)
			: typed ? tr("The value typed, to every device on the link in one broadcast frame (slave 0). No device "
						 "answers a broadcast: each one is read back afterwards.")
					: tr("Type a value first: it goes to every device on the link in one broadcast frame (slave 0)."));
}

void QuickWritePanel::onValuesChanged(const QModelIndex &first, const QModelIndex &last) {
	if (row_ >= first.row() && row_ <= last.row()) refresh();
}

/* ------------------------------------------------------------------ writing */

void QuickWritePanel::writeTyped() {
	const QString text = value_->text().trimmed();
	if (row_ < 0 || text.isEmpty()) return;
	requestWrite(text);
}

void QuickWritePanel::writeNamedValue(int listIndex) {
	if (row_ < 0) return;
	/* "s3.5": a special value, in shown units; a number: an enum's raw value */
	const QVariant data = namedValues_->itemData(listIndex);
	if (data.typeId() == QMetaType::QString) requestWrite(data.toString().mid(1));
	else if (data.toLongLong() >= 0) requestWrite(QStringLiteral("0x%1").arg(quint64(data.toLongLong()), 0, 16));
	else requestWrite(QString::number(data.toLongLong())); /* a negative raw value: as a number */
}

/* `width` bits from `lsb` up become value: the rest is what the device holds now (nothing read yet: no write) */
void QuickWritePanel::writeField(int lsb, int width, quint64 value) {
	if (row_ < 0) return;
	const RegisterModel::Row &row = model_->rows()[row_];
	if (!row.valid) return;
	const quint64 mask = bitMask(width) << lsb;
	requestWrite(bitsText(row.def, (rawBits(row.raw) & ~mask) | ((value << lsb) & mask)));
}

void QuickWritePanel::requestWrite(const QString &text) {
	const RegisterModel::Row &row = model_->rows()[row_];
	emit writeRequested(row_, text, row.valid ? row.raw : QByteArray());
	refresh(); /* refused or cancelled: the controls show the device's value again */
}
