/* SPDX-License-Identifier: Apache-2.0 */
/* The register list of the Map editor tab: see map_table_model.h. */
#include "ui/map_table_model.h"

#include <QStringList>

#include "model/map_document.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

QString writeText(WriteKind kind) {
	switch (kind) {
	case WriteKind::Normal: return QStringLiteral("normal");
	case WriteKind::Action: return QStringLiteral("action");
	case WriteKind::WriteOneToClear: return QStringLiteral("w1c");
	}
	return {};
}

/* the rest of a register in a few words: what the other columns do not show */
QString moreText(const RegDef &def) {
	QStringList parts;
	if (def.danger) parts << QStringLiteral("⚠ danger");
	if (def.persist) parts << QObject::tr("persist");
	if (!def.plottable && def.isNumeric()) parts << QObject::tr("not plotted");
	if (def.hex) parts << QStringLiteral("hex");
	if (def.scale != 1.0 || def.offset != 0.0)
		parts << ltrPiece(QStringLiteral("×%1%2").arg(def.scale).arg(def.offset != 0.0 ? QStringLiteral(" %1%2")
				.arg(def.offset > 0 ? QStringLiteral("+") : QString()).arg(def.offset) : QString()));
	if (def.hasMin() || def.hasMax()) /* one piece in Arabic: "-20 … 120", not "120 … 20-" */
		parts << ltrPiece(QStringLiteral("%1 … %2").arg(def.hasMin() ? QString::number(def.min) : QString(),
				def.hasMax() ? QString::number(def.max) : QString()));
	if (def.hasDefault()) parts << QObject::tr("default %1").arg(ltrPiece(QString::number(def.defaultValue)));
	if (!def.enumValues.isEmpty()) parts << QObject::tr("%n value name(s)", nullptr, int(def.enumValues.size()));
	if (!def.special.isEmpty()) parts << QObject::tr("%n special", nullptr, int(def.special.size()));
	if (!def.fields.isEmpty()) parts << QObject::tr("%n field(s)", nullptr, int(def.fields.size()));
	if (!def.notes.isEmpty()) parts << QObject::tr("notes");
	return parts.join(QStringLiteral(" · "));
}

} // namespace

MapTableModel::MapTableModel(MapDocument *doc, QObject *parent) : QAbstractTableModel(parent), doc_(doc) {
	monoFont_ = QFont(QStringLiteral("Cascadia Mono"));
	monoFont_.setStyleHint(QFont::Monospace);
	/* the document is the truth: every change, the whole list again (a map is small) */
	connect(doc_, &MapDocument::changed, this, [this] {
		beginResetModel();
		endResetModel();
	});
}

QStringList MapTableModel::typeChoices() {
	return { QStringLiteral("u8"), QStringLiteral("i8"), QStringLiteral("u16"), QStringLiteral("i16"),
		QStringLiteral("u32"), QStringLiteral("i32"), QStringLiteral("f32"), QStringLiteral("bytes") };
}

QStringList MapTableModel::accessChoices() { return { QStringLiteral("ro"), QStringLiteral("rw"), QStringLiteral("wo") }; }

QStringList MapTableModel::writeChoices() {
	return { QStringLiteral("normal"), QStringLiteral("action"), QStringLiteral("w1c") };
}

int MapTableModel::rowCount(const QModelIndex &parent) const {
	return parent.isValid() ? 0 : int(doc_->map().regs.size());
}

quint32 MapTableModel::uidAt(int row) const {
	const QVector<RegDef> &regs = doc_->map().regs;
	return row >= 0 && row < regs.size() ? regs[row].uid : 0;
}

QString MapTableModel::displayText(const RegDef &def, int column) const {
	switch (column) {
	case ColAddr: return addrText(def.addr);
	case ColName: return def.name;
	case ColType: return typeName(def.type);
	case ColSize: return QString::number(def.size);
	case ColUnit: return def.unit;
	case ColAccess: return accessText(def);
	case ColWrite: return def.write == WriteKind::Normal ? QString() : writeText(def.write);
	case ColGroup: return def.group;
	case ColDesc: return def.desc;
	case ColMore: return moreText(def);
	}
	return {};
}

QString MapTableModel::issuesOf(int row, bool &error) const {
	QStringList lines;
	error = false;
	for (const MapIssue &issue : doc_->issues()) {
		if (issue.reg != row) continue;
		lines << (issue.error ? tr("Error: %1") : tr("Warning: %1")).arg(issue.text);
		error = error || issue.error;
	}
	return lines.join(QLatin1Char('\n'));
}

QVariant MapTableModel::data(const QModelIndex &index, int role) const {
	if (!index.isValid() || index.row() >= rowCount()) return {};
	const RegDef &def = doc_->map().regs[index.row()];
	const int column = index.column();
	if (column == ColIssue) {
		/* the row's number in the map, and the checks' dot before it */
		bool error;
		const QString issues = issuesOf(index.row(), error);
		const QString number = QString::number(index.row() + 1);
		switch (role) {
		case Qt::DisplayRole: return issues.isEmpty() ? number : QStringLiteral("● ") + number;
		case Qt::ToolTipRole: return issues.isEmpty() ? QVariant() : QVariant(issues);
		case Qt::ForegroundRole:
			return issues.isEmpty() ? Theme::colors().muted : error ? Theme::colors().bad : Theme::colors().warn;
		case Qt::TextAlignmentRole: return int(Qt::AlignCenter);
		case Qt::FontRole: return monoFont_;
		}
		return {};
	}
	switch (role) {
	case Qt::DisplayRole: return displayText(def, column);
	case Qt::EditRole: return column == ColWrite ? writeText(def.write) : displayText(def, column);
	case Qt::FontRole: return column == ColAddr ? QVariant(monoFont_) : QVariant();
	case Qt::ForegroundRole:
		if (column == ColMore || (column == ColSize && def.type != RegType::Bytes)) return Theme::colors().muted;
		if (column == ColAccess && def.rw) return Theme::colors().accent;
		return {};
	case Qt::ToolTipRole:
		if (column == ColDesc && !def.notes.isEmpty()) return def.desc + QStringLiteral("\n\n") + def.notes;
		if (column == ColMore) return moreText(def);
		return {};
	}
	return {};
}

