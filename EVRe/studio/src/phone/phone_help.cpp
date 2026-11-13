/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's Help: see phone_help.h. */
#include "phone/phone_help.h"

#include <QGuiApplication>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include "ui/help_topics.h"
#include "ui/theme.h"

PhoneHelp::PhoneHelp(QObject *parent) : QObject(parent) {}

QStringList PhoneHelp::titles() const {
	QStringList list;
	for (int i = 0; i < help::topicCount(); ++i) list << help::title(i);
	return list;
}

QString PhoneHelp::page(int index, bool dark) const {
	const QString html = help::page(index, Theme::colorsFor(dark));
	if (html.isEmpty() || QGuiApplication::layoutDirection() != Qt::RightToLeft) return html;
	/* right to left: each block so, the code blocks left to right (HelpDialog::showTopic), carried by the HTML */
	QTextDocument document;
	document.setHtml(html);
	QTextCursor cursor(&document);
	for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
		QTextBlockFormat format = block.blockFormat();
		format.setLayoutDirection(format.nonBreakableLines() ? Qt::LeftToRight : Qt::RightToLeft);
		cursor.setPosition(block.position());
		cursor.setBlockFormat(format);
	}
	/* the document's own font in the body's style would set the page's size: the Text's font sets it, as in English */
	QString out = document.toHtml();
	static const QRegularExpression bodyFont(QStringLiteral("(<body[^>]*?) font-family:[^;]*; font-size:[^;]*;"));
	out.replace(bodyFont, QStringLiteral("\\1"));
	return out;
}
