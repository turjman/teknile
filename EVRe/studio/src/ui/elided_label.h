/* SPDX-License-Identifier: Apache-2.0 */
/* A label of one line that never asks for more room than the layout gives it
 * (QSizePolicy::Ignored across): a text longer than its room is cut ("...")
 * and the whole of it is in the tooltip. Rich text is shown as it is while it
 * fits, and as plain text cut when it does not. It is one line tall even when
 * empty, so a text that comes, goes or grows moves nothing around it. The
 * status bar's hint, the Map editor's banner, the Devices card's line, the
 * problems in the bus dialogs and the link state pill are ones. */
#pragma once

#include <QLabel>
#include <QString>

class ElidedLabel : public QLabel {
	Q_OBJECT
public:
	explicit ElidedLabel(QWidget *parent = nullptr);
	/* the text, cut to the room there is; toolTip: what the tooltip says (empty: the whole text, as plain text).
	 * shorter: shown whole in place of a text that does not fit, before anything is cut (the pill's address without
	 * "Connected · ": an address is never shown cut while it fits alone) */
	void setFullText(const QString &text, const QString &toolTip = QString(), const QString &shorter = QString());
	QString fullText() const { return full_; }
	bool isCut() const { return cut_; } /* the text shown is cut: it did not fit */
	/* where a text too long is cut: at the end (the default), or in the middle (both ends stay: the pill's
	 * "Connected · host:port" keeps its port) */
	void setElideMode(Qt::TextElideMode mode);

protected:
	void resizeEvent(QResizeEvent *event) override;
	void changeEvent(QEvent *event) override; /* a new font (the style sheet): one line of it, and cut again */

private:
	void fit();
	QString full_, toolTip_, shorter_;
	bool cut_ = false;
	Qt::TextElideMode mode_ = Qt::ElideRight;
};
