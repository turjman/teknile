/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's theme for QML (PhoneTheme, a singleton): the colours of Theme::colorsFor, the desktop's table, and
 * `dark`, saved in "ui/dark" as the desktop saves it. Main.qml colours Material from it (theme, accent, background),
 * and every screen takes its colours from here, so both looks keep the desktop's contrast (theme.h). */
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
	Q_PROPERTY(QColor control READ control NOTIFY changed)
	Q_PROPERTY(QColor good READ good NOTIFY changed)
	Q_PROPERTY(QColor warn READ warn NOTIFY changed)
	Q_PROPERTY(QColor bad READ bad NOTIFY changed)
	Q_PROPERTY(QColor grid READ grid NOTIFY changed)
	Q_PROPERTY(QVariantList series READ series NOTIFY changed)

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
	QColor control() const { return colors_.control; }
	QColor good() const { return colors_.good; }
	QColor warn() const { return colors_.warn; }
	QColor bad() const { return colors_.bad; }
	QColor grid() const { return colors_.grid; }
	QVariantList series() const;

signals:
	void changed();

private:
	bool dark_ = true;
	ThemeColors colors_;
};
