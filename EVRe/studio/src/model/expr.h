/* SPDX-License-Identifier: Apache-2.0 */
/* A small math expression over register values, for the chart's math lines:
 *
 *   SUPPLY_V * SUPPLY_I        power, W
 *   abs(SUPPLY_I) * 3.6        sqrt(X^2 + Y^2)        (T - 32) / 1.8
 *
 * Numbers (1, 2.5, 1e-3), names (registers, resolved by the caller), + - * /
 * ^ (power), unary -, parentheses, pi, e, and the functions abs sqrt exp log
 * (natural, also ln) log10 sin cos tan asin acos atan atan2 min max pow floor
 * ceil round sign clamp(x, lo, hi).
 *
 * parse() compiles the text once into a postfix program; eval() runs that
 * program for every sample, on a small stack, without allocating. */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

class Expr {
public:
	/* names -> an input index (>= 0), or -1 if unknown */
	using Resolve = std::function<int(const QString &name)>;

	/* false with err set (in words for the user) when the text is not a valid expression */
	bool parse(const QString &text, const Resolve &resolve, QString &err);
	/* inputs[i]: the value of input i (as numbered by resolve); NaN if not parsed */
	double eval(const double *inputs) const;
	bool ok() const { return !program_.isEmpty(); }
	QStringList names() const { return names_; } /* the names used, as written */

	/* what a formula may name besides registers: the functions with their parameters ("atan2", "y, x"), then the
	 * constants (pi, e: no parameters). From the parser's own table, so a completion offers nothing it refuses. */
	struct Builtin {
		QString name;
		QString params; /* empty: a constant */
	};
	static QVector<Builtin> builtins();

private:
	class Parser;

	enum Op { Number, Input, Negate, Add, Subtract, Multiply, Divide, Power, Call };
	struct Step {
		Op op;
		double number = 0; /* Number: its value */
		int index = 0;     /* Input: which input; Call: which function */
	};

	QVector<Step> program_; /* postfix */
	QStringList names_;
};
