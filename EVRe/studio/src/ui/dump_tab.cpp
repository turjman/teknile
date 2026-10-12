/* SPDX-License-Identifier: Apache-2.0 */
/* The Dump tab: see dump_tab.h. */
#include "ui/dump_tab.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QVBoxLayout>
#include <algorithm>

#include "model/register_model.h"
#include "ui/elided_label.h"

namespace {

const QString COLOUR_KEY = QStringLiteral("dump/colour");
const QString COLOUR_PLOTTED = QStringLiteral("plotted");
const QString COLOUR_ALL = QStringLiteral("all");
const QString FIELDS_KEY = QStringLiteral("dump/fields");

} // namespace

DumpTab::DumpTab(RegisterModel *model, QWidget *parent) : QWidget(parent), model_(model) {
	view_ = new DumpView;
	view_->setObjectName(QStringLiteral("dumpView"));

	deviceLabel_ = mutedLabel(tr("Device"));
	device_ = new QComboBox;
	device_->setObjectName(QStringLiteral("dumpDevice"));
	device_->setToolTip(tr("The device whose reads the Dump shows: one device at a time"));
	deviceLabel_->hide();
	device_->hide();
	connect(device_, &QComboBox::activated, this, [this] {
		setSlave(device_->currentData().toInt());
		emit slaveChosen(slave_);
	});

	/* Follow or Hold: a segmented pair, as Follow is the usual state and Hold the exception */
	followButton_ = segmentButton(tr("Follow"), "first");
	followButton_->setObjectName(QStringLiteral("dumpFollow"));
	followButton_->setToolTip(tr("Follow: every card shows its block's latest reply, at the pace of Show values"));
	holdButton_ = segmentButton(tr("Hold"), "last");
	holdButton_->setObjectName(QStringLiteral("dumpHold"));
	holdButton_->setToolTip(tr("Hold: the cards keep the bytes they show now (an amber stripe and a banner say they are "
			"not live); Follow shows the replies again"));
	auto *followGroup = new QButtonGroup(this);
	followGroup->addButton(followButton_);
	followGroup->addButton(holdButton_);
	followButton_->setChecked(true);
	connect(holdButton_, &QPushButton::toggled, this, [this](bool hold) { setFollow(!hold); });

	/* Colour: every register its own colour (the chips tell register from register), or only the plotted ones in
	 * their chart colours (what is on the chart stands out) */
	allColoursButton_ = segmentButton(tr("All registers"), "first");
	allColoursButton_->setObjectName(QStringLiteral("dumpColourAll"));
	allColoursButton_->setToolTip(tr("Every register's chips in a colour of its own; a plotted one in its chart "
			"colour"));
	plottedColoursButton_ = segmentButton(tr("Plotted"), "last");
	plottedColoursButton_->setObjectName(QStringLiteral("dumpColourPlotted"));
	plottedColoursButton_->setToolTip(tr("Only the plotted registers in colour (their chart colours); the others "
			"grey"));
	auto *colourGroup = new QButtonGroup(this);
	colourGroup->addButton(allColoursButton_);
	colourGroup->addButton(plottedColoursButton_);
	const bool plotted = QSettings().value(COLOUR_KEY, COLOUR_ALL).toString() == COLOUR_PLOTTED;
	(plotted ? plottedColoursButton_ : allColoursButton_)->setChecked(true);
	view_->setColourMode(plotted ? DumpView::PlottedOnly : DumpView::AllRegisters);
	connect(plottedColoursButton_, &QPushButton::toggled, this, [this](bool on) {
		view_->setColourMode(on ? DumpView::PlottedOnly : DumpView::AllRegisters);
		QSettings().setValue(COLOUR_KEY, on ? COLOUR_PLOTTED : COLOUR_ALL);
	});

	/* Fields: the frames broken down for those who are learning the frame; off, each frame one line for those who
	 * know it (the names still in the bytes' tooltips). One choice for the tab, saved: the cards stay alike */
	fields_ = new QCheckBox(tr("Fields"));
	fields_->setObjectName(QStringLiteral("dumpFields"));
	fields_->setToolTip(tr("The frame's fields named under their bytes; off: each frame on one line, the names in the "
			"bytes' tooltips"));
	fields_->setCursor(Qt::PointingHandCursor);
	fields_->setChecked(QSettings().value(FIELDS_KEY, true).toBool());
	view_->setFields(fields_->isChecked());
	connect(fields_, &QCheckBox::toggled, this, [this](bool on) {
		view_->setFields(on);
		QSettings().setValue(FIELDS_KEY, on);
	});

	/* the whole tab's reason the cards are not live, said once over them (each card's own reasons are its stripe
	 * and its label) */
	banner_ = new ElidedLabel;
	banner_->setObjectName(QStringLiteral("dumpBanner"));
	auto *bannerLine = new QFrame; /* a line in the same amber under it: the banner reads apart from the cards */
	bannerLine->setObjectName(QStringLiteral("dumpBannerLine"));
	bannerLine->setFixedHeight(1);
	bannerBox_ = new QWidget;
	auto *bannerLayout = new QVBoxLayout(bannerBox_);
	bannerLayout->setContentsMargins(0, 0, 0, 0);
	bannerLayout->setSpacing(6);
	bannerLayout->addWidget(banner_);
	bannerLayout->addWidget(bannerLine);
	bannerBox_->hide();

	/* the right-click's items on a button: the selected register's, and its card's (or the first card in view) */
	copyButton_ = new QPushButton(tr("Copy"));
	copyButton_->setObjectName(QStringLiteral("dumpCopy"));
	copyButton_->setCursor(Qt::PointingHandCursor);
	copyButton_->setToolTip(tr("The selected register and its block: Show in Registers, Plot, Write…, and the bytes "
			"or the decoded values to the clipboard. The same as a right-click on the cards."));
	auto *copyMenu = new QMenu(copyButton_);
	copyMenu->setToolTipsVisible(true);
	setButtonMenu(copyButton_, copyMenu);
	connect(copyMenu, &QMenu::aboutToShow, this, [this, copyMenu] {
		copyMenu->clear();
		fillMenu(copyMenu, view_->selected(), view_->currentCard());
	});

	connect(view_, &DumpView::registerPicked, this, &DumpTab::registerPicked);
	connect(view_, &DumpView::menuRequested, this, [this](RegKey key, int card, const QPoint &globalPos) {
		QMenu menu(this);
		menu.setToolTipsVisible(true);
		fillMenu(&menu, key, card);
		menu.exec(globalPos);
	});

	auto *bar = new QHBoxLayout;
	bar->addWidget(deviceLabel_);
	bar->addWidget(device_);
	bar->addSpacing(6);
	auto *followPair = new QHBoxLayout;
	followPair->setSpacing(0);
	followPair->addWidget(followButton_);
	followPair->addWidget(holdButton_);
	bar->addLayout(followPair);
	bar->addSpacing(12);
	bar->addWidget(mutedLabel(tr("Colour")));
	auto *colourPair = new QHBoxLayout;
	colourPair->setSpacing(0);
	colourPair->addWidget(allColoursButton_);
	colourPair->addWidget(plottedColoursButton_);
	bar->addLayout(colourPair);
	bar->addSpacing(12);
	bar->addWidget(fields_);
	bar->addStretch(1);
	bar->addWidget(copyButton_);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 12, 0, 0);
	layout->setSpacing(10);
	layout->addLayout(bar);
	layout->addWidget(bannerBox_);
	layout->addWidget(view_, 1);
	setTabOrder(copyButton_, view_); /* the keys work the cards after the toolbar */
	updateEmptyText();
}

