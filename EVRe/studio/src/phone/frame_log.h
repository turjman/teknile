/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's Monitor (FrameLog, a list model for QML): the frames on the link as the engine writes them for the
 * desktop's Monitor tab (IoEngine::takeFrames: "14:03:12.345  TX  01 03 ..."), each split into its time, its
 * direction (TX, RX, DEV->, ->DEV; the protocol's words, as the desktop shows them) and its bytes and note, the newest
 * MAX_LINES kept. PhoneSession feeds it once a frame while the Monitor screen shows and it is not paused (the engine
 * writes no frames otherwise: at fast polling they are a lot of text); a pile the engine could not hand over in time
 * is said in a line of its own. Pause holds what is there; Clear empties it. */
#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

class FrameLog : public QAbstractListModel {
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("the Monitor's frames are PhoneSession's: PhoneSession.frames")
	Q_PROPERTY(int count READ count NOTIFY countChanged)
	Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)

public:
	enum Role { TimeRole = Qt::UserRole + 1, DirectionRole, BytesRole, NoteRole };
	static constexpr int MAX_LINES = 5000;

	explicit FrameLog(QObject *parent = nullptr) : QAbstractListModel(parent) {}

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;
	int count() const { return int(lines_.size()); }
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
		QString time, direction, bytes, note;
	};
	static Line parse(const QString &text);
	QVector<Line> lines_;
	bool paused_ = false;
	qint64 added_ = 0;
};
