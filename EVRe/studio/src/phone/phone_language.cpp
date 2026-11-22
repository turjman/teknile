/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's language choice: see phone_language.h. */
#include "phone/phone_language.h"

#include "ui/language.h"

PhoneLanguage::PhoneLanguage(QObject *parent) : QObject(parent), choice_(language::saved()) {}

QStringList PhoneLanguage::codes() const { return language::codes(); }

void PhoneLanguage::setChoice(const QString &code) {
	if (code == choice_ || !language::codes().contains(code)) return;
	choice_ = code;
	language::save(code);
	emit changed();
}

bool PhoneLanguage::pending() const { return language::resolve(choice_) != language::current(); }