void DumpTab::setShown(bool shown) {
	if (shown == shown_) return;
	shown_ = shown;
	/* hidden, the engine keeps nothing and the cards go: shown again, they come with the next replies, never as old
	 * bytes that look new */
	if (!shown) {
		engineBlocks_.clear();
		fastBlocks_.clear();
	}
	compose();
	emit dumpWanted(shown);
}

void DumpTab::applyFastDumps(const QVector<DumpBlock> &blocks) {
	QVector<DumpBlock> kept;
	for (const DumpBlock &b : blocks) {
		const auto same = std::find_if(fastBlocks_.begin(), fastBlocks_.end(),
				[&b](const DumpBlock &was) { return was.stream == b.stream && was.version == b.version; });
		kept << (same != fastBlocks_.end() ? *same : b);
	}
	fastBlocks_ = kept;
	compose();
}

/* the same streams again (the map's info shown again) keep their blocks; another layout's would be read wrongly */
void DumpTab::setStreams(const QVector<StreamDef> &streams) {
	auto layout = [](const QVector<StreamDef> &list) {
		QStringList out;
		for (const StreamDef &s : list) {
			QStringList channels;
			for (const StreamChannel &c : s.channels)
				channels << c.name + QLatin1Char(' ') + typeName(c.type) + QLatin1Char(' ') + QString::number(c.scale)
								+ QLatin1Char(' ') + QString::number(c.offset) + QLatin1Char(' ') + c.unit
								+ QLatin1Char(' ') + QString::number(c.decimals);
			out << s.name + QLatin1Char(' ') + QString::number(s.addr) + QLatin1Char(' ') + QString::number(s.size)
							+ QLatin1Char(' ') + channels.join(QLatin1Char(','));
		}
		return out;
	};
	if (layout(streams) == layout(streams_)) return;
	streams_ = streams;
	fastBlocks_.clear();
	view_->setStreams(streams);
	compose();
}

