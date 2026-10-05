/* SPDX-License-Identifier: Apache-2.0 */
/* The dialog for a math line: see math_line_dialog.h. */
#include "ui/math_line_dialog.h"

#include <QRegularExpression>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "ui/formula_completer.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

MathLineDialog::MathLineDialog(const MathLine &start, bool editing, const QVector<RegDef> &registers,
		QWidget *parent)
	: QDialog(parent), start_(start), registers_(registers) {
	setWindowTitle(editing ? tr("Edit math line") : tr("New math line"));
	name_ = new QLineEdit(start.name);
	unit_ = new QLineEdit(start.unit);
	formula_ = new QLineEdit(start.formula);
	formula_->setObjectName(QStringLiteral("formula"));
	formula_->setPlaceholderText(tr("e.g. SUPPLY_V * SUPPLY_I"));
	formula_->setMinimumWidth(380);
	new FormulaCompleter(formula_, registers, this); /* a list of the registers and functions while a name is typed */
	state_ = new QLabel;
	state_->setObjectName(QStringLiteral("mathState"));
	state_->setWordWrap(true);
	auto *help = mutedLabel(tr("Type a name and a list offers the registers and functions (Enter or Tab takes one).\n"
			"Register names, numbers, + − * / ^, ( ), pi, and abs sqrt exp log log10 sin cos tan "
			"asin acos atan atan2(y,x) min(a,b) max(a,b) pow(a,b) floor ceil round sign clamp(x,lo,hi).\n"
			"Examples: SUPPLY_V * SUPPLY_I (power, W) · abs(SUPPLY_I) · (TEMPERATURE * 9/5) + 32 · sqrt(X^2 + Y^2)"));
	help->setWordWrap(true);
	buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

	auto *form = new QFormLayout(this);
	form->addRow(tr("Name"), name_);
	form->addRow(tr("Unit"), unit_);
	form->addRow(tr("Formula"), formula_);
	form->addRow(QString(), state_);
	form->addRow(help);
	form->addRow(buttons_);

	connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(formula_, &QLineEdit::textChanged, this, &MathLineDialog::validate);
	connect(name_, &QLineEdit::textChanged, this, &MathLineDialog::validate);
	validate();
}

MathLine MathLineDialog::result() const {
	MathLine line = start_;
	line.name = name_->text().trimmed();
	line.unit = unit_->text().trimmed();
	line.formula = formula_->text().trimmed();
	line.on = true;
	return line;
}

void MathLineDialog::setFastChannels(const QStringList &names) {
	fastChannels_ = names;
	validate();
}

void MathLineDialog::validate() {
	MathLine trial;
	trial.formula = formula_->text();
	const bool ok = trial.compile(registers_);
	const ThemeColors &colors = Theme::colors();
	const QStringList names = trial.expr.names();
	const QString reads = names.isEmpty() ? tr("no register") : names.join(QStringLiteral(", "));
	QString error = trial.error;
	if (!ok) { /* a fast stream's channel named: why it is no register (Fast EVRe: math over fast channels comes later) */
		static const QRegularExpression word(QStringLiteral("[A-Za-z_][A-Za-z0-9_.]*"));
		for (auto it = word.globalMatch(formula_->text()); it.hasNext();) {
			const QString name = it.next().captured();
			if (!fastChannels_.contains(name, Qt::CaseInsensitive)) continue;
			error = tr("%1 is a fast stream's channel: a math line reads registers, not fast channels, in this version")
					.arg(name);
			break;
		}
	}
	state_->setText(ok ? coloredSpan(tr("OK: reads %1").arg(reads), colors.good) : coloredSpan(error.toHtmlEscaped(), colors.bad));
	buttons_->button(QDialogButtonBox::Ok)->setEnabled(ok && !name_->text().trimmed().isEmpty());
}
