/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's Monitor (FrameLog, a list model for QML): the frames on the link as the engine writes them for the
 * desktop's Monitor tab (IoEngine::takeFrames: "14:03:12.345  TX  01 03 ..."), each split into its time, its
 * direction (TX, RX, DEV->, ->DEV; the protocol's words, as the desktop shows them) and its bytes and note, the newest
 * MAX_LINES kept. Each frame's side (sent: TX, ->DEV; received: RX, DEV->; a note of its own) for the screen's TX and
 * RX filter (a RowFilter over "side"), counted, and its function's name (READ, READ_RESP ...: evre::Parser and fnName,
 * the engine's) with its kind for the label's colour. decode() spells a frame out field by field as the desktop's
 * Dump does (START, SLAVE, FUNCTION, OFFSET, COUNT, DATA, CRC, END: the bytes, what they mean, and the Dump's words for
 * each), the registers its offset and count cover named from the map (setNamer, the session's). PhoneSession feeds it once a frame while the Monitor screen shows and it is not paused (the engine
 * writes no frames otherwise: at fast polling they are a lot of text); a pile the engine could not hand over in time
 * is said in a line of its own. Pause holds what is there; Clear empties it. */
#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <functional>
#include <QtQml/qqmlregistration.h>

class FrameLog : public QAbstractListModel {
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("the Monitor's frames are PhoneSession's: PhoneSession.frames")
	Q_PROPERTY(int count READ count NOTIFY countChanged)
	Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
	Q_PROPERTY(int sentCount READ sentCount NOTIFY countChanged)
	Q_PROPERTY(int receivedCount READ receivedCount NOTIFY countChanged)

public:
	enum Role { TimeRole = Qt::UserRole + 1, DirectionRole, BytesRole, NoteRole, SideRole, TypeRole, KindRole };
	/* which way a frame went, for the filter: a note (frames not shown) is neither */
	enum Side { Sent = 0, Received = 1, NoteSide = 2 };
	Q_ENUM(Side)
	static constexpr int MAX_LINES = 5000;

	explicit FrameLog(QObject *parent = nullptr) : QAbstractListModel(parent) {}

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;
	int count() const { return int(lines_.size()); }
	int sentCount() const { return sides_[Sent]; }
	int receivedCount() const { return sides_[Received]; }
	/* the map's registers a frame's offset and count cover, by slave, address and count: "UPTIME, SUPPLY_V" */
	void setNamer(std::function<QString(int slave, int addr, int count)> namer) { namer_ = std::move(namer); }
	/* a frame's bytes as the Monitor shows them ("7B 01 AA ...") and its direction: a row per field, each a map of
	 * name (the protocol's word), bytes, meaning and words (the Dump's tooltip); empty: not one whole EVRe frame */
	Q_INVOKABLE QVariantList decode(const QString &bytes, const QString &direction) const;
	/* a frame's bytes on the clipboard (its sheet's Copy) */
	Q_INVOKABLE void copyText(const QString &text) const;
	bool paused() const { return paused_; }
	void setPaused(bool paused);

	/* the engine's lines since the last take, and how many it dropped before them; nothing while paused */
	void add(const QStringList &lines, int dropped);
	Q_INVOKABLE void clear();
	qint64 added() const { return added_; } /* tests: every line taken since the start */
	QString line(int row) const;           /* tests: a row as the engine wrote it */

signals:
	void countChanged();
	void pausedChanged();

private:
	struct Line {
		QString time, direction, bytes, note, type, kind;
		int side = NoteSide;
	};
	static Line parse(const QString &text);
	QVector<Line> lines_;
	int sides_[3] = { 0, 0, 0 };
	std::function<QString(int, int, int)> namer_;
	bool paused_ = false;
	qint64 added_ = 0;
};
