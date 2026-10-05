/* SPDX-License-Identifier: Apache-2.0 */
/* The Monitor tab: see monitor_tab.h. */
#include "ui/monitor_tab.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

#include "evre/frame.h"
#include "evre/registers.h"
#include "model/register_model.h"
#include "ui/limit_spin_box.h"
#include "ui/ui_helpers.h"

MonitorTab::MonitorTab(const RegisterModel *model, QWidget *parent) : QWidget(parent), model_(model) {
	/* as the sidebar's Slave: a number past the range is kept as typed, the edge amber, until Enter */
	slave_ = new LimitSpinBox;
	slave_->setObjectName(QStringLiteral("monitorSlave"));
	const QString slaveHelp = tr("The device the request goes to. 0 is the broadcast address: every device takes a "
			"WRITE (no acknowledge) sent there, and none answers.");
	slave_->setHelp(slaveHelp);
	slave_->setLimits(0, 255);
	slave_->setValue(1);
	slave_->setSpecialValueText(tr("0 (broadcast)"));
	device_ = new QComboBox;
	device_->setObjectName(QStringLiteral("monitorDevice"));
	device_->setToolTip(slaveHelp);
	device_->hide();
	connect(device_, &QComboBox::activated, this, [this] { slave_->setValue(device_->currentData().toInt()); });
	function_ = new QComboBox;
	function_->setObjectName(QStringLiteral("monitorFunction"));
	function_->addItem(tr("READ"), int(evre::READ));
	function_->addItem(tr("WRITE + ack"), int(evre::WRITE_ACK));
	function_->addItem(tr("WRITE (no ack)"), int(evre::WRITE));
	address_ = new QLineEdit(addrText(evre::DEVICE_ID));
	address_->setFixedWidth(90);
	argument_ = new QLineEdit(QStringLiteral("2"));
	argument_->setMaximumWidth(480); /* room for a few bytes; it took the whole width for READ's count */
	auto *sendButton = new QPushButton(tr("Send"));
	sendButton->setObjectName(QStringLiteral("primary"));
	auto *logFrames = new QCheckBox(tr("Log frames"));
	logFrames->setToolTip(tr("Every frame sent and received. Off by default: at fast polling it is a lot of text."));
	auto *clearButton = new QPushButton(tr("Clear"));
	frames_ = new QPlainTextEdit;
	/* frames, hex and addresses read left to right in any language */
	for (QWidget *w : std::initializer_list<QWidget *>{ frames_, address_, argument_ }) w->setLayoutDirection(Qt::LeftToRight);
	frames_->setReadOnly(true);
	frames_->setMaximumBlockCount(5000);
	frames_->setFont(monospaceFont());
	/* empty: what shows here, and how to get it */
	frames_->setPlaceholderText(tr("Nothing yet. Send a request above, or tick Log frames to see every frame of the "
			"polling."));
	/* each box says what it takes; the second one's name and hint follow the function (count / bytes). READ's
	 * count left as it started ("2") is no value to write: a WRITE starts it empty, with its hint */
	argumentLabel_ = mutedLabel(QString());
	auto showArgument = [this] {
		const bool read = function_->currentData().toInt() == int(evre::READ);
		argumentLabel_->setText(read ? tr("Count") : tr("Bytes"));
		argument_->setPlaceholderText(read ? tr("bytes to read, e.g. 2")
				: tr("the value as hex bytes, low byte first: 2C 01 = 300 (0x012C)"));
		if (!read && argument_->text() == QLatin1String("2")) argument_->clear();
		if (read && argument_->text().isEmpty()) argument_->setText(QStringLiteral("2"));
		argument_->setToolTip(argument_->placeholderText());
	};
	showArgument();
	connect(function_, &QComboBox::currentIndexChanged, this, showArgument);
	/* the broadcast address takes a WRITE without acknowledge only: the function follows, and a device chosen again
	 * gets the function chosen before */
	connect(slave_, &QSpinBox::valueChanged, this, [this](int slave) {
		const bool broadcast = slave == evre::BROADCAST;
		if (broadcast && function_->isEnabled()) {
			functionBeforeBroadcast_ = function_->currentIndex();
			function_->setCurrentIndex(function_->findData(int(evre::WRITE)));
		} else if (!broadcast && !function_->isEnabled()) {
			function_->setCurrentIndex(functionBeforeBroadcast_);
		}
		function_->setEnabled(!broadcast);
		const QSignalBlocker blocker(device_);
		device_->setCurrentIndex(device_->findData(slave));
	});

	auto *bar = new QHBoxLayout;
	bar->addWidget(mutedLabel(tr("Slave")));
	bar->addWidget(slave_);
	bar->addWidget(device_);
	bar->addSpacing(6);
	bar->addWidget(function_);
	bar->addSpacing(6);
	bar->addWidget(mutedLabel(tr("Address")));
	bar->addWidget(address_);
	bar->addSpacing(6);
	bar->addWidget(argumentLabel_);
	bar->addWidget(argument_, 1);
	bar->addWidget(sendButton);
	bar->addStretch(1);
	bar->addSpacing(12);
	bar->addWidget(logFrames);
	bar->addWidget(clearButton);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 12, 0, 0);
	layout->setSpacing(10);
	layout->addLayout(bar);
	layout->addWidget(frames_, 1);

	connect(sendButton, &QPushButton::clicked, this, &MonitorTab::sendRequest);
	connect(argument_, &QLineEdit::returnPressed, this, &MonitorTab::sendRequest);
	connect(address_, &QLineEdit::returnPressed, this, &MonitorTab::sendRequest); /* Enter in either box */
	connect(logFrames, &QCheckBox::toggled, this, &MonitorTab::logFramesToggled);
	connect(clearButton, &QPushButton::clicked, frames_, &QPlainTextEdit::clear);
}

