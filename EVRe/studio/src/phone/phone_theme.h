/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's theme for QML (PhoneTheme, a singleton): the colours of Theme::colorsFor, the desktop's table, and
 * `dark`, saved in "ui/dark" as the desktop saves it. Main.qml colours Material from it (theme, accent, background),
 * and every screen takes its colours from here, so both looks keep the desktop's contrast (theme.h).
 * The soft tones (accentSoft, goodSoft, warnSoft, badSoft) are the phone's own, made here from the table and nowhere
 * else: a state's colour laid over the surface at a small share, behind a chip, the link's pill, the chosen tab and the
 * write sheet's question. They are mixed, not see-through, so a text on them keeps a known contrast (the state's
 * colour, or the text colour, on them stays at 4.5:1 or more in both looks); the desktop has no such tone.
 * monoFamily: a fixed-pitch family the font database has (the Monitor's bytes in columns). Android's "monospace" is an
 * alias of its fonts.xml that Qt does not read: asked by that name, a Samsung phone drew Roboto, its "1"s narrow and
 * the bytes out of line. So the files' own names first (Droid Sans Mono, Cutive Mono, ...), then the platform's fixed
 * font (QFontDatabase::FixedFont). */
#pragma once

#include <QColor>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "ui/theme.h"

class PhoneTheme : public QObject {
	Q_OBJECT
	QML_ELEMENT
	QML_SINGLETON
	Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY changed)
	Q_PROPERTY(QColor bg READ bg NOTIFY changed)
	Q_PROPERTY(QColor surface READ surface NOTIFY changed)
	Q_PROPERTY(QColor surface2 READ surface2 NOTIFY changed)
	Q_PROPERTY(QColor border READ border NOTIFY changed)
	Q_PROPERTY(QColor text READ text NOTIFY changed)
	Q_PROPERTY(QColor muted READ muted NOTIFY changed)
	Q_PROPERTY(QColor accent READ accent NOTIFY changed)
	Q_PROPERTY(QColor accentFill READ accentFill NOTIFY changed)
	Q_PROPERTY(QColor badFill READ badFill NOTIFY changed)
	Q_PROPERTY(QColor accentSoft READ accentSoft NOTIFY changed)
	Q_PROPERTY(QColor goodSoft READ goodSoft NOTIFY changed)
	Q_PROPERTY(QColor warnSoft READ warnSoft NOTIFY changed)
	Q_PROPERTY(QColor badSoft READ badSoft NOTIFY changed)
	Q_PROPERTY(QColor control READ control NOTIFY changed)
	Q_PROPERTY(QColor good READ good NOTIFY changed)
	Q_PROPERTY(QColor warn READ warn NOTIFY changed)
	Q_PROPERTY(QColor bad READ bad NOTIFY changed)
	Q_PROPERTY(QColor grid READ grid NOTIFY changed)
	Q_PROPERTY(QVariantList series READ series NOTIFY changed)
	Q_PROPERTY(QString monoFamily READ monoFamily CONSTANT)

public:
	explicit PhoneTheme(QObject *parent = nullptr);

	bool dark() const { return dark_; }
	void setDark(bool dark); /* saved at once, as the desktop's theme button does */
	QColor bg() const { return colors_.bg; }
	QColor surface() const { return colors_.surface; }
	QColor surface2() const { return colors_.surface2; }
	QColor border() const { return colors_.border; }
	QColor text() const { return colors_.text; }
	QColor muted() const { return colors_.muted; }
	QColor accent() const { return colors_.accent; }
	QColor accentFill() const { return colors_.accentFill; }
	QColor badFill() const { return colors_.badFill; }
	QColor accentSoft() const { return soft(colors_.accent); }
	QColor goodSoft() const { return soft(colors_.good); }
	QColor warnSoft() const { return soft(colors_.warn); }
	QColor badSoft() const { return soft(colors_.bad); }
	QColor control() const { return colors_.control; }
	QColor good() const { return colors_.good; }
	QColor warn() const { return colors_.warn; }
	QColor bad() const { return colors_.bad; }
	QColor grid() const { return colors_.grid; }
	QVariantList series() const;
	static QString monoFamily();

signals:
	void changed();

private:
	QColor soft(const QColor &tone) const; /* `tone` over the surface at SOFT_SHARE (dark) or SOFT_SHARE_LIGHT */
	static constexpr double SOFT_SHARE = 0.14, SOFT_SHARE_LIGHT = 0.08;
	bool dark_ = true;
	ThemeColors colors_;
};
