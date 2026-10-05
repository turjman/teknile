/* SPDX-License-Identifier: Apache-2.0 */
/* JSON that keeps its file's layout: see json_doc.h. */
#include "model/json_doc.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QObject>
#include <algorithm>
#include <cmath>
#include <map>

namespace jsondoc {

/* ---------------------------------------------------------------------- values */

Value Value::makeString(const QString &text) {
	Value v;
	v.kind = String;
	v.string = text;
	return v;
}

Value Value::makeNumber(double number) {
	Value v;
	v.kind = Number;
	v.number = number;
	return v;
}

Value Value::makeBool(bool value) {
	Value v;
	v.kind = Bool;
	v.boolean = value;
	return v;
}

int Value::indexOf(const QString &key) const {
	for (int i = int(members.size()) - 1; i >= 0; i--)
		if (members[size_t(i)].key == key) return i;
	return -1;
}

const Value *Value::find(const QString &key) const {
	const int i = indexOf(key);
	return i < 0 ? nullptr : &members[size_t(i)].value;
}

Value *Value::find(const QString &key) {
	const int i = indexOf(key);
	return i < 0 ? nullptr : &members[size_t(i)].value;
}

/* a changed container no longer reads as its text: start = -1 keeps render() from copying it */
void Value::set(const QString &key, const Value &value) {
	start = end = -1;
	if (Value *there = find(key)) {
		*there = value;
		return;
	}
	Member member;
	member.key = key;
	member.value = value;
	members.push_back(member);
}

void Value::remove(const QString &key) {
	start = end = -1;
	for (size_t i = members.size(); i-- > 0;)
		if (members[i].key == key) members.erase(members.begin() + long(i));
}

void forgetSource(Value &value) {
	value.start = value.end = -1;
	for (Value &item : value.items) forgetSource(item);
	for (Member &m : value.members) {
		m.keyStart = -1;
		forgetSource(m.value);
	}
}

/* ---------------------------------------------------------------------- parse */

namespace {

class Parser {
public:
	explicit Parser(const QByteArray &text) : text_(text), p_(text.constData()), n_(int(text.size())) {}

	bool parseDocument(Value &root, QString &err) {
		if (n_ >= 3 && uchar(p_[0]) == 0xEF && uchar(p_[1]) == 0xBB && uchar(p_[2]) == 0xBF) i_ = 3; /* a UTF-8 BOM */
		skipSpace();
		if (!parseValue(root, 0)) {
			err = error_;
			return false;
		}
		skipSpace();
		if (i_ < n_) {
			fail(QObject::tr("text after the end of the JSON"));
			err = error_;
			return false;
		}
		return true;
	}

private:
	const QByteArray &text_;
	const char *p_;
	int n_;
	int i_ = 0;
	QString error_;

	bool fail(const QString &what) {
		if (!error_.isEmpty()) return false;
		int line = 1, column = 1;
		for (int k = 0; k < i_ && k < n_; k++) {
			if (p_[k] == '\n') {
				line++;
				column = 1;
			} else {
				column++;
			}
		}
		error_ = QObject::tr("line %1, column %2: %3").arg(line).arg(column).arg(what);
		return false;
	}

	void skipSpace() {
		while (i_ < n_ && (p_[i_] == ' ' || p_[i_] == '\t' || p_[i_] == '\n' || p_[i_] == '\r')) i_++;
	}

	bool literal(const char *word) {
		const int length = int(qstrlen(word));
		if (i_ + length > n_ || qstrncmp(p_ + i_, word, uint(length)) != 0) return fail(QObject::tr("unexpected text"));
		i_ += length;
		return true;
	}

