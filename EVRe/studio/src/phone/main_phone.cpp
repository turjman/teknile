/* SPDX-License-Identifier: Apache-2.0 */
/* EVRe Studio for phones: the phone's interface in Qt Quick (STUDIO.md 25.6), the APK on Android. On a desktop it is
 * built with EVRE_PHONE, for checks and trying, as a 412 x 915 window.
 *
 * This file names the program (the desktop's settings: "ui/dark", "ui/language"), applies the saved language, sets
 * the Material style and loads Main.qml (phone_app.cpp). EVRE_P9=1, or on Android `am start ... --ez p9 true`, shows
 * the proof P-9 instead (p9_proof.h). */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/qqmlextensionplugin.h>

#include "phone/phone_app.h"
#include "ui/language.h"

Q_IMPORT_QML_PLUGIN(EVRePhonePlugin)

int main(int argc, char **argv) {
	QGuiApplication app(argc, argv);
	QGuiApplication::setOrganizationName(QStringLiteral("teknile"));
	QGuiApplication::setApplicationName(QStringLiteral("EVReStudio"));
	QGuiApplication::setApplicationVersion(QStringLiteral(EVRE_STUDIO_VERSION));

	language::apply(app, language::resolve(language::saved()));
	phone::setStyle();
	QQmlApplicationEngine engine;
	if (!phone::loadMain(engine, phone::p9Asked())) return 1;
	return app.exec();
}
