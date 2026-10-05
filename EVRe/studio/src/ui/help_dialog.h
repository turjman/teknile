/* SPDX-License-Identifier: Apache-2.0 */
/* Help (F1): topics on the left, the page on the right. The pages live in help_dialog.cpp. */
#pragma once

#include <QDialog>

class QListWidget;
class QTextBrowser;

class HelpDialog : public QDialog {
	Q_OBJECT
public:
	explicit HelpDialog(QWidget *parent = nullptr);
	void showTopic(int index); /* index into the topic list; out of range: nothing changes */

private:
	QListWidget *topics_;
	QTextBrowser *page_;
};
