/* SPDX-License-Identifier: Apache-2.0 */
/* The USB devices plugged into the phone (PortList, a list model for QML): the Connect drawer's port list, from
 * UsbBridge::devices() as the desktop's sidebar reads them on Android (Sidebar::refreshPorts). A device whose USB IDs
 * are the map's ("usb" in the map) is named by the map's device. PhoneSession refreshes it when the drawer opens and
 * every two seconds while the drawer is open on USB, so a board plugged in or out shows; the rows change only when
 * the devices did, so the box keeps its choice. */
#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "evre/usb_bridge.h"

struct DeviceMap;

class PortList : public QAbstractListModel {
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("the ports are PhoneSession's: PhoneSession.ports")
	Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
	enum Role { NameRole = Qt::UserRole + 1, ProductRole, VidRole, PidRole, MappedRole, LabelRole };

	explicit PortList(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;
	int count() const { return int(rows_.size()); }

	/* the bridge's devices now (none without a bridge: on a desktop), named by `map` where its USB IDs are theirs */
	void refresh(evre::UsbBridge *bridge, const DeviceMap &map);
	Q_INVOKABLE int indexOf(const QString &name) const;
	/* "name · what · vid:pid", as the desktop's port box writes a port (Sidebar::addPort) */
	static QString label(const evre::UsbBridge::Device &device, const QString &mapped);

signals:
	void countChanged();

private:
	struct Row {
		evre::UsbBridge::Device device;
		QString mapped; /* the map's device name when its USB IDs are this device's */
	};
	QVector<Row> rows_;
};
