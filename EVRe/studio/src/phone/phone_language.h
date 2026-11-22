/* SPDX-License-Identifier: Apache-2.0 */
/* The language choice for QML (PhoneLanguage, a singleton): the drawer's System / English / العربية, saved in
 * "ui/language" as the desktop's sidebar saves it (language.h) and applied at the next start; `pending` while the
 * choice gives another language than the one applied, so the drawer says it comes when the app is opened again, with
 * Restart now under it (restart(): the app opened again, as the desktop's Restart now does). */
#pragma once

#include <QObject>
#include <QStringList>
#include <functional>
#include <QtQml/qqmlregistration.h>

class PhoneLanguage : public QObject {
	Q_OBJECT
	QML_ELEMENT
	QML_SINGLETON
	Q_PROPERTY(QStringList codes READ codes CONSTANT)
	Q_PROPERTY(QString choice READ choice WRITE setChoice NOTIFY changed)
	Q_PROPERTY(bool pending READ pending NOTIFY changed)

public:
	explicit PhoneLanguage(QObject *parent = nullptr);

	QStringList codes() const;
	QString choice() const { return choice_; }
	void setChoice(const QString &code); /* saved at once */
	bool pending() const;
	/* the app closed and opened again, in the language chosen: on Android its activity started anew in a new process
	 * (EvreService.restart); elsewhere the program started again and this one quit, as the desktop's main() does */
	Q_INVOKABLE void restart();
	/* a test's stand-in for restart() (nullptr: the real one) */
	static void setRestartForTests(std::function<void()> standIn);

signals:
	void changed();

private:
	QString choice_;
};