void DumpTab::applyDumps(const QVector<DumpBlock> &blocks) {
	engineBlocks_ = blocks;
	compose();
}

void DumpTab::setStreamNotes(const QVector<DumpBlock> &notes) {
	auto same = [](const QVector<DumpBlock> &a, const QVector<DumpBlock> &b) {
		if (a.size() != b.size()) return false;
		for (int i = 0; i < a.size(); i++)
			if (a[i].addr != b[i].addr || a[i].stream != b[i].stream) return false;
		return true;
	};
	if (same(notes, streamNotes_)) return;
	streamNotes_ = notes;
	compose();
}

void DumpTab::showManualRead(const DumpBlock &block) {
	manual_ = block;
	manual_.kind = DumpBlock::Manual;
	hasManual_ = true;
	compose();
	view_->verticalScrollBar()->setValue(0); /* the card at the top, where the eye goes after Send */
}

void DumpTab::tick() {
	if (shown_ && following()) view_->viewport()->update();
}

void DumpTab::setFollow(bool follow) {
	followButton_->setChecked(follow);
	holdButton_->setChecked(!follow);
	view_->setHold(!follow);
	updateBanner();
}

void DumpTab::setConnected(bool connected) {
	connected_ = connected;
	view_->setConnected(connected);
	updateEmptyText();
	updateBanner();
}

void DumpTab::setDevices(const QVector<PickerDevice> &devices) {
	bus_ = !devices.isEmpty();
	deviceLabel_->setVisible(bus_);
	device_->setVisible(bus_);
	if (bus_) fillDevicePicker(device_, devices, slave_);
	compose();
}

void DumpTab::setSlave(int slave) {
	if (bus_) {
		const int index = device_->findData(slave);
		if (index >= 0) device_->setCurrentIndex(index);
	}
	if (slave == slave_) return;
	slave_ = slave;
	compose();
}

void DumpTab::showRegister(RegKey key) {
	if (bus_) setSlave(regKeySlave(key));
	view_->select(key);
	view_->scrollToRegister(key);
}

/* The cards in their order: the manual read and the last write at the top (what the user asked for), then the blocks
 * of the device shown and the fast streams' by address: each stream's newest block (stopped when its stream is no
 * longer switched on), or its one line while it has sent none. */
void DumpTab::compose() {
	QVector<DumpBlock> fast;
	for (DumpBlock b : fastBlocks_) {
		b.stopped = std::none_of(streamNotes_.begin(), streamNotes_.end(), [&b](const DumpBlock &on) { return on.stream == b.stream; });
		fast << b;
	}
	for (const DumpBlock &note : streamNotes_)
		if (std::none_of(fastBlocks_.begin(), fastBlocks_.end(), [&note](const DumpBlock &b) { return b.stream == note.stream; }))
			fast << note;
	QVector<DumpBlock> blocks;
	for (const DumpBlock &b : engineBlocks_ + fast)
		if (!bus_ || b.slave == slave_) blocks << b;
	std::stable_sort(blocks.begin(), blocks.end(), [](const DumpBlock &a, const DumpBlock &b) {
		return std::make_pair(a.kind != DumpBlock::Write, a.addr) < std::make_pair(b.kind != DumpBlock::Write, b.addr);
	});
	if (hasManual_) blocks.prepend(manual_);
	view_->setBlocks(blocks);
	updateEmptyText();
	updateBanner();
}

void DumpTab::updateEmptyText() {
	view_->setEmptyText(connected_
			? tr("Nothing read yet. Each block the polling reads shows here as a card: the request, the reply and its "
				 "bytes, decoded by the map. A READ sent from the Monitor shows here too.")
			: tr("Not connected. Once the link is up, each block the polling reads shows here as a card: the request, "
				 "the reply and its bytes, decoded by the map."));
}

