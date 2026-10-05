/* SPDX-License-Identifier: Apache-2.0 */
/* The math-line expression: a recursive-descent parser that compiles the text
 * into a postfix program, and the stack machine that runs it. See expr.h. */
#include "model/expr.h"

#include <QObject>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

/* the functions a math line may call; each gets its arguments in order */
struct Function {
	const char *name;
	int argCount;
	const char *params; /* their names, for the formula box's completion: "y, x" */
	double (*call)(const double *args);
};

const Function FUNCTIONS[] = {
	{ "abs", 1, "x", [](const double *a) { return std::fabs(a[0]); } },
	{ "sqrt", 1, "x", [](const double *a) { return std::sqrt(a[0]); } },
	{ "exp", 1, "x", [](const double *a) { return std::exp(a[0]); } },
	{ "log", 1, "x", [](const double *a) { return std::log(a[0]); } },
	{ "ln", 1, "x", [](const double *a) { return std::log(a[0]); } },
	{ "log10", 1, "x", [](const double *a) { return std::log10(a[0]); } },
	{ "sin", 1, "x", [](const double *a) { return std::sin(a[0]); } },
	{ "cos", 1, "x", [](const double *a) { return std::cos(a[0]); } },
	{ "tan", 1, "x", [](const double *a) { return std::tan(a[0]); } },
	{ "asin", 1, "x", [](const double *a) { return std::asin(a[0]); } },
	{ "acos", 1, "x", [](const double *a) { return std::acos(a[0]); } },
	{ "atan", 1, "x", [](const double *a) { return std::atan(a[0]); } },
	{ "floor", 1, "x", [](const double *a) { return std::floor(a[0]); } },
	{ "ceil", 1, "x", [](const double *a) { return std::ceil(a[0]); } },
	{ "round", 1, "x", [](const double *a) { return std::round(a[0]); } },
	{ "sign", 1, "x", [](const double *a) { return a[0] > 0 ? 1.0 : a[0] < 0 ? -1.0 : 0.0; } },
	{ "atan2", 2, "y, x", [](const double *a) { return std::atan2(a[0], a[1]); } },
	{ "min", 2, "a, b", [](const double *a) { return std::min(a[0], a[1]); } },
	{ "max", 2, "a, b", [](const double *a) { return std::max(a[0], a[1]); } },
	{ "pow", 2, "x, y", [](const double *a) { return std::pow(a[0], a[1]); } },
	/* bits(x, lsb, width): `width` bits of the integer x from bit `lsb` up (a bit field) */
	{ "bits", 3, "x, lsb, width", [](const double *a) {
		const quint64 x = quint64(qint64(std::llround(a[0])));
		const int lsb = std::clamp(int(a[1]), 0, 63), width = std::clamp(int(a[2]), 1, 64);
		return double((x >> lsb) & (width >= 64 ? ~0ULL : (1ULL << width) - 1));
	} },
	/* clamp(x, lo, hi): the limits in either order */
	{ "clamp", 3, "x, lo, hi", [](const double *a) { return std::clamp(a[0], std::min(a[1], a[2]), std::max(a[1], a[2])); } },
};

/* the function's place in FUNCTIONS (names in any case), or -1 */
int findFunction(const QString &name) {
	for (int i = 0; i < int(std::size(FUNCTIONS)); i++)
		if (name.compare(QLatin1String(FUNCTIONS[i].name), Qt::CaseInsensitive) == 0) return i;
	return -1;
}

} // namespace

QVector<Expr::Builtin> Expr::builtins() {
	QVector<Builtin> all;
	for (const Function &f : FUNCTIONS) all.push_back({ QLatin1String(f.name), QLatin1String(f.params) });
	all.push_back({ QStringLiteral("pi"), QString() });
	all.push_back({ QStringLiteral("e"), QString() });
	return all;
}

/* Recursive descent, one function per precedence level:
 *
 *   sum     = product (+|- product)*
 *   product = unary (*|/ unary)*
 *   unary   = -unary | +unary | power
 *   power   = atom (^ unary)?
 *   atom    = number | name | name(sum, ...) | (sum)
 *
 * Each level appends its operator after its operands, so the program comes
 * out in postfix. The first error stops the parse. */
class Expr::Parser {
public:
	Parser(const QString &text, const Resolve &resolve) : text_(text), resolve_(resolve) {}

	/* the whole text as one expression; false: error() says why */
	bool parseAll() {
		if (!parseSum()) return false;
		skipSpaces();
		if (pos_ != text_.size()) return fail(QObject::tr("unexpected \"%1\"").arg(text_.mid(pos_, 12)));
		return true;
	}
	const QString &error() const { return error_; }
	const QVector<Step> &program() const { return program_; }
	const QStringList &names() const { return names_; }

private:
	bool parseSum() {
		if (!parseProduct()) return false;
		for (;;) {
			Op op;
			if (take('+')) op = Add;
			else if (take('-')) op = Subtract;
			else return true;
			if (!parseProduct()) return false;
			append(op);
		}
	}

	bool parseProduct() {
		if (!parseUnary()) return false;
		for (;;) {
			Op op;
			if (take('*')) op = Multiply;
			else if (take('/')) op = Divide;
			else return true;
			if (!parseUnary()) return false;
			append(op);
		}
	}

	bool parseUnary() {
		if (take('-')) {
			if (!parseUnary()) return false;
			append(Negate);
			return true;
		}
		if (take('+')) return parseUnary();
		return parsePower();
	}

	bool parsePower() {
		if (!parseAtom()) return false;
		if (!take('^')) return true;
		if (!parseUnary()) return false; /* right-associative: 2^3^2 = 2^9 */
		append(Power);
		return true;
	}