void MonitorTab::addFrames(const QStringList &lines, int dropped) {
	if (lines.isEmpty() && !dropped) return;
	/* all of them in one append: one repaint */
	QString text = lines.join(QLatin1Char('\n'));
	if (dropped) text.prepend(tr("… %1 frames not shown (too many to show)\n").arg(dropped));
	frames_->appendPlainText(text);
}

void MonitorTab::showAnswer(quint16 addr, bool ok, const QByteArray &data, const QString &message, double ms) {
	if (!ok) {
		frames_->appendPlainText(tr("!! %1 %2").arg(addrText(addr), message));
		return;
	}
	const QString bytes = data.isEmpty() ? QString() : QStringLiteral(": ") + evre::hex(data);
	/* a write is not timed: no "(0.0 ms)" */
	frames_->appendPlainText(ms >= 0 ? tr("== %1 OK%2  (%3 ms)").arg(addrText(addr), bytes).arg(ms, 0, 'f', 1)
									 : tr("== %1 OK%2").arg(addrText(addr), bytes));
}

void MonitorTab::setSlave(int slave) {
	if (slave_->value() != evre::BROADCAST) slave_->setValue(slave); /* a broadcast being typed stays one */
}

void MonitorTab::setDevices(const QVector<PickerDevice> &devices) {
	slave_->setVisible(devices.isEmpty());
	device_->setVisible(!devices.isEmpty());
	if (devices.isEmpty()) return;
	const QVector<PickerDevice> rows = devices + QVector<PickerDevice> { { tr("Broadcast · slave 0"),
			evre::BROADCAST, QColor(), tr("A WRITE to slave 0: every device takes it, none answers") } };
	fillDevicePicker(device_, rows, slave_->value());
}

void MonitorTab::showNote(const QString &text) { frames_->appendPlainText(text); }

void MonitorTab::showSent(quint16 addr, const QByteArray &bytes) {
	frames_->appendPlainText(tr("-> %1 sent: %2  (no acknowledge: nothing says it arrived; READ it to see)")
			.arg(addrText(addr), evre::hex(bytes)));
}

bool MonitorTab::parseHexBytes(const QString &text, QByteArray &bytes) {
	bytes.clear();
	static const QRegularExpression separators(QStringLiteral("[\\s,;:]+"));
	static const QRegularExpression hexDigits(QStringLiteral("^[0-9A-Fa-f]+$"));
	for (QString token : text.split(separators, Qt::SkipEmptyParts)) {
		if (token.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) token.remove(0, 2);
		/* one byte ("5", "2C"), or several run together ("2C01"): never an odd run, it has no clear split */
		if (!hexDigits.match(token).hasMatch() || (token.size() > 2 && token.size() % 2 != 0)) return false;
		bytes += QByteArray::fromHex(token.toLatin1().rightJustified(std::max(2, int(token.size())), '0'));
	}
	return !bytes.isEmpty();
}

void MonitorTab::sendRequest() {
	bool ok;
	const QString addressText = address_->text().trimmed();
	const uint addr = parseAddress(addressText, &ok);
	if (!ok || addr > 0xFFFF) {
		frames_->appendPlainText(tr("!! bad address \"%1\"").arg(addressText));
		return;
	}
	const int function = function_->currentData().toInt();
	if (function == evre::READ) {
		const int count = argument_->text().trimmed().toInt(&ok, 0);
		if (!ok || count < 1 || count > 0xFFFF) {
			frames_->appendPlainText(tr("!! READ needs a count"));
			return;
		}
		emit readRequested(quint8(slave_->value()), quint16(addr), quint16(count));
		return;
	}
	QByteArray bytes;
	if (!parseHexBytes(argument_->text(), bytes)) {
		frames_->appendPlainText(argument_->text().trimmed().isEmpty() ? tr("!! WRITE needs hex bytes")
				: tr("!! not hex bytes: \"%1\" (e.g. 2C 01 for 300: the low byte first)").arg(argument_->text().trimmed()));
		return;
	}
	if (!model_->writesEnabled()) {
		frames_->appendPlainText(tr("!! writes are off: tick \"Allow writes\" on the Registers tab"));
		return;
	}
	emit writeRequested(quint8(slave_->value()), quint16(addr), bytes, function == evre::WRITE_ACK);
}