	bool parseValue(Value &v, int depth) {
		if (depth > 200) return fail(QObject::tr("nested too deep"));
		if (i_ >= n_) return fail(QObject::tr("a value is missing"));
		v.start = i_;
		const char c = p_[i_];
		bool ok;
		if (c == '{') ok = parseObject(v, depth);
		else if (c == '[') ok = parseArray(v, depth);
		else if (c == '"') {
			v.kind = Value::String;
			ok = parseString(v.string);
		} else if (c == 't') {
			v.kind = Value::Bool;
			v.boolean = true;
			ok = literal("true");
		} else if (c == 'f') {
			v.kind = Value::Bool;
			ok = literal("false");
		} else if (c == 'n') {
			v.kind = Value::Null;
			ok = literal("null");
		} else if (c == '-' || (c >= '0' && c <= '9')) {
			ok = parseNumber(v);
		} else {
			ok = fail(QObject::tr("unexpected character '%1'").arg(QChar::fromLatin1(c)));
		}
		v.end = i_;
		return ok;
	}

	bool parseObject(Value &v, int depth) {
		v.kind = Value::Object;
		i_++; /* { */
		skipSpace();
		if (i_ < n_ && p_[i_] == '}') {
			i_++;
			return true;
		}
		for (;;) {
			skipSpace();
			if (i_ >= n_ || p_[i_] != '"') return fail(QObject::tr("a member name (in quotes) is expected"));
			Member member;
			member.keyStart = i_;
			if (!parseString(member.key)) return false;
			skipSpace();
			if (i_ >= n_ || p_[i_] != ':') return fail(QObject::tr("':' is expected"));
			i_++;
			skipSpace();
			if (!parseValue(member.value, depth + 1)) return false;
			v.members.push_back(std::move(member));
			skipSpace();
			if (i_ < n_ && p_[i_] == ',') {
				i_++;
				continue;
			}
			if (i_ < n_ && p_[i_] == '}') {
				i_++;
				return true;
			}
			return fail(QObject::tr("',' or '}' is expected"));
		}
	}

	bool parseArray(Value &v, int depth) {
		v.kind = Value::Array;
		i_++; /* [ */
		skipSpace();
		if (i_ < n_ && p_[i_] == ']') {
			i_++;
			return true;
		}
		for (;;) {
			skipSpace();
			Value item;
			if (!parseValue(item, depth + 1)) return false;
			v.items.push_back(std::move(item));
			skipSpace();
			if (i_ < n_ && p_[i_] == ',') {
				i_++;
				continue;
			}
			if (i_ < n_ && p_[i_] == ']') {
				i_++;
				return true;
			}
			return fail(QObject::tr("',' or ']' is expected"));
		}
	}

	int hex4() {
		if (i_ + 4 > n_) return -1;
		int value = 0;
		for (int k = 0; k < 4; k++) {
			const char c = p_[i_ + k];
			value <<= 4;
			if (c >= '0' && c <= '9') value |= c - '0';
			else if (c >= 'a' && c <= 'f') value |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') value |= c - 'A' + 10;
			else return -1;
		}
		i_ += 4;
		return value;
	}

	bool parseString(QString &out) {
		i_++; /* " */
		QByteArray bytes;
		for (;;) {
			if (i_ >= n_) return fail(QObject::tr("the string is not closed"));
			const char c = p_[i_];
			if (c == '"') {
				i_++;
				out = QString::fromUtf8(bytes);
				return true;
			}
			if (uchar(c) < 0x20) return fail(QObject::tr("a control character in a string"));
			if (c != '\\') {
				bytes.append(c);
				i_++;
				continue;
			}
			i_++;
			if (i_ >= n_) return fail(QObject::tr("the string is not closed"));
			const char e = p_[i_++];
			switch (e) {
			case '"': bytes.append('"'); break;
			case '\\': bytes.append('\\'); break;
			case '/': bytes.append('/'); break;
			case 'b': bytes.append('\b'); break;
			case 'f': bytes.append('\f'); break;
			case 'n': bytes.append('\n'); break;
			case 'r': bytes.append('\r'); break;
			case 't': bytes.append('\t'); break;
			case 'u': {
				int code = hex4();
				if (code < 0) return fail(QObject::tr("a bad \\u escape"));
				char32_t full = char32_t(code);
				if (code >= 0xD800 && code <= 0xDBFF && i_ + 1 < n_ && p_[i_] == '\\' && p_[i_ + 1] == 'u') {
					i_ += 2;
					const int low = hex4();
					if (low < 0xDC00 || low > 0xDFFF) return fail(QObject::tr("a bad \\u escape"));
					full = 0x10000 + ((char32_t(code) - 0xD800) << 10) + (char32_t(low) - 0xDC00);
				}
				bytes.append(QString::fromUcs4(&full, 1).toUtf8());
				break;
			}
			default: return fail(QObject::tr("a bad escape in a string"));
			}
		}
	}