QString DumpTab::bannerText() const {
	if (view_->blocks().isEmpty()) return {}; /* no card: the empty view says what there is */
	if (view_->hold())
		return tr("Held at %1 · the cards show the bytes of that moment; Follow shows the replies again")
				.arg(view_->heldAt());
	if (!connected_) return tr("The link is closed · the cards show the last bytes read");
	return {};
}

void DumpTab::updateBanner() {
	const QString text = bannerText();
	banner_->setFullText(text);
	bannerBox_->setVisible(!text.isEmpty());
}

int DumpTab::rowOf(RegKey key) const {
	const QVector<RegisterModel::Row> &rows = model_->rows();
	for (int i = 0; i < rows.size(); i++)
		if (regKey(rows[i].def) == key) return i;
	return -1;
}

/* The register's items first (when there is one), then the card's. Every item is there even when it cannot be used
 * now, disabled with the reason in its tooltip, so the menu always reads the same. */
void DumpTab::fillMenu(QMenu *menu, RegKey key, int card) {
	const int row = key == DumpView::NO_REGISTER ? -1 : rowOf(key);
	const RegisterModel::Row *reg = row >= 0 ? &model_->rows()[row] : nullptr;
	/* a fast stream's block has no register to act on: its items say so */
	const bool fastCard = card >= 0 && card < view_->blocks().size() && view_->blocks()[card].kind == DumpBlock::FastStream;
	const QString none = fastCard && !reg ? tr("A fast block has records, not registers")
										  : tr("Click a register's chip or decode line first");

	QAction *show = menu->addAction(tr("Show in Registers"), this, [this, key] { emit registerPicked(key, true); });
	show->setEnabled(reg != nullptr);
	show->setToolTip(reg ? tr("The Registers tab, on %1").arg(reg->def.name) : none);

	const bool plotted = reg && reg->plot;
	QAction *plot = menu->addAction(plotted ? tr("Unplot") : tr("Plot"), this,
			[this, row, plotted] { model_->setPlot(row, !plotted); });
	plot->setEnabled(reg && reg->def.canPlot());
	plot->setToolTip(!reg ? none : reg->def.canPlot() ? tr("%1 on or off the chart").arg(reg->def.name)
			: tr("%1 is not a number the map lets plot").arg(reg->def.name));

	QAction *write = menu->addAction(tr("Write…"), this, [this, key] { emit writeRequested(key); });
	const bool editable = reg && (model_->flags(model_->index(row, RegisterModel::ColValue)) & Qt::ItemIsEditable);
	write->setEnabled(editable);
	write->setToolTip(!reg ? none
			: !reg->def.rw ? tr("%1 is read only").arg(reg->def.name)
			: !model_->writesEnabled() ? tr("Tick Allow writes on the Registers tab first")
			: tr("The Registers tab's editor on %1").arg(reg->def.name));
	menu->addSeparator();

	auto copy = [this, menu](const QString &text, DumpView::CopyKind kind, int card, RegKey key, const QString &tip) {
		const QString value = view_->copyText(kind, card, key);
		QAction *action = menu->addAction(text, this, [value] { QGuiApplication::clipboard()->setText(value); });
		action->setEnabled(!value.isEmpty());
		action->setToolTip(value.isEmpty() ? tip : value.section(QLatin1Char('\n'), 0, 3));
		return action;
	};
	const int keyCard = view_->cardOf(key);
	const QString noCard = tr("No reply to copy yet");
	copy(tr("Copy register bytes"), DumpView::CopyRegisterBytes, keyCard, key, reg ? noCard : none);
	copy(tr("Copy block as hex"), DumpView::CopyBlockHex, card, DumpView::NO_REGISTER, noCard);
	copy(tr("Copy block as C array"), DumpView::CopyBlockC, card, DumpView::NO_REGISTER, noCard);
	QAction *decoded = copy(tr("Copy decoded"), DumpView::CopyDecoded, card, DumpView::NO_REGISTER, noCard);
	/* one meaning of "decoded" on a fast card: its records, as Copy records as CSV gives them */
	if (fastCard && decoded->isEnabled())
		decoded->setToolTip(tr("The records as CSV, as Copy records as CSV gives them:") + QLatin1Char('\n')
				+ view_->copyText(DumpView::CopyDecoded, card).section(QLatin1Char('\n'), 0, 2));
	copy(tr("Copy records as CSV"), DumpView::CopyRecordsCsv, card, DumpView::NO_REGISTER,
			card >= 0 && !fastCard ? tr("A read has registers: Copy decoded") : noCard);
}
