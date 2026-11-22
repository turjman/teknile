/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's Help (PhoneHelp, a singleton for QML; the Help screen, HelpScreen.qml): the pages of help_topics.h
 * (split 5) that apply on a phone, its own "Using the phone app" first, then the desktop's but the Map editor, the API,
 * the Dump, the command line and Keys & mouse; every topic in the language the app runs in, styled with the theme's
 * colours. In Arabic every paragraph runs right to left, also one that starts with a Latin word, and the code blocks
 * left to right, as the desktop's dialog lays them out. */
#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class PhoneHelp : public QObject {
	Q_OBJECT
	QML_ELEMENT
	QML_SINGLETON
	Q_PROPERTY(QStringList titles READ titles CONSTANT)

public:
	explicit PhoneHelp(QObject *parent = nullptr);

	QStringList titles() const;
	/* a topic's page as rich text for a QML Text; empty out of range */
	Q_INVOKABLE QString page(int index, bool dark) const;
};
