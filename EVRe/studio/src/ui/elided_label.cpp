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

void ElidedLabel::setWraps(bool wraps) {
	if (wraps == wraps_) return;
	wraps_ = wraps;
	fit();
}

void ElidedLabel::setLines(int lines) {
	lines_ = std::max(1, lines);
	setWordWrap(lines_ > 1);
	if (lines_ == 1) setMaximumHeight(QWIDGETSIZE_MAX);
	keepHeight();
	fit();
}

/* the lines' height as the label lays them out in its own font (the style sheet sets it when the label is polished, and
 * again at a theme switch): counted before, it was a pixel or two short of what the font then took */
void ElidedLabel::keepHeight() {
	if (lines_ == 1) {
		setMinimumHeight(fontMetrics().height());
		return;
	}
	QStringList rows;
	for (int i = 0; i < lines_; i++) rows << QStringLiteral("x");
	const QMargins margins = contentsMargins();
	setFixedHeight(fontMetrics().boundingRect(QRect(0, 0, 1000, 1000), Qt::AlignLeft, rows.join(QLatin1Char('\n'))).height()
			+ margins.top() + margins.bottom() + 2 * margin());
}

/* laid out as QLabel lays out a wrapped text: within the room across, and in no more lines than the label has */
bool ElidedLabel::fitsLines(const QString &text, int room) const {
	if (room <= 0) return false;
	const QRect needs = fontMetrics().boundingRect(QRect(0, 0, room, 100000), Qt::AlignLeft | Qt::TextWordWrap, text);
	return needs.width() <= room && needs.height() <= contentsRect().height() - 2 * margin();
}

/* wrapped, the label's height follows its width (QLabel's height for width), and it may be taller than one line. Only
 * a text that does not fit is wrapped: a wrapped label of one line can be a pixel or two taller than an unwrapped one
 * (Linux's fonts), and a short reason must not move the card */
void ElidedLabel::wrapLines(bool on) {
	if (on == wordWrap()) return;
	setWordWrap(on);
	QSizePolicy policy(QSizePolicy::Ignored, on ? QSizePolicy::Preferred : QSizePolicy::Fixed);
	policy.setHeightForWidth(on);
	setSizePolicy(policy);
}

void ElidedLabel::resizeEvent(QResizeEvent *event) {
	QLabel::resizeEvent(event);
	fit();
}

void ElidedLabel::changeEvent(QEvent *event) {
	QLabel::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
		keepHeight();
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
	setToolTip(toolTip_.isEmpty() ? plain : toolTip_);
	if (lines_ > 1 && !rich) {
		/* whole on the lines there are, or as many whole words as fit with "…" after them */
		cut_ = !full_.isEmpty() && !fitsLines(full_, room);
		QString shown = full_;
		if (cut_) {
			shown = QString(QChar(0x2026));
			for (int at = int(full_.lastIndexOf(QLatin1Char(' '))); at > 0; at = int(full_.lastIndexOf(QLatin1Char(' '), at - 1))) {
				const QString part = full_.left(at).trimmed() + QChar(0x2026);
				if (fitsLines(part, room)) {
					shown = part;
					break;
				}
			}
		}
		QLabel::setText(shown);
		return;
	}
	cut_ = width > room && !full_.isEmpty();
	wrapLines(wraps_ && cut_ && !rich);
	if (!cut_) {
		QLabel::setText(full_);
		return;
	}
	if (wraps_ && !rich) {
		/* whole, on as many lines as it takes: the gist alone on the first when there is one */
		cut_ = false;
		const int colon = int(full_.indexOf(QLatin1String(": ")));
		QLabel::setText(colon > 0 ? full_.left(colon + 1) + QLatin1Char('\n') + full_.mid(colon + 2) : full_);
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