	bool parseNumber(Value &v) {
		const int begin = i_;
		if (p_[i_] == '-') i_++;
		auto digits = [this] {
			const int from = i_;
			while (i_ < n_ && p_[i_] >= '0' && p_[i_] <= '9') i_++;
			return i_ > from;
		};
		if (!digits()) return fail(QObject::tr("a bad number"));
		if (i_ < n_ && p_[i_] == '.') {
			i_++;
			if (!digits()) return fail(QObject::tr("a bad number"));
		}
		if (i_ < n_ && (p_[i_] == 'e' || p_[i_] == 'E')) {
			i_++;
			if (i_ < n_ && (p_[i_] == '+' || p_[i_] == '-')) i_++;
			if (!digits()) return fail(QObject::tr("a bad number"));
		}
		bool ok = false;
		v.kind = Value::Number;
		v.number = text_.mid(begin, i_ - begin).toDouble(&ok);
		return ok || fail(QObject::tr("a bad number"));
	}
};

} // namespace

bool parse(const QByteArray &text, Value &root, QString &err) {
	root = Value();
	return Parser(text).parseDocument(root, err);
}

/* -------------------------------------------------------------------- Qt JSON */

QJsonValue toQt(const Value &value) {
	switch (value.kind) {
	case Value::Null: return QJsonValue(QJsonValue::Null);
	case Value::Bool: return value.boolean;
	case Value::Number: return value.number;
	case Value::String: return value.string;
	case Value::Array: {
		QJsonArray array;
		for (const Value &item : value.items) array.append(toQt(item));
		return array;
	}
	case Value::Object: {
		QJsonObject object;
		for (const Member &m : value.members) object.insert(m.key, toQt(m.value));
		return object;
	}
	}
	return {};
}

Value fromQt(const QJsonValue &json) {
	Value v;
	switch (json.type()) {
	case QJsonValue::Bool: return Value::makeBool(json.toBool());
	case QJsonValue::Double: return Value::makeNumber(json.toDouble());
	case QJsonValue::String: return Value::makeString(json.toString());
	case QJsonValue::Array:
		v.kind = Value::Array;
		for (const QJsonValue &item : json.toArray()) v.items.push_back(fromQt(item));
		return v;
	case QJsonValue::Object: {
		v.kind = Value::Object;
		const QJsonObject object = json.toObject();
		for (auto it = object.begin(); it != object.end(); ++it) v.set(it.key(), fromQt(it.value()));
		return v;
	}
	case QJsonValue::Null: case QJsonValue::Undefined: break;
	}
	return v;
}

/* ------------------------------------------------------------------ comparing */

namespace {

bool sameScalar(const Value &a, const Value &b) {
	switch (a.kind) {
	case Value::Null: return true;
	case Value::Bool: return a.boolean == b.boolean;
	case Value::Number: return a.number == b.number;
	case Value::String: return a.string == b.string;
	case Value::Array: case Value::Object: break;
	}
	return false;
}

} // namespace

bool equal(const Value &a, const Value &b) {
	if (a.kind != b.kind) return false;
	if (a.kind == Value::Array) {
		if (a.items.size() != b.items.size()) return false;
		for (size_t i = 0; i < a.items.size(); i++)
			if (!equal(a.items[i], b.items[i])) return false;
		return true;
	}
	if (a.kind == Value::Object) {
		/* the last member of a name is the one that counts, as in find() */
		std::map<QString, const Value *> left, right;
		for (const Member &m : a.members) left[m.key] = &m.value;
		for (const Member &m : b.members) right[m.key] = &m.value;
		if (left.size() != right.size()) return false;
		for (const auto &entry : left) {
			const auto other = right.find(entry.first);
			if (other == right.end() || !equal(*entry.second, *other->second)) return false;
		}
		return true;
	}
	return sameScalar(a, b);
}

bool identical(const Value &a, const Value &b) {
	if (a.kind != b.kind) return false;
	if (a.kind == Value::Array) {
		if (a.items.size() != b.items.size()) return false;
		for (size_t i = 0; i < a.items.size(); i++)
			if (!identical(a.items[i], b.items[i])) return false;
		return true;
	}
	if (a.kind == Value::Object) {
		if (a.members.size() != b.members.size()) return false;
		for (size_t i = 0; i < a.members.size(); i++)
			if (a.members[i].key != b.members[i].key || !identical(a.members[i].value, b.members[i].value)) return false;
		return true;
	}
	return sameScalar(a, b);
}

/* ------------------------------------------------------------------ rendering */

QByteArray quote(const QString &text) {
	QByteArray out;
	out.reserve(text.size() + 2);
	out.append('"');
	for (const char c : text.toUtf8()) {
		switch (c) {
		case '"': out.append("\\\""); break;
		case '\\': out.append("\\\\"); break;
		case '\n': out.append("\\n"); break;
		case '\r': out.append("\\r"); break;
		case '\t': out.append("\\t"); break;
		case '\b': out.append("\\b"); break;
		case '\f': out.append("\\f"); break;
		default:
			if (uchar(c) < 0x20) out.append(QStringLiteral("\\u%1").arg(int(uchar(c)), 4, 16, QLatin1Char('0')).toLatin1());
			else out.append(c);
		}
	}
	out.append('"');
	return out;
}

namespace {

QByteArray numberText(double number) {
	if (std::isnan(number) || std::isinf(number)) return "null"; /* JSON has no NaN */
	if (number == std::floor(number) && std::fabs(number) < 1e15) return QByteArray::number(qint64(number));
	return QString::number(number, 'g', QLocale::FloatingPointShortest).toLatin1();
}

bool fromSource(const Value &v, const QByteArray *source) {
	return source && v.start >= 0 && v.end <= source->size() && v.end > v.start;
}

/* scalars: from the text if they came from it, else fresh */
QByteArray scalarText(const Value &v, const QByteArray *source) {
	if (fromSource(v, source)) return source->mid(v.start, v.end - v.start);
	switch (v.kind) {
	case Value::Null: return "null";
	case Value::Bool: return v.boolean ? "true" : "false";
	case Value::Number: return numberText(v.number);
	case Value::String: return quote(v.string);
	case Value::Array: case Value::Object: break;
	}
	return {};
}

/* an unchanged value read from the text, on one line there: copied as it is */
bool copyable(const Value &v, const QByteArray *source) {
	if (!fromSource(v, source)) return false;
	if (v.kind != Value::Array && v.kind != Value::Object) return true;
	return !source->mid(v.start, v.end - v.start).contains('\n');
}

QByteArray compactText(const Value &v, const QByteArray *source) {
	if (v.kind != Value::Array && v.kind != Value::Object) return scalarText(v, source);
	if (copyable(v, source)) return source->mid(v.start, v.end - v.start);
	if (v.kind == Value::Array) {
		if (v.items.empty()) return "[]";
		QByteArray out = "[ ";
		for (size_t i = 0; i < v.items.size(); i++) {
			if (i) out.append(", ");
			out.append(compactText(v.items[i], source));
		}
		return out + " ]";
	}
	if (v.members.empty()) return "{}";
	QByteArray out = "{ ";
	for (size_t i = 0; i < v.members.size(); i++) {
		if (i) out.append(", ");
		out.append(quote(v.members[i].key)).append(": ").append(compactText(v.members[i].value, source));
	}
	return out + " }";
}

QByteArray prettyText(const Value &v, const Style &style, int level, const QByteArray *source) {
	if (v.kind != Value::Array && v.kind != Value::Object) return scalarText(v, source);
	const bool array = v.kind == Value::Array;
	const size_t count = array ? v.items.size() : v.members.size();
	if (count == 0) return array ? "[]" : "{}";
	const QByteArray inner = style.unit.repeated(level + 1);
	QByteArray out(array ? "[\n" : "{\n");
	for (size_t i = 0; i < count; i++) {
		out.append(inner);
		if (array) {
			out.append(prettyText(v.items[i], style, level + 1, source));
		} else {
			out.append(quote(v.members[i].key)).append(": ");
			out.append(prettyText(v.members[i].value, style, level + 1, source));
		}
		out.append(i + 1 < count ? ",\n" : "\n");
	}
	out.append(style.unit.repeated(level)).append(array ? "]" : "}");
	return out;
}

/* compact, wrapped: the pieces go on a line until it is `width` long, then a new
 * line starts two columns in. An array of objects breaks between its objects. */
QByteArray wrappedText(const Value &v, const Style &style, int column, const QByteArray *source) {
	if (v.kind != Value::Object || v.members.empty()) return compactText(v, source);
	QList<QByteArray> pieces;
	for (const Member &m : v.members) {
		const QByteArray head = quote(m.key) + ": ";
		const QByteArray whole = head + compactText(m.value, source);
		const bool objects = m.value.kind == Value::Array && m.value.items.size() > 1
				&& m.value.items.front().kind == Value::Object;
		if (!objects || column + 2 + whole.size() <= style.width) {
			pieces << whole;
			continue;
		}
		for (size_t i = 0; i < m.value.items.size(); i++) {
			QByteArray piece = compactText(m.value.items[i], source);
			if (i == 0) piece = head + "[ " + piece;
			if (i + 1 == m.value.items.size()) piece += " ]";
			pieces << piece;
		}
	}
	const QByteArray indent(column + 2, ' ');
	QByteArray out = "{ ";
	int lineLength = column + 2;
	for (int i = 0; i < pieces.size(); i++) {
		if (i > 0) {
			if (lineLength + 2 + pieces[i].size() > style.width) {
				out.append(",\n").append(indent);
				lineLength = column + 2;
			} else {
				out.append(", ");
				lineLength += 2;
			}
		}
		out.append(pieces[i]);
		lineLength += int(pieces[i].size());
	}
	return out + " }";
}

} // namespace

QByteArray render(const Value &value, const Style &style, int column, const QByteArray *source) {
	if (value.kind != Value::Array && value.kind != Value::Object) return scalarText(value, source);
	if (style.pretty) {
		const int unit = std::max(1, int(style.unit.size()));
		return prettyText(value, style, column / unit, source);
	}
	return wrappedText(value, style, column, source);
}

int columnOf(const QByteArray &source, int offset) {
	const int newline = int(source.lastIndexOf('\n', offset - 1));
	return offset - (newline + 1);
}

bool isPrettyIn(const QByteArray &source, const Value &value) {
	if (!fromSource(value, &source) || (value.kind != Value::Array && value.kind != Value::Object)) return true;
	for (int i = value.start + 1; i < value.end; i++) {
		const char c = source[i];
		if (c == '\n' || c == '\r') return true;
		if (c != ' ' && c != '\t') return false;
	}
	return false;
}

/* --------------------------------------------------------------------- patch */

QByteArray patchSequence(const QByteArray &source, const Value &original, const std::vector<OutItem> &items,
		const Style &style, int column) {
	const bool object = original.kind == Value::Object;
	const int count = int(object ? original.members.size() : original.items.size());
	/* where the original items are: [itemStart, itemEnd) with a member's key */
	auto itemStart = [&](int i) {
		return object ? original.members[size_t(i)].keyStart : original.items[size_t(i)].start;
	};
	auto itemEnd = [&](int i) {
		return object ? original.members[size_t(i)].value.end : original.items[size_t(i)].end;
	};
	auto originalValue = [&](int i) -> const Value & {
		return object ? original.members[size_t(i)].value : original.items[size_t(i)];
	};
	const QByteArray open(1, object ? '{' : '[');
	const QByteArray close(1, object ? '}' : ']');

	/* the text between two items: the one that was there most often (a
	 * blank line between groups is the exception, not the rule) */
	QByteArray usualSeparator = ",\n" + QByteArray(column, ' ');
	if (count > 1) {
		std::map<QByteArray, int> seen;
		for (int i = 0; i + 1 < count; i++) seen[source.mid(itemEnd(i), itemStart(i + 1) - itemEnd(i))]++;
		int best = 0;
		for (const auto &entry : seen)
			if (entry.second > best) {
				best = entry.second;
				usualSeparator = entry.first;
			}
	}
	/* before the first item and after the last one */
	QByteArray prefix, suffix;
	if (count > 0) {
		prefix = source.mid(original.start, itemStart(0) - original.start);
		suffix = source.mid(itemEnd(count - 1), original.end - itemEnd(count - 1));
	} else {
		const int closing = column - int(style.unit.size());
		prefix = open + (style.pretty ? "\n" + QByteArray(column, ' ') : QByteArray(" "));
		suffix = style.pretty ? "\n" + QByteArray(std::max(0, closing), ' ') + close : " " + close;
	}
	if (items.empty()) return open + close;
	const bool crlf = source.contains("\r\n");

	QByteArray out = prefix;
	for (size_t k = 0; k < items.size(); k++) {
		const OutItem &item = items[k];
		if (k > 0) {
			/* an original item keeps the text that was before it; a new one gets the usual */
			const int j = item.original;
			out.append(j > 0 ? source.mid(itemEnd(j - 1), itemStart(j) - itemEnd(j - 1)) : usualSeparator);
		}
		const bool known = item.original >= 0 && item.original < count;
		if (!item.rendered.isEmpty()) {
			if (object && known) {
				const Member &m = original.members[size_t(item.original)];
				out.append(source.mid(m.keyStart, m.value.start - m.keyStart));
			} else if (object) {
				out.append(quote(item.key)).append(": ");
			}
			out.append(item.rendered);
		} else if (known && identical(item.value, originalValue(item.original))) {
			out.append(source.mid(itemStart(item.original), itemEnd(item.original) - itemStart(item.original)));
		} else {
			if (object && known) {
				/* the key as it was written, and the text up to its value */
				const Member &m = original.members[size_t(item.original)];
				out.append(source.mid(m.keyStart, m.value.start - m.keyStart));
			} else if (object) {
				out.append(quote(item.key)).append(": ");
			}
			Style itemStyle = style;
			if (known) {
				/* as the item was: its layout, and a line as long as its longest one was */
				const Value &was = originalValue(item.original);
				itemStyle.pretty = isPrettyIn(source, was);
				const int from = int(source.lastIndexOf('\n', itemStart(item.original)) + 1);
				for (const QByteArray &line : source.mid(from, itemEnd(item.original) + 1 - from).split('\n'))
					itemStyle.width = std::max(itemStyle.width, int(line.size()));
			}
			const int valueColumn = object ? column + int(quote(item.key).size()) + 2 : column;
			/* pretty nests by the item's level, compact wraps from where the value starts; a file
			 * with Windows line ends gets them in what is new too */
			QByteArray text = render(item.value, itemStyle, itemStyle.pretty ? column : valueColumn, &source);
			if (crlf) text.replace("\n", "\r\n");
			out.append(text);
		}
	}
	out.append(suffix);
	return out;
}

} // namespace jsondoc
