/* SPDX-License-Identifier: Apache-2.0 */
/* The map being edited: one DeviceMap, the undo history of every change to
 * it, and whether it is saved.
 *
 * Every part of the window that changes the map (the Map editor tab, the Map
 * settings dialog, the Registers tab) does it through edit(): the change is
 * made on a copy, the copy becomes the map, and the step goes on the undo
 * stack. Whoever shows the map follows changed().
 *
 * Each register gets a uid (RegDef::uid) when it comes into the document: it
 * stays the same while the register is edited (even its address), so the
 * table keeps its live value and the editor its selection. A copy gets a new one.
 *
 * Steps with the same merge key follow each other into one (typing a name is
 * one undo step, not one per letter) until endMerge(), called when an editor
 * loses the focus.
 */
#pragma once

#include <QObject>
#include <QVector>
#include <functional>

#include "model/device_map.h"

class QUndoStack;

class MapDocument : public QObject {
	Q_OBJECT
public:
	explicit MapDocument(QObject *parent = nullptr);

	const DeviceMap &map() const { return map_; }
	QUndoStack *undoStack() const { return undo_; }

	/* a map loaded or new: no undo history, and saved */
	void reset(const DeviceMap &map);
	/* saved to `path`: the map's path, and no changes to save */
	void markSaved(const QString &path);
	bool isModified() const;

	/* One undoable step: `change` is applied to a copy of the map; the registers are then
	 * sorted by address and new ones get a uid. mergeKey > 0: the step joins the one before
	 * it when that one has the same key (see endMerge). */
	void edit(const QString &text, const std::function<void(DeviceMap &)> &change, int mergeKey = 0);
	/* the next step starts a new undo step, even with the same merge key */
	void endMerge() { mergeSalt_++; }

	int indexOf(quint32 uid) const; /* -1 if no register has it */
	const RegDef *reg(quint32 uid) const;
	quint32 newUid() { return nextUid_++; }
	/* the map's groups, in the order they first come */
	QStringList groups() const;
	/* the next address after `def` that no register uses (for a new or copied register of `size` bytes) */
	uint16_t freeAddressAfter(uint16_t from, int size) const;
	/* checkMap() of the map as it is, kept until the next change */
	const QVector<MapIssue> &issues() const;

signals:
	void changed();                /* anything: registers, settings, or both */
	void modifiedChanged(bool modified);

private:
	friend class MapEditCommand;
	void setMap(const DeviceMap &map); /* for the undo stack: redo and undo */
	void giveUids(DeviceMap &map);

	DeviceMap map_;
	QUndoStack *undo_;
	quint32 nextUid_ = 1;
	int mergeSalt_ = 0;
	mutable QVector<MapIssue> issues_;
	mutable bool issuesValid_ = false;
};