QVariant MapTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
	if (orientation != Qt::Horizontal) return {};
	/* as the column's cells: the row number in the middle, the rest at the left */
	if (role == Qt::TextAlignmentRole)
		return int(section == ColIssue ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
	if (role != Qt::DisplayRole) return {};
	switch (section) {
	case ColIssue: return tr("#");
	case ColAddr: return tr("Address");
	case ColName: return tr("Name");
	case ColType: return tr("Type");
	case ColSize: return tr("Size");
	case ColUnit: return tr("Unit");
	case ColAccess: return tr("Access");
	case ColWrite: return tr("Write");
	case ColGroup: return tr("Group");
	case ColDesc: return tr("Description");
	case ColMore: return tr("More");
	}
	return {};
}

Qt::ItemFlags MapTableModel::flags(const QModelIndex &index) const {
	Qt::ItemFlags itemFlags = QAbstractTableModel::flags(index);
	const int column = index.column();
	if (column == ColIssue || column == ColMore) return itemFlags;
	if (column == ColSize && index.row() < rowCount() && doc_->map().regs[index.row()].type != RegType::Bytes)
		return itemFlags; /* fixed by the type */
	return itemFlags | Qt::ItemIsEditable;
}

bool MapTableModel::setData(const QModelIndex &index, const QVariant &value, int role) {
	if (role != Qt::EditRole || !index.isValid() || index.row() >= rowCount()) return false;
	const int column = index.column();
	const QString text = value.toString().trimmed();
	const quint32 uid = uidAt(index.row());
	const RegDef &now = doc_->map().regs[index.row()];
	if (displayText(now, column) == text || (column == ColWrite && writeText(now.write) == text)) return false;

	/* the name and the address: this register only, and checked first */
	if (column == ColAddr || column == ColName) {
		uint16_t addr = now.addr;
		if (column == ColAddr) {
			bool ok = false;
			const uint parsed = parseAddress(text, &ok);
			if (!ok || parsed > 0xFFFF) {
				emit refused(tr("not an address: \"%1\" (0x0000 … 0xFFFF)").arg(text));
				return false;
			}
			addr = uint16_t(parsed);
		} else if (text.isEmpty()) {
			emit refused(tr("a register needs a name"));
			return false;
		}
		doc_->edit(column == ColAddr ? tr("Address of %1").arg(now.name) : tr("Rename %1").arg(now.name),
				[uid, addr, column, text](DeviceMap &map) {
					for (RegDef &def : map.regs) {
						if (def.uid != uid) continue;
						if (column == ColAddr) def.addr = addr;
						else def.name = text;
					}
				});
		return true;
	}

	RegType type = now.type;
	if (column == ColType && !parseType(text, type)) {
		emit refused(tr("not a type: \"%1\"").arg(text));
		return false;
	}
	bool sizeOk = false;
	const int size = text.toInt(&sizeOk);
	if (column == ColSize && (!sizeOk || size < 1 || size > 0xFFFF)) {
		emit refused(tr("a size of 1 … 65535 bytes"));
		return false;
	}
	if (column == ColAccess && !accessChoices().contains(text)) {
		emit refused(tr("access is ro, rw or wo"));
		return false;
	}
	if (column == ColWrite && !writeChoices().contains(text)) {
		emit refused(tr("write is normal, action or w1c"));
		return false;
	}
	const QVector<quint32> uids = targets_ ? targets_(index.row()) : QVector<quint32>{ uid };
	const QString what = headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
	const QString step = uids.size() > 1 ? tr("%1 of %n registers", nullptr, int(uids.size())).arg(what)
			: tr("%1 of %2").arg(what, now.name);
	doc_->edit(step, [uids, column, text, type, size](DeviceMap &map) {
		for (RegDef &def : map.regs) {
			if (!uids.contains(def.uid)) continue;
			switch (column) {
			case ColType:
				def.type = type;
				if (type != RegType::Bytes) def.size = typeSize(type);
				break;
			case ColSize:
				if (def.type == RegType::Bytes) def.size = size;
				break;
			case ColUnit: def.unit = text; break;
			case ColAccess:
				def.rw = text != QLatin1String("ro");
				def.readable = text != QLatin1String("wo");
				break;
			case ColWrite:
				def.write = text == QLatin1String("action") ? WriteKind::Action
						: text == QLatin1String("w1c") ? WriteKind::WriteOneToClear : WriteKind::Normal;
				break;
			case ColGroup: def.group = text.isEmpty() ? QObject::tr("Registers") : text; break;
			case ColDesc: def.desc = text; break;
			}
		}
	});
	return true;
}
