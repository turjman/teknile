/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's theme: see phone_theme.h. */
#include "phone/phone_theme.h"

#include <QSettings>

namespace {
const QString DARK_KEY = QStringLiteral("ui/dark"); /* the desktop's key (main.cpp): dark unless set */
}

PhoneTheme::PhoneTheme(QObject *parent)
	: QObject(parent), dark_(QSettings().value(DARK_KEY, true).toBool()), colors_(Theme::colorsFor(dark_)) {}

void PhoneTheme::setDark(bool dark) {
	if (dark == dark_) return;
	dark_ = dark;
	colors_ = Theme::colorsFor(dark);
	QSettings().setValue(DARK_KEY, dark);
	emit changed();
}

QVariantList PhoneTheme::series() const {
	QVariantList list;
	for (const QColor &color : colors_.series) list.append(color);
	return list;
}
