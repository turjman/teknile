/* SPDX-License-Identifier: Apache-2.0 */
/* What Windows says of a COM port, for usbPcapPortState (usb_capture.h), which decides from it. Built on Windows only.
 *
 * It opens no COM port. The registry and the device tree are only read; a hub answers
 * IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX from what it knows. Only the configuration descriptor reaches the device: the
 * hub fetches it, the standard request every USB host makes, and only when `descriptors` asks for it, which it never
 * does while a capture runs. None of it needs administrator rights: USBPcap's control devices let anyone open them for
 * the hub's name, and only a capture's read and write access is refused.
 */
#include "io/usb_capture.h"

#include <QByteArray>
#include <QFile>
#include <QSettings>

#include <windows.h>
#include <cfgmgr32.h>
#include <devpropdef.h>
#include <setupapi.h>
#include <usbioctl.h>
#include <winioctl.h>

#include <cstddef>
#include <vector>

namespace {

/* The system's fixed values, written out: its headers define them only under initguid.h, included before windows.h */
constexpr GUID COM_PORT_INTERFACE = { 0x86E0D1E0, 0x8089, 0x11D0, { 0x9C, 0xE4, 0x08, 0x00, 0x3E, 0x30, 0x1F, 0x73 } };
constexpr GUID PORTS_CLASS = { 0x4D36E978, 0xE325, 0x11CE, { 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18 } };
constexpr GUID USB_HUB_INTERFACE = { 0xF18A0E88, 0xC30C, 0x11D0, { 0x88, 0x15, 0x00, 0xA0, 0xC9, 0x06, 0xBE, 0xD8 } };
const DEVPROPKEY DEVICE_STACK = { { 0x540B947E, 0x8B40, 0x45BC, { 0xA8, 0xA2, 0x6A, 0x0B, 0x89, 0x4C, 0xBD, 0xA2 } }, 14 };

/* USBPcap has no public header for programs: this is USBPcapDriver/include/USBPcap.h's own definition, 0x0022200C, read
 * there and tried on the driver. FILE_ANY_ACCESS: it needs no rights on the control device */
constexpr DWORD IOCTL_USBPCAP_GET_HUB_SYMLINK = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS);

constexpr int MAX_FILTERS = 32;         /* one per root hub; a number may be missing after a dock went */
constexpr int MAX_LINK = 512;
constexpr int MAX_PIPES = 30;           /* what the hub's answer has room for: USB's limit on a device */
constexpr ULONG MAX_CONFIGURATION = 4096;

struct Handle {
	explicit Handle(HANDLE handle) : value(handle) {}
	~Handle() {
		if (ok()) CloseHandle(value);
	}
	Handle(const Handle &) = delete;
	Handle &operator=(const Handle &) = delete;
	bool ok() const { return value != INVALID_HANDLE_VALUE; }
	HANDLE value;
};

QString instanceOf(DEVINST node) {
	wchar_t id[MAX_DEVICE_ID_LEN + 1] = {};
	return CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN + 1, 0) == CR_SUCCESS ? QString::fromWCharArray(id) : QString();
}

