/* SPDX-License-Identifier: Apache-2.0 */
/* The language choice for QML (PhoneLanguage, a singleton): the drawer's System / English / العربية, saved in
 * "ui/language" as the desktop's sidebar saves it (language.h) and applied at the next start; `pending` while the
 * choice gives another language than the one applied, so the drawer says it comes when the app is opened again. */
#pragma once

#include <QObject>
#include <QStringList>
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

signals:
	void changed();

private:
	QString choice_;
};
