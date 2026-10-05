/* SPDX-License-Identifier: Apache-2.0 */
/* Completion while a formula is typed: see formula_completer.h. */
#include "ui/formula_completer.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QLineEdit>
#include <QStandardItemModel>
#include <algorithm>

#include "model/expr.h"

namespace {

enum Role { NameRole = Qt::UserRole + 1, FunctionRole };

/* a character of a name, as the formula's parser reads one */
bool nameChar(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.'); }

/* a part of the name after _ or . starts with the word (I: SUPPLY_I, V: SUPPLY_V) */
bool partStartsWith(const QString &name, const QString &word) {
	for (int i = 1; i < name.size(); i++)
		if ((name.at(i - 1) == QLatin1Char('_') || name.at(i - 1) == QLatin1Char('.'))
				&& name.mid(i).startsWith(word, Qt::CaseInsensitive))
			return true;
	return false;
}

} // namespace

FormulaCompleter::FormulaCompleter(QLineEdit *box, const QVector<RegDef> &registers, QObject *parent)
	: QObject(parent), box_(box), model_(new QStandardItemModel(this)), completer_(new QCompleter(this)) {
	for (const RegDef &def : registers) {
		if (!def.isNumeric()) continue; /* as MathLine::compile takes them */
		QStringList detail;
		if (!def.unit.isEmpty()) detail << def.unit;
		if (!def.desc.isEmpty()) detail << def.desc;
		all_.push_back({ def.name, detail.join(QStringLiteral(" · ")), false });
	}
	for (const Expr::Builtin &builtin : Expr::builtins()) {
		const bool function = !builtin.params.isEmpty();
		all_.push_back({ builtin.name,
				function ? QStringLiteral("%1(%2)").arg(builtin.name, builtin.params) : tr("constant"), function });
	}
	/* the list is filtered and ordered here (rank): the completer only shows it, under the box */
	completer_->setModel(model_);
	completer_->setWidget(box_);
	completer_->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
	completer_->setMaxVisibleItems(10);
	connect(box_, &QLineEdit::textEdited, this, &FormulaCompleter::update);
	connect(completer_, qOverload<const QModelIndex &>(&QCompleter::activated), this, &FormulaCompleter::accept);
}

int FormulaCompleter::wordStart(const QString &text, int cursor) {
	cursor = std::clamp(cursor, 0, int(text.size()));
	int start = cursor;
	while (start > 0 && nameChar(text.at(start - 1))) start--;
	if (start < cursor && text.at(start).isDigit()) return cursor; /* a number, not a name */
	return start;
}

QVector<FormulaCompleter::Candidate> FormulaCompleter::rank(const QVector<Candidate> &all, const QString &word) {
	if (word.isEmpty()) return {};
	QVector<Candidate> starts, parts, contains;
	for (const Candidate &candidate : all) {
		if (candidate.name.startsWith(word, Qt::CaseInsensitive)) starts << candidate;
		else if (partStartsWith(candidate.name, word)) parts << candidate;
		else if (candidate.name.contains(word, Qt::CaseInsensitive)) contains << candidate;
	}
	/* in each group the shorter names first (SIN before SINE_FREQ), then by name */
	const auto order = [](const Candidate &a, const Candidate &b) {
		if (a.name.size() != b.name.size()) return a.name.size() < b.name.size();
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	};
	for (QVector<Candidate> *group : { &starts, &parts, &contains }) std::stable_sort(group->begin(), group->end(), order);
	return starts + parts + contains;
}

QStringList FormulaCompleter::shown() const {
	QStringList names;
	if (!completer_->popup()->isVisible()) return names;
	for (int row = 0; row < model_->rowCount(); row++) names << model_->item(row)->data(NameRole).toString();
	return names;
}

void FormulaCompleter::update() {
	const QString text = box_->text();
	const int cursor = box_->cursorPosition();
	const int start = wordStart(text, cursor);
	const QString word = text.mid(start, cursor - start);
	const QVector<Candidate> found = rank(all_, word);
	/* nothing to offer, or the word is that one name already */
	if (found.isEmpty() || (found.size() == 1 && found.front().name.compare(word, Qt::CaseInsensitive) == 0)) {
		completer_->popup()->hide();
		return;
	}
	model_->clear();
	for (const Candidate &candidate : found) {
		auto *item = new QStandardItem(candidate.detail.isEmpty() ? candidate.name
				: QStringLiteral("%1     %2").arg(candidate.name, candidate.detail));
		item->setData(candidate.name, NameRole);
		item->setData(candidate.function, FunctionRole);
		item->setToolTip(candidate.detail);
		model_->appendRow(item);
	}
	completer_->complete(); /* under the box, as wide as it */
	completer_->popup()->setCurrentIndex(completer_->completionModel()->index(0, 0)); /* Enter takes the best */
}

void FormulaCompleter::accept(const QModelIndex &index) {
	const QString name = index.data(NameRole).toString();
	if (name.isEmpty()) return;
	QString text = box_->text();
	const int cursor = box_->cursorPosition();
	const int start = wordStart(text, cursor);
	int end = cursor; /* the whole word, also what follows the cursor in it */
	while (end < text.size() && nameChar(text.at(end))) end++;
	QString insert = name;
	int caret = start + int(name.size());
	if (index.data(FunctionRole).toBool()) {
		const bool bracket = end < text.size() && text.at(end) == QLatin1Char('(');
		if (!bracket) insert += QStringLiteral("()");
		caret += 1; /* inside the brackets */
	}
	text.replace(start, end - start, insert);
	box_->setText(text);
	box_->setCursorPosition(caret);
}
