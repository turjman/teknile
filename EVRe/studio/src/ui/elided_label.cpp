/* SPDX-License-Identifier: Apache-2.0 */
/* The one-line label that cuts its text: see elided_label.h. */
#include "ui/elided_label.h"

#include <QEvent>
#include <QFontMetrics>
#include <QTextDocument>
#include <algorithm>
#include <cmath>

ElidedLabel::ElidedLabel(QWidget *parent) : QLabel(parent) {
	setWordWrap(false);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	setMinimumHeight(fontMetrics().height());
}

void ElidedLabel::setFullText(const QString &text, const QString &toolTip, const QString &shorter) {
	full_ = text;
	toolTip_ = toolTip;
	shorter_ = shorter;
	fit();
}

void ElidedLabel::setElideMode(Qt::TextElideMode mode) {
	mode_ = mode;
	fit();
}

void ElidedLabel::resizeEvent(QResizeEvent *event) {
	QLabel::resizeEvent(event);
	fit();
}

void ElidedLabel::changeEvent(QEvent *event) {
	QLabel::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
		setMinimumHeight(fontMetrics().height());
		fit();
	}
}

/* The width the text takes, rich text measured as drawn (bold is wider); then the text as it is, or cut. The room is
 * the contents rectangle: the style sheet's padding (the status bar's labels have 8 px a side) is already out of it. */
void ElidedLabel::fit() {
	const bool rich = textFormat() == Qt::RichText || (textFormat() == Qt::AutoText && Qt::mightBeRichText(full_));
	QString plain = full_;
	int width = 0;
	if (rich) {
		QTextDocument document;
		document.setDefaultFont(font());
		document.setDocumentMargin(0);
		document.setHtml(full_);
		plain = document.toPlainText();
		width = int(std::ceil(document.idealWidth()));
	} else {
		width = fontMetrics().horizontalAdvance(full_);
	}
	const int room = std::max(0, contentsRect().width() - 2 * margin());
	cut_ = width > room && !full_.isEmpty();
	setToolTip(toolTip_.isEmpty() ? plain : toolTip_);
	if (!cut_) {
		QLabel::setText(full_);
		return;
	}
	/* the shorter text whole when it fits; cut only when even it does not */
	if (!shorter_.isEmpty() && fontMetrics().horizontalAdvance(shorter_) <= room) {
		QLabel::setText(shorter_);
		return;
	}
	const QString shown = fontMetrics().elidedText(shorter_.isEmpty() ? plain : shorter_, mode_, room);
	/* cut rich text stays rich (a span), so an escaped "&amp;" is drawn as "&" */
	QLabel::setText(rich ? QStringLiteral("<span>%1</span>").arg(shown.toHtmlEscaped()) : shown);
}
