/* SPDX-License-Identifier: Apache-2.0 */
/* JSON that keeps its file's layout: the map files are written by hand (or by
 * a script) in their own style, and the Studio must be able to save one
 * without rewriting all of it.
 *
 * jsondoc::Value is an ordered tree (object members in file order) that
 * remembers where every value was in the text it was read from. Writing a
 * value back copies the original text of whatever is unchanged, so a number
 * written 1.50 or a string with \u escapes stays as it was; a new or changed
 * value is written in one of two layouts:
 *
 *   pretty    one member per line, as Python's json.dump(indent=2) writes it
 *   compact   { "a": 1, "b": [ 2, 3 ] } on one line, wrapped at `width`
 *
 * patchSequence() writes an object's members or an array's items again: the
 * items that did not change are copied from the text with the spaces, commas
 * and blank lines between them, the others are rendered. It is how a saved
 * map changes only where it was edited (device_map.cpp).
 */
#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QString>
#include <vector>

namespace jsondoc {

struct Member;

/* One JSON value, with the place it came from (byte offsets in the text; -1 = made in memory). */
struct Value {
	enum Kind { Null, Bool, Number, String, Array, Object };
	Kind kind = Null;
	bool boolean = false;
	double number = 0;
	QString string;
	std::vector<Value> items;     /* Array */
	std::vector<Member> members;  /* Object, in file order */
	int start = -1, end = -1;     /* the value's text: [start, end) */

	Value() = default;
	static Value makeString(const QString &text);
	static Value makeNumber(double number);
	static Value makeBool(bool value);
	static Value makeObject() { Value v; v.kind = Object; return v; }
	static Value makeArray() { Value v; v.kind = Array; return v; }

	bool isObject() const { return kind == Object; }
	bool isArray() const { return kind == Array; }
	/* an object's member (the last one of that name, as QJsonObject keeps it); nullptr if none */
	const Value *find(const QString &key) const;
	Value *find(const QString &key);
	int indexOf(const QString &key) const; /* -1 if none */
	/* adds the member, or replaces the value of the one already there. set() and
	 * remove() forget where the object was in the text (start = -1): changed, it
	 * must be rendered, not copied. Code that changes `items` or `members`
	 * directly must do the same. */
	void set(const QString &key, const Value &value);
	void remove(const QString &key);
};

struct Member {
	QString key;
	Value value;
	int keyStart = -1; /* the key's opening quote in the text; -1 = made in memory */
};

/* start = end = keyStart = -1 all the way down: for values moved to another text */
void forgetSource(Value &value);

/* the text -> a tree; false with err ("line 3, column 7: ...") if it is not one JSON value */
bool parse(const QByteArray &text, Value &root, QString &err);

/* to and from Qt's JSON (which sorts the members of an object by name) */
QJsonValue toQt(const Value &value);
Value fromQt(const QJsonValue &value);

/* the same JSON: same kinds, numbers and strings, and objects with the same members
 * in any order. Where the values came from does not count. */
bool equal(const Value &a, const Value &b);
/* equal, and object members in the same order too: written again it would read the same */
bool identical(const Value &a, const Value &b);

struct Style {
	bool pretty = true;
	QByteArray unit = "  ";   /* one level of indentation */
	int width = 110;          /* compact: where a line wraps */
};

/* The value as text. `source` is the text the value was read from (nullptr: none):
 * unchanged scalars, and unchanged one-line values, are copied from it.
 * `column` is where the value starts on its line (for pretty: its level is
 * column / unit; for compact: the wrapping). */
QByteArray render(const Value &value, const Style &style, int column, const QByteArray *source = nullptr);

/* A string as JSON: quotes and escapes, other characters as they are (UTF-8). */
QByteArray quote(const QString &text);

/* One item of an object or an array written again: its key (objects only), its
 * value now, and which item it was in the original container (-1: new). */
struct OutItem {
	QString key;
	Value value;
	int original = -1;
	QByteArray rendered;  /* when not empty: written as it is (the caller rendered it) */
};

/* The container `original` (an object or an array read from `source`) written
 * again with `items`: unchanged items and the text between them are copied, the
 * others rendered with `style` at `column` (the items' column). The result
 * replaces source[original.start, original.end). */
QByteArray patchSequence(const QByteArray &source, const Value &original, const std::vector<OutItem> &items,
		const Style &style, int column);

/* where the value starts on its line (bytes since the last newline) */
int columnOf(const QByteArray &source, int offset);
/* true if an object or array read from `source` has a line break right after its bracket:
 * the pretty layout, not the compact one */
bool isPrettyIn(const QByteArray &source, const Value &value);

} // namespace jsondoc
