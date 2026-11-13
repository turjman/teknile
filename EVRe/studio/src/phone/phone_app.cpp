/* SPDX-License-Identifier: Apache-2.0 */
/* Loading the phone's window: see phone_app.h. */
#include "phone/phone_app.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QVariantMap>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

#include "phone/p9_proof.h"
#include "phone/phone_session.h"

namespace phone {

QString mapsFolder() {
#ifdef Q_OS_ANDROID
	return QStringLiteral("assets:/maps");
#else
	return QCoreApplication::applicationDirPath() + QStringLiteral("/maps");
#endif
}

void setStyle() { QQuickStyle::setStyle(QStringLiteral("Material")); }

void useOwnFolder(const QString &folder) {
	QDir().mkpath(folder);
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, folder);
	PhoneSession::setDataFolder(folder);
}

void useOwnFolderAsked() {
	if (qEnvironmentVariableIsSet("EVRE_SETTINGS_DIR")) useOwnFolder(qEnvironmentVariable("EVRE_SETTINGS_DIR"));
}

namespace {

/* a test aid asked for: its variable =1, or on Android the start intent's extra of that name */
bool asked(const char *variable, const char *extra) {
	if (qEnvironmentVariableIntValue(variable) == 1) return true;
#ifdef Q_OS_ANDROID
	/* an app on Android has no environment of its own: the start intent's extra, as main.cpp reads the desktop's */
	const QJniObject activity = QNativeInterface::QAndroidApplication::context();
	const QJniObject intent = activity.callObjectMethod("getIntent", "()Landroid/content/Intent;");
	if (!intent.isValid()) return false;
	const QJniObject name = QJniObject::fromString(QString::fromLatin1(extra));
	return intent.callMethod<jboolean>("getBooleanExtra", "(Ljava/lang/String;Z)Z", name.object<jstring>(), jboolean(false));
#else
	Q_UNUSED(extra)
	return false;
#endif
}

} // namespace

bool p9Asked() { return asked("EVRE_P9", "p9"); }

bool perfAsked() { return asked("EVRE_PERF", "perf"); }

bool chartDemoAsked() { return asked("EVRE_CHARTDEMO", "chartdemo"); }

QQuickWindow *loadMain(QQmlApplicationEngine &engine, bool p9, QSize size, bool perf, bool chartDemo) {
	QVariantMap properties{ { QStringLiteral("p9"), p9 }, { QStringLiteral("perf"), perf },
		{ QStringLiteral("chartDemo"), chartDemo } };
	if (size.isValid()) {
		properties.insert(QStringLiteral("width"), size.width());
		properties.insert(QStringLiteral("height"), size.height());
	}
	engine.setInitialProperties(properties);
	engine.loadFromModule("EVRe.Phone", "Main");
	auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().constLast());
	if (window && p9) new P9Meter(window);
	if (window && perf && !p9) {
		const QPointer<QQuickWindow> shown(window);
		new P9Meter(window, "EVRE_PERF", [shown] { return shown && shown->property("registersShown").toBool(); });
	}
	if (window && chartDemo && !p9) {
		const QPointer<QQuickWindow> shown(window);
		new P9Meter(window, "EVRE_PERF", [shown] { return shown && shown->property("chartShown").toBool(); });
	}
	if (window)
		if (auto *session = engine.singletonInstance<PhoneSession *>("EVRe.Phone", "PhoneSession")) session->attachWindow(window);
	return window;
}

} // namespace phone
