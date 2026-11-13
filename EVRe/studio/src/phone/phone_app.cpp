/* SPDX-License-Identifier: Apache-2.0 */
/* Loading the phone's window: see phone_app.h. */
#include "phone/phone_app.h"

#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QVariantMap>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

#include "phone/p9_proof.h"

namespace phone {

QString mapsFolder() {
#ifdef Q_OS_ANDROID
	return QStringLiteral("assets:/maps");
#else
	return QCoreApplication::applicationDirPath() + QStringLiteral("/maps");
#endif
}

void setStyle() { QQuickStyle::setStyle(QStringLiteral("Material")); }

bool p9Asked() {
	if (qEnvironmentVariableIntValue("EVRE_P9") == 1) return true;
#ifdef Q_OS_ANDROID
	/* an app on Android has no environment of its own: the start intent's extra, as main.cpp reads the desktop's */
	const QJniObject activity = QNativeInterface::QAndroidApplication::context();
	const QJniObject intent = activity.callObjectMethod("getIntent", "()Landroid/content/Intent;");
	if (!intent.isValid()) return false;
	const QJniObject name = QJniObject::fromString(QStringLiteral("p9"));
	return intent.callMethod<jboolean>("getBooleanExtra", "(Ljava/lang/String;Z)Z", name.object<jstring>(), jboolean(false));
#else
	return false;
#endif
}

QQuickWindow *loadMain(QQmlApplicationEngine &engine, bool p9, QSize size) {
	QVariantMap properties{ { QStringLiteral("p9"), p9 } };
	if (size.isValid()) {
		properties.insert(QStringLiteral("width"), size.width());
		properties.insert(QStringLiteral("height"), size.height());
	}
	engine.setInitialProperties(properties);
	engine.loadFromModule("EVRe.Phone", "Main");
	auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().constLast());
	if (window && p9) new P9Meter(window);
	return window;
}

} // namespace phone
