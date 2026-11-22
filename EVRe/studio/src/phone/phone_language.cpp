/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's language choice: see phone_language.h. */
#include "phone/phone_language.h"

#include <QCoreApplication>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#else
#include <QProcess>
#endif

#include "ui/language.h"

namespace {
std::function<void()> restartStandIn;
}

PhoneLanguage::PhoneLanguage(QObject *parent) : QObject(parent), choice_(language::saved()) {}

QStringList PhoneLanguage::codes() const { return language::codes(); }

void PhoneLanguage::setChoice(const QString &code) {
	if (code == choice_ || !language::codes().contains(code)) return;
	choice_ = code;
	language::save(code);
	emit changed();
}

bool PhoneLanguage::pending() const { return language::resolve(choice_) != language::current(); }

void PhoneLanguage::setRestartForTests(std::function<void()> standIn) { restartStandIn = std::move(standIn); }

void PhoneLanguage::restart() {
	if (restartStandIn) {
		restartStandIn();
		return;
	}
#ifdef Q_OS_ANDROID
	QJniObject::callStaticMethod<void>("com/teknile/evre/EvreService", "restart", "(Landroid/content/Context;)V",
			QNativeInterface::QAndroidApplication::context().object());
#else
	QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
	QCoreApplication::quit();
#endif
}
