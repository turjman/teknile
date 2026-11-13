/* SPDX-License-Identifier: Apache-2.0 */
/* The help pages (help_topics.cpp, shared with the phone's Help screen), styled with the theme's colours each time a
 * page is shown (so they follow a theme switch). */
#include "ui/help_dialog.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>

#include "ui/help_topics.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

HelpDialog::HelpDialog(QWidget *parent) : QDialog(parent) {
	setWindowTitle(tr("EVRe Studio — Help"));
	resize(980, 700);
	topics_ = new QListWidget;
	keepCurrentInView(topics_);
	topics_->setObjectName(QStringLiteral("helpTopics")); /* a card as the page beside it (theme) */
	topics_->setFixedWidth(250); /* the longest title and the card's padding: no scroll bar under it */
	topics_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	for (int i = 0; i < help::topicCount(); ++i) topics_->addItem(help::title(i));
	page_ = new QTextBrowser;
	page_->setOpenExternalLinks(true);

	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(14);
	layout->addWidget(topics_);
	layout->addWidget(page_, 1);

	connect(topics_, &QListWidget::currentRowChanged, this, &HelpDialog::showTopic);
	topics_->setCurrentRow(0);
}

void HelpDialog::showTopic(int index) {
	if (index < 0 || index >= help::topicCount()) return;
	page_->setHtml(help::page(index, Theme::colors()));
	/* right to left (Arabic): every paragraph so, also one that starts with a Latin word ("RAM: …", which the text
	 * would lay out left to right by itself); the code blocks stay left to right */
	if (!isRightToLeft()) return;
	QTextCursor cursor(page_->document());
	for (QTextBlock block = page_->document()->begin(); block.isValid(); block = block.next()) {
		QTextBlockFormat format = block.blockFormat();
		format.setLayoutDirection(format.nonBreakableLines() ? Qt::LeftToRight : Qt::RightToLeft);
		cursor.setPosition(block.position());
		cursor.setBlockFormat(format);
	}
}
