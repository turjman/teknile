/* SPDX-License-Identifier: Apache-2.0 */
/* Loading the phone's window: see phone_app.h. */
#include "phone/phone_app.h"

#include <QCoreApplication>
#include <QDir>
#include <QKeyEvent>
#include <QSettings>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlProperty>
#include <QQuickItem>
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

/* Android's back (the gesture, the button: Qt::Key_Back) to Main.qml's back() before anything else sees it: it closes
 * what is open in order (a Help page, the Help, the write sheet, the drawer) and says whether it did. A popup takes a
 * back only while it has the keys, which the drawer and the sheet do not; what back() leaves goes on as usual. With
 * nothing open, while a link is wanted or a recording runs, back() moves the app to the background (as Home does) and
 * takes the back; otherwise Android has it and closes the app. */
class BackKey : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override {
		if (event->type() != QEvent::KeyPress || static_cast<QKeyEvent *>(event)->key() != Qt::Key_Back) return false;
		QVariant answer; /* a QML function's return */
		QMetaObject::invokeMethod(watched, "back", Q_RETURN_ARG(QVariant, answer));
		const bool closed = answer.toBool();
		if (closed) event->accept();
		return closed;
	}
};

/* No circle on a press (the owner: "so much circles, I hate that"). Material's controls draw a press as a ripple: a
 * circle growing from the finger inside their background (a Button's, a ToolButton's, an ItemDelegate's, a MenuItem's,
 * a TabButton's, a SpinBox's buttons), and a round halo around a Switch's thumb. Before a press reaches the window's
 * items, every Ripple in the window is told it is never pressed (its binding to the control's pressed let go, so the
 * write holds): the wave never starts, and what stays is the Ripple's flat tint over the control's own rounded shape
 * while the control is down (its `active`), in at once and fading out on the release, as the phone's own controls draw
 * it (PressTint.qml). A Switch's halo goes altogether (its thumb moving says the tap). Items made after (a list's rows, a
 * popup's) are met at the next press, before it is delivered. */
class NoRippleWave : public QObject {
public:
	using QObject::QObject;

	static void calm(QQuickItem *item) {
		if (item->inherits("QQuickMaterialRipple") && !item->property("evrePressShape").toBool()) {
			item->setProperty("evrePressShape", true);
			QQmlProperty::write(item, QStringLiteral("pressed"), false);
			/* the Switch's: its indicator's child */
			const QQuickItem *indicator = item->parentItem();
			if (indicator && indicator->parentItem() && indicator->parentItem()->inherits("QQuickSwitch"))
				QQmlProperty::write(item, QStringLiteral("active"), false);
		}
		for (QQuickItem *child : item->childItems()) calm(child);
	}

protected:
	bool eventFilter(QObject *watched, QEvent *event) override {
		const QEvent::Type type = event->type();
		if (type == QEvent::TouchBegin || type == QEvent::MouseButtonPress)
			if (auto *window = qobject_cast<QQuickWindow *>(watched)) {
				QQuickItem *top = window->contentItem();
				while (top && top->parentItem()) top = top->parentItem();
				if (top) calm(top);
			}
		return false;
	}
};

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
	if (window) window->installEventFilter(new BackKey(window));
	if (window) window->installEventFilter(new NoRippleWave(window));
	return window;
}

} // namespace phone
