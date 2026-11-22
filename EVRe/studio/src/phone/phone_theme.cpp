/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's theme: see phone_theme.h. */
#include "phone/phone_theme.h"

#include <QFontDatabase>
#include <QSettings>
#include <cmath>

namespace {
const QString DARK_KEY = QStringLiteral("ui/dark"); /* the desktop's key (main.cpp): dark unless set */
}

QString PhoneTheme::monoFamily() {
	static const QString family = [] {
		const QStringList have = QFontDatabase::families();
		for (const char *name : { "Droid Sans Mono", "Cutive Mono", "Noto Sans Mono", "Consolas", "DejaVu Sans Mono",
					 "Liberation Mono" }) {
			const QString wanted = QString::fromLatin1(name);
			if (have.contains(wanted, Qt::CaseInsensitive) && QFontDatabase::isFixedPitch(wanted)) return wanted;
		}
		return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
	}();
	return family;
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

QColor PhoneTheme::soft(const QColor &tone) const {
	const double k = dark_ ? SOFT_SHARE : SOFT_SHARE_LIGHT;
	const QColor &under = colors_.surface;
	auto mix = [k](int a, int b) { return int(std::lround(b + (a - b) * k)); };
	return QColor(mix(tone.red(), under.red()), mix(tone.green(), under.green()), mix(tone.blue(), under.blue()));
}

QVariantList PhoneTheme::series() const {
	QVariantList list;
	for (const QColor &color : colors_.series) list.append(color);
	return list;
}
