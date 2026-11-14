/* SPDX-License-Identifier: Apache-2.0 */
/* The Registers screen's rows (RegisterList, a list model for QML) over the desktop's RegisterModel: a table's ten
 * columns are no use at 412 px, so each register is one row with the roles a card shows. The values, their texts, the
 * glow and the plot are RegisterModel's, as on the desktop; this class only lays them out as a list.
 *
 *   order     the groups in the order of their first register in the map, each group's registers in the map's order
 *             (the screen's sections need a group's rows together; the example map's System comes back after Power)
 *   search    RegisterFilter's, the desktop's search box: the name, description, group, unit or address
 *   changes   RegisterModel's dataChanged forwarded row by row, with the roles that changed; its glow's own repaints
 *             (every 50 ms while a value fades) only when a new glow begins: the screen fades it by itself
 *   stale     a value not refreshed for a while (RegisterModel::setStaleAfterMs) is looked at every STALE_CHECK_MS, as
 *             time passes without a change
 *   trend     the last TREND_POINTS values a plottable plain number was read with (no enum, bit fields or hex: their
 *             line says nothing; PhoneSession::sync gives them, at the values' pace), for the sheet's small line; a ring per register, made at its first value, so the
 *             memory stays TREND_POINTS x 16 bytes a register at most; cleared with another map
 *
 * The rows QML gets are this list's; `source` is the row in RegisterModel, which PhoneSession's write and plot take. */
#pragma once

#include <QAbstractListModel>
#include <QTimer>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "model/register_model.h"

class RegisterList : public QAbstractListModel {
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("the registers are PhoneSession's: PhoneSession.registerList")
	Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
	Q_PROPERTY(int count READ count NOTIFY countChanged)
	Q_PROPERTY(int total READ total NOTIFY countChanged)
	Q_PROPERTY(int glowMs READ glowMs CONSTANT)

public:
	enum Role {
		NameRole = Qt::UserRole + 1,
		GroupRole,
		AddrRole,      /* "0xD080" */
		ValueRole,     /* the value column's text: the number, "—" not read yet, "not available", "error" */
		UnitRole,
		DecodedRole,   /* an enum's name, the bit fields, a special value's name */
		GlowRole,      /* how strongly it glows now, 1 .. 0 (signalled only when a glow begins) */
		DirectionRole, /* that change: +1 up, -1 down, 0 no arrow */
		WritableRole,  /* the map lets it be written */
		PlottedRole,
		ErrorRole,     /* why there is no value (the device's answer), empty when there is one */
		StaleRole,     /* the value was not refreshed for a while */
		PlottableRole, /* a number the map lets plot */
		DangerRole,    /* a write moves, powers or changes something: asked first */
		SourceRole,    /* the row in RegisterModel */
		NumericRole    /* a number (not bytes): the sheet's field brings up the number pad */
	};
	static constexpr int STALE_CHECK_MS = 250;
	static constexpr int TREND_POINTS = 120;

	explicit RegisterList(RegisterModel *model, QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	QString search() const { return search_; }
	void setSearch(const QString &text);
	int count() const { return int(shown_.size()); }
	int total() const { return int(model_->rows().size()); }
	int sourceRow(int row) const { return row >= 0 && row < shown_.size() ? shown_[row] : -1; }
	Q_INVOKABLE int rowOf(int source) const { return source >= 0 && source < listRow_.size() ? listRow_[source] : -1; }
	int glowMs() const { return int(RegisterModel::GLOW_MS); } /* a glow's fade, the desktop's */
	/* one register's roles by its source row, as a map of the role names (the write sheet's) */
	Q_INVOKABLE QVariantMap entry(int source) const;
	/* "SETPOINT = 25.0 °C" on the clipboard (the detail sheet's Copy); the text, or empty */
	Q_INVOKABLE QString copy(int source) const;
	/* a value read at `ms` (the engine's clock) into the register's trend; only a plottable plain number keeps one */
	void sample(int source, qint64 ms, double value);
	/* the trend for the sheet: { points: [Qt.point(seconds before the newest, value)], min, max (numbers), minText,
	 * maxText (as the value column writes them), seconds (the span) }; empty under two points */
	Q_INVOKABLE QVariantMap trend(int source) const;
	int trendSize(int source) const { return source >= 0 && source < trends_.size() ? trends_[source].size : 0; }

signals:
	void searchChanged();
	void countChanged();

private:
	void rebuild();
	void forward(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles);
	void checkStale();
	bool isStale(int source) const;
	bool showsError(int source) const;

	RegisterModel *model_;
	RegisterFilter filter_; /* the desktop's search, asked row by row in rebuild() */
	QString search_;
	QVector<int> shown_;    /* the source rows shown, in the list's order */
	QVector<int> listRow_;  /* each source row's row here, -1 when the search hides it */
	QVector<qint64> lit_;   /* each source row's glow start last signalled */
	QVector<bool> stale_;   /* each source row's stale state last signalled */
	QTimer staleTimer_;
	/* a register's last values, oldest first from `head` round; empty until its first value */
	struct Trend {
		QVector<qint64> ms;
		QVector<double> values;
		int head = 0, size = 0;
	};
	QVector<Trend> trends_;
};