QString portNameOf(HDEVINFO set, SP_DEVINFO_DATA &node) {
	const HKEY key = SetupDiOpenDevRegKey(set, &node, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
	if (key == HKEY(INVALID_HANDLE_VALUE)) return {};
	wchar_t name[64] = {};
	DWORD size = sizeof(name) - sizeof(wchar_t), type = 0;
	const LSTATUS read = RegQueryValueExW(key, L"PortName", nullptr, &type, reinterpret_cast<BYTE *>(name), &size);
	RegCloseKey(key);
	return read == ERROR_SUCCESS && type == REG_SZ ? QString::fromWCharArray(name) : QString();
}

DEVINST portNodeIn(const QString &port, const GUID &kind, DWORD flags) {
	const HDEVINFO set = SetupDiGetClassDevsW(&kind, nullptr, nullptr, flags);
	if (set == INVALID_HANDLE_VALUE) return 0;
	DEVINST found = 0;
	SP_DEVINFO_DATA node{};
	node.cbSize = sizeof(node);
	for (DWORD i = 0; !found && SetupDiEnumDeviceInfo(set, i, &node); i++)
		if (portNameOf(set, node).compare(port, Qt::CaseInsensitive) == 0) found = node.DevInst;
	SetupDiDestroyDeviceInfoList(set);
	return found;
}

/* The serial ports' interface, or failing that the Ports class: a vendor's driver need not register the interface */
DEVINST portNode(const QString &port) {
	const DEVINST found = portNodeIn(port, COM_PORT_INTERFACE, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	return found ? found : portNodeIn(port, PORTS_CLASS, DIGCF_PRESENT);
}

/* the drivers on a node, top first */
QStringList stackOf(DEVINST node) {
	DEVPROPTYPE type = 0;
	ULONG size = 0;
	if (CM_Get_DevNode_PropertyW(node, &DEVICE_STACK, &type, nullptr, &size, 0) != CR_BUFFER_SMALL || !size) return {};
	std::vector<wchar_t> names(size / sizeof(wchar_t));
	if (CM_Get_DevNode_PropertyW(node, &DEVICE_STACK, &type, reinterpret_cast<BYTE *>(names.data()), &size, 0) != CR_SUCCESS
			|| type != DEVPROP_TYPE_STRING_LIST)
		return {};
	return QString::fromWCharArray(names.data(), qsizetype(names.size())).split(QChar(0), Qt::SkipEmptyParts);
}

/* a hub's device interface, the path it opens by */
QString hubPath(DEVINST hub) {
	const QString instance = instanceOf(hub);
	const auto id = reinterpret_cast<DEVINSTID_W>(const_cast<ushort *>(instance.utf16()));
	GUID kind = USB_HUB_INTERFACE;
	ULONG size = 0;
	if (CM_Get_Device_Interface_List_SizeW(&size, &kind, id, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || size < 2)
		return {};
	std::vector<wchar_t> list(size);
	if (CM_Get_Device_Interface_ListW(&kind, id, list.data(), size, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
		return {};
	return QString::fromWCharArray(list.data());
}

void askHub(DEVINST device, DEVINST hub, UsbPortFacts &facts, bool descriptors) {
	DWORD connection = 0, size = sizeof(connection);
	if (CM_Get_DevNode_Registry_PropertyW(device, CM_DRP_ADDRESS, nullptr, &connection, &size, 0) != CR_SUCCESS) return;
	const QString path = hubPath(hub);
	if (path.isEmpty()) return;
	const Handle file(CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
			OPEN_EXISTING, 0, nullptr));
	if (!file.ok()) return;

	QByteArray answer(int(sizeof(USB_NODE_CONNECTION_INFORMATION_EX) + MAX_PIPES * sizeof(USB_PIPE_INFO)), '\0');
	auto *node = reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX *>(answer.data());
	node->ConnectionIndex = connection;
	DWORD got = 0;
	if (!DeviceIoControl(file.value, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, node, DWORD(answer.size()), node,
			DWORD(answer.size()), &got, nullptr) || node->ConnectionStatus != DeviceConnected)
		return;
	facts.address = node->DeviceAddress;
	if (!descriptors) return;

	const int header = int(offsetof(USB_DESCRIPTOR_REQUEST, Data));
	QByteArray request(header + int(MAX_CONFIGURATION), '\0');
	auto *ask = reinterpret_cast<USB_DESCRIPTOR_REQUEST *>(request.data());
	ask->ConnectionIndex = connection;
	ask->SetupPacket.wValue = USB_CONFIGURATION_DESCRIPTOR_TYPE << 8; /* the first configuration, as USBPcapCMD's own */
	ask->SetupPacket.wLength = USHORT(MAX_CONFIGURATION);
	if (DeviceIoControl(file.value, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, ask, DWORD(request.size()), ask,
			DWORD(request.size()), &got, nullptr) && int(got) > header)
		facts.configuration = request.mid(header, int(got) - header);
}

/* Access 0, as USBPcapCMD's own enumeration: a device can be opened that way by anyone and while a capture holds it */
QStringList filterLinks() {
	QStringList links;
	for (int number = 1; number <= MAX_FILTERS; number++) {
		const QString name = QStringLiteral("\\\\.\\USBPcap%1").arg(number);
		const Handle file(CreateFileW(reinterpret_cast<const wchar_t *>(name.utf16()), 0, 0, nullptr, OPEN_EXISTING, 0, nullptr));
		wchar_t link[MAX_LINK] = {};
		DWORD got = 0;
		const bool said = file.ok() && DeviceIoControl(file.value, IOCTL_USBPCAP_GET_HUB_SYMLINK, nullptr, 0, link,
				sizeof(link) - sizeof(wchar_t), &got, nullptr);
		links << (said ? QString::fromWCharArray(link) : QString());
	}
	return links;
}

/* The test USBPcapCMD makes itself: below this it asks for a worker with a UAC prompt */
bool runsElevated() {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
	TOKEN_ELEVATION elevation{};
	DWORD size = sizeof(elevation);
	const bool read = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
	CloseHandle(token);
	return read && elevation.TokenIsElevated;
}

} // namespace

UsbPortFacts usbPcapFacts(const QString &port, bool descriptors) {
	UsbPortFacts facts;
	facts.port = port;
	QVector<DEVINST> nodes;
	for (DEVINST node = portNode(port); node; ) {
		nodes << node;
		facts.chain << instanceOf(node);
		DEVINST parent = 0;
		node = CM_Get_Parent(&parent, node, 0) == CR_SUCCESS ? parent : 0;
	}
	const UsbChain chain = usbChainOf(facts.chain);
	if (chain.device < 0) return facts;
	facts.stack = stackOf(nodes[chain.device]);
	askHub(nodes[chain.device], nodes[chain.device + 1], facts, descriptors);
	facts.filters = filterLinks();
	facts.program = !usbPcapProgram().isEmpty();
	facts.elevated = runsElevated();
	return facts;
}

QString usbPcapProgram() {
	const QString usual = qEnvironmentVariable("ProgramFiles") + QLatin1String("/USBPcap/USBPcapCMD.exe");
	if (QFile::exists(usual)) return usual;
	const QSettings uninstall(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\USBPcap"),
			QSettings::NativeFormat);
	const QString beside = usbPcapProgramBeside(uninstall.value(QStringLiteral("UninstallString")).toString());
	return !beside.isEmpty() && QFile::exists(beside) ? beside : QString();
}
