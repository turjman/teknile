/* SPDX-License-Identifier: Apache-2.0 */
/* The window's language: see language.h. */
#include "ui/language.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>
#include <memory>

namespace language {

namespace {

const QString SETTING = QStringLiteral("ui/language");
QString applied = QStringLiteral("en");
std::unique_ptr<QTranslator> studio, qt;

} // namespace

QStringList codes() { return { QStringLiteral("system"), QStringLiteral("en"), QStringLiteral("ar") }; }

QString saved() {
	const QString code = QSettings().value(SETTING, QStringLiteral("system")).toString();
	return codes().contains(code) ? code : QStringLiteral("system");
}

void save(const QString &code) { QSettings().setValue(SETTING, code); }

QString resolve(const QString &choice) {
	if (choice != QLatin1String("system")) return choice == QLatin1String("ar") ? choice : QStringLiteral("en");
	return QLocale::system().language() == QLocale::Arabic ? QStringLiteral("ar") : QStringLiteral("en");
}

bool apply(QApplication &app, const QString &code) {
	for (auto *translator : { studio.get(), qt.get() })
		if (translator) QApplication::removeTranslator(translator);
	studio = std::make_unique<QTranslator>();
	qt = std::make_unique<QTranslator>();
	const bool loaded = code != QLatin1String("en") && studio->load(QStringLiteral(":/i18n/evre_studio_%1.qm").arg(code));
	if (loaded) QApplication::installTranslator(studio.get());
	if (code != QLatin1String("en") && qt->load(QStringLiteral("qtbase_%1").arg(code), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
		QApplication::installTranslator(qt.get());
	const bool arabic = loaded && code == QLatin1String("ar");
	app.setLayoutDirection(arabic ? Qt::RightToLeft : Qt::LeftToRight);
	/* Arabic: numbers with Western digits and a decimal point (the spin boxes, the fields), not the locale's */
	QLocale::setDefault(arabic ? QLocale::c() : QLocale::system());
	applied = arabic ? code : QStringLiteral("en");
	return loaded || code == QLatin1String("en");
}

QString current() { return applied; }

} // namespace language
