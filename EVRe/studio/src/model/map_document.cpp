/* SPDX-License-Identifier: Apache-2.0 */
/* The map being edited, and its undo history: see map_document.h. */
#include "model/map_document.h"

#include <QUndoCommand>
#include <QUndoStack>
#include <algorithm>

/* One step: the whole map before and after it. A map is small (a few hundred
 * registers at most), so a copy is simpler and safer than undoing each kind of
 * change by hand. */
class MapEditCommand : public QUndoCommand {
public:
	MapEditCommand(MapDocument *doc, const QString &text, const DeviceMap &before, const DeviceMap &after, int id)
		: QUndoCommand(text), doc_(doc), before_(before), after_(after), id_(id) {}

	void undo() override { doc_->setMap(before_); }
	void redo() override { doc_->setMap(after_); }
	int id() const override { return id_; }
	bool mergeWith(const QUndoCommand *other) override {
		after_ = static_cast<const MapEditCommand *>(other)->after_;
		return true;
	}

private:
	MapDocument *doc_;
	DeviceMap before_, after_;
	int id_;
};

MapDocument::MapDocument(QObject *parent) : QObject(parent), undo_(new QUndoStack(this)) {
	connect(undo_, &QUndoStack::cleanChanged, this, [this](bool clean) { emit modifiedChanged(!clean); });
}

void MapDocument::reset(const DeviceMap &map) {
	undo_->clear();
	map_ = map;
	/* a map that comes with uids (a device of a bus, numbered before) keeps them: new ones start past them */
	for (const RegDef &def : map_.regs) nextUid_ = std::max(nextUid_, def.uid + 1);
	giveUids(map_);
	issuesValid_ = false;
	undo_->setClean();
	emit changed();
	emit modifiedChanged(false);
}

void MapDocument::markSaved(const QString &path) {
	map_.path = path;
	undo_->setClean();
	emit changed();
}

bool MapDocument::isModified() const { return !undo_->isClean(); }

void MapDocument::edit(const QString &text, const std::function<void(DeviceMap &)> &change, int mergeKey) {
	DeviceMap after = map_;
	change(after);
	giveUids(after);
	after.sort();
	/* merge keys stay apart from each other and from the steps before endMerge() */
	const int id = mergeKey > 0 ? ((mergeKey & 0xFFFFF) | ((mergeSalt_ & 0x3FF) << 20)) : -1;
	undo_->push(new MapEditCommand(this, text, map_, after, id));
}

void MapDocument::setMap(const DeviceMap &map) {
	/* where the map is saved is not a step: undo after a Save As keeps the new file */
	const QString path = map_.path;
	map_ = map;
	map_.path = path;
	issuesValid_ = false;
	emit changed();
}

void MapDocument::giveUids(DeviceMap &map) {
	for (RegDef &def : map.regs)
		if (def.uid == 0) def.uid = nextUid_++;
}

int MapDocument::indexOf(quint32 uid) const {
	for (int i = 0; i < map_.regs.size(); i++)
		if (map_.regs[i].uid == uid) return i;
	return -1;
}

const RegDef *MapDocument::reg(quint32 uid) const {
	const int i = indexOf(uid);
	return i < 0 ? nullptr : &map_.regs[i];
}

QStringList MapDocument::groups() const {
	QStringList groups;
	for (const RegDef &def : map_.regs)
		if (!groups.contains(def.group)) groups << def.group;
	return groups;
}

uint16_t MapDocument::freeAddressAfter(uint16_t from, int size) const {
	int addr = from;
	for (bool moved = true; moved;) {
		moved = false;
		for (const RegDef &def : map_.regs) {
			/* overlaps [addr, addr + size): go past it and look again */
			if (int(def.addr) < addr + size && addr < int(def.addr) + def.size) {
				addr = def.addr + def.size;
				moved = true;
			}
		}
	}
	return uint16_t(std::min(addr, 0x10000 - std::max(1, size)));
}

const QVector<MapIssue> &MapDocument::issues() const {
	if (!issuesValid_) {
		issues_ = checkMap(map_);
		issuesValid_ = true;
	}
	return issues_;
}
