/* SPDX-License-Identifier: Apache-2.0 */
/* The USB devices plugged into the phone: see port_list.h. */
#include "phone/port_list.h"

#include <QStringList>

#include "model/device_map.h"

PortList::PortList(QObject *parent) : QAbstractListModel(parent) {}

int PortList::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(rows_.size()); }

QVariant PortList::data(const QModelIndex &index, int role) const {
	if (!index.isValid() || index.row() >= rows_.size()) return {};
	const Row &row = rows_[index.row()];
	switch (role) {
	case NameRole: return row.device.name;
	case ProductRole: return row.device.product;
	case VidRole: return int(row.device.vid);
	case PidRole: return int(row.device.pid);
	case MappedRole: return row.mapped;
	case LabelRole:
	case Qt::DisplayRole: return label(row.device, row.mapped);
	default: return {};
	}
}

QHash<int, QByteArray> PortList::roleNames() const {
	return { { NameRole, "name" }, { ProductRole, "product" }, { VidRole, "vid" }, { PidRole, "pid" },
		{ MappedRole, "mapped" }, { LabelRole, "label" } };
}

void PortList::refresh(evre::UsbBridge *bridge, const DeviceMap &map) {
	QVector<Row> rows;
	if (bridge) {
		for (const evre::UsbBridge::Device &device : bridge->devices()) {
			const bool mapped = map.usbVid && device.vid == map.usbVid && device.pid == map.usbPid;
			rows.append({ device, mapped ? map.device : QString() });
		}
	}
	/* the same devices under the same names: nothing moves, and the box keeps its choice */
	bool same = rows.size() == rows_.size();
	for (int i = 0; same && i < rows.size(); i++) {
		const Row &a = rows[i], &b = rows_[i];
		same = a.device.name == b.device.name && a.device.vid == b.device.vid && a.device.pid == b.device.pid
				&& a.device.product == b.device.product && a.mapped == b.mapped;
	}
	if (same) return;
	beginResetModel();
	rows_ = rows;
	endResetModel();
	emit countChanged();
}

int PortList::indexOf(const QString &name) const {
	for (int i = 0; i < rows_.size(); i++)
		if (rows_[i].device.name == name) return i;
	return -1;
}

QString PortList::label(const evre::UsbBridge::Device &device, const QString &mapped) {
	QStringList parts{ device.name };
	const QString what = mapped.isEmpty() ? device.product : mapped;
	if (!what.isEmpty()) parts << what;
	parts << QStringLiteral("%1:%2").arg(device.vid, 4, 16, QLatin1Char('0')).arg(device.pid, 4, 16, QLatin1Char('0'));
	return parts.join(QStringLiteral(" · "));
}
