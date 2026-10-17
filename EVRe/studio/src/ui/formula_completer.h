/* SPDX-License-Identifier: Apache-2.0 */
/* Completion while a formula is typed (the Math line dialog): a list under the
 * box of what the word at the cursor may become. The registers a formula may
 * read (the numeric ones, as MathLine::compile takes them), each with its unit
 * and description, the fast streams' channels (addStreams), and the functions and constants of the formula language
 * (Expr::builtins, the parser's own table), each function with its parameters.
 *
 * The best first: names that start with the word, then names with a part
 * after _ or . that does (I: SUPPLY_I), then names that contain it; any case.
 * Up / Down pick, Enter or Tab take one, Esc closes the list. A register is
 * put in as its name; a function as name(), the cursor inside the brackets.
 * Only the word at the cursor is replaced, not the rest of the formula. */
#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>

#include "model/device_map.h"

class QCompleter;
class QLineEdit;
class QModelIndex;
class QStandardItemModel;

class FormulaCompleter : public QObject {
	Q_OBJECT
public:
	struct Candidate {
		QString name;
		QString detail;        /* beside it: a register's unit and description, a function's parameters */
		bool function = false; /* put in as name(), the cursor inside */
	};

	FormulaCompleter(QLineEdit *box, const QVector<RegDef> &registers, QObject *parent = nullptr);
	/* the fast streams' channels offered too, as STREAM.CHANNEL, each with its unit and stream */
	void addStreams(const QVector<StreamDef> &streams);

	/* where the name being typed at `cursor` starts (letters, digits, _ and ., not a number): cursor if none */
	static int wordStart(const QString &text, int cursor);
	/* the candidates for `word`, best first (see above); an empty word: none */
	static QVector<Candidate> rank(const QVector<Candidate> &all, const QString &word);

	QStringList shown() const; /* the names in the open list, best first; none when it is closed (tests) */
	QCompleter *completer() const { return completer_; }

private:
	void update();                          /* a key typed: the list for the word at the cursor, or none */
	void accept(const QModelIndex &index);  /* one taken: it replaces the word */

	QLineEdit *box_;
	QVector<Candidate> all_;
	QStandardItemModel *model_;
	QCompleter *completer_;
};