	bool parseAtom() {
		skipSpaces();
		if (pos_ >= text_.size()) return fail(QObject::tr("the expression ends too early"));
		if (take('(')) {
			if (!parseSum()) return false;
			if (!take(')')) return fail(QObject::tr("a ) is missing"));
			return true;
		}
		const QChar c = at(pos_);
		if (c.isDigit() || c == QLatin1Char('.')) return parseNumber();
		if (c.isLetter() || c == QLatin1Char('_')) return parseName();
		return fail(QObject::tr("unexpected \"%1\"").arg(c));
	}

	/* 12, 2.5, .5, 1e-3; toDouble() has the last word, so "1.2.3" is refused */
	bool parseNumber() {
		int end = pos_;
		while (at(end).isDigit() || at(end) == QLatin1Char('.')) end++;
		if (at(end) == QLatin1Char('e') || at(end) == QLatin1Char('E')) {
			/* an exponent only when digits follow */
			int digits = end + 1;
			if (at(digits) == QLatin1Char('+') || at(digits) == QLatin1Char('-')) digits++;
			if (at(digits).isDigit()) {
				end = digits;
				while (at(end).isDigit()) end++;
			}
		}
		const QString number = text_.mid(pos_, end - pos_);
		bool ok = false;
		const double value = number.toDouble(&ok);
		if (!ok) return fail(QObject::tr("\"%1\" is not a number").arg(number));
		pos_ = end;
		append(Number, value);
		return true;
	}

	/* a function call, a constant or a register (register names may hold dots) */
	bool parseName() {
		int end = pos_;
		while (at(end).isLetterOrNumber() || at(end) == QLatin1Char('_') || at(end) == QLatin1Char('.')) end++;
		const QString name = text_.mid(pos_, end - pos_);
		pos_ = end;
		if (peek('(')) return parseCall(name);
		if (name.compare(QLatin1String("pi"), Qt::CaseInsensitive) == 0) {
			append(Number, M_PI);
			return true;
		}
		if (name == QLatin1String("e")) {
			append(Number, M_E);
			return true;
		}
		const int input = resolve_ ? resolve_(name) : -1;
		if (input < 0) return fail(QObject::tr("no register \"%1\" in the map").arg(name));
		if (!names_.contains(name)) names_ << name;
		append(Input, 0, input);
		return true;
	}

	bool parseCall(const QString &name) {
		const int function = findFunction(name);
		if (function < 0) return fail(QObject::tr("no function \"%1\"").arg(name));
		const int argCount = FUNCTIONS[function].argCount;
		const auto wrongCount = [&] {
			return fail(QObject::tr("%1 takes %2 values").arg(name, QString::number(argCount)));
		};
		take('(');
		for (int arg = 0; arg < argCount; arg++) {
			if (arg > 0 && !take(',')) return wrongCount();
			if (!parseSum()) return false;
		}
		if (!take(')')) return wrongCount();
		append(Call, 0, function);
		return true;
	}

	/* the character at i, or a null QChar past the end */
	QChar at(int i) const { return i < text_.size() ? text_[i] : QChar(); }
	void skipSpaces() {
		while (at(pos_).isSpace()) pos_++;
	}
	/* the next character after any spaces is c */
	bool peek(char c) {
		skipSpaces();
		return at(pos_) == QLatin1Char(c);
	}
	/* takes the next character if it is c */
	bool take(char c) {
		if (!peek(c)) return false;
		pos_++;
		return true;
	}
	bool fail(const QString &message) {
		error_ = message;
		return false;
	}
	void append(Op op, double number = 0, int index = 0) { program_.push_back({ op, number, index }); }

	const QString &text_;
	const Resolve &resolve_;
	int pos_ = 0;
	QString error_;
	QVector<Step> program_;
	QStringList names_;
};

bool Expr::parse(const QString &text, const Resolve &resolve, QString &err) {
	program_.clear();
	names_.clear();
	if (text.trimmed().isEmpty()) {
		err = QObject::tr("empty");
		return false;
	}
	Parser parser(text, resolve);
	if (!parser.parseAll()) {
		err = parser.error();
		return false;
	}
	program_ = parser.program();
	names_ = parser.names();
	return true;
}

double Expr::eval(const double *inputs) const {
	/* the parser builds only well-formed programs, so the stack never runs
	 * dry; an expression nested too deep for it gives NaN */
	constexpr int MAX_DEPTH = 61;
	double stack[MAX_DEPTH];
	int depth = 0;
	for (const Step &step : program_) {
		switch (step.op) {
		case Number:
		case Input:
			if (depth == MAX_DEPTH) return NAN;
			stack[depth++] = step.op == Number ? step.number : inputs[step.index];
			break;
		case Negate: stack[depth - 1] = -stack[depth - 1]; break;
		case Add: depth--; stack[depth - 1] += stack[depth]; break;
		case Subtract: depth--; stack[depth - 1] -= stack[depth]; break;
		case Multiply: depth--; stack[depth - 1] *= stack[depth]; break;
		case Divide: depth--; stack[depth - 1] /= stack[depth]; break;
		case Power: depth--; stack[depth - 1] = std::pow(stack[depth - 1], stack[depth]); break;
		case Call: {
			/* the arguments are the top argCount values; the result takes the first one's place */
			const Function &function = FUNCTIONS[step.index];
			depth -= function.argCount - 1;
			stack[depth - 1] = function.call(&stack[depth - 1]);
			break;
		}
		}
	}
	return depth == 1 ? stack[0] : NAN;
}
