/* SPDX-License-Identifier: Apache-2.0 */
/* The map files, without a window or a device (QtTest):
 *
 *   - every map in maps/ beside the program, and every file named in
 *     EVRE_MAP_TEST_FILES (separated by ';'), saves back byte for byte;
 *   - an edit changes only the edited register's text, in its own layout;
 *   - keys the Studio does not know, hex value names, numbers as typed stay;
 *   - the keys min, max, default, special and decimals;
 *   - overlays ("extends"): merge, save only the differences, flatten;
 *   - the checks of checkMap();
 *   - the exports: Markdown, a C header (compiled by gcc when it is on PATH),
 *     a Python module (imported by python when it is on PATH), CSV and back.
 *
 *   evre_map_test                     (run from the build folder)
 */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "model/bus_file.h"
#include "model/device_map.h"
#include "model/map_export.h"

namespace {

QByteArray readFile(const QString &file) {
	QFile input(file);
	return input.open(QIODevice::ReadOnly) ? input.readAll() : QByteArray();
}

void writeFile(const QString &file, const QByteArray &text) {
	QFile output(file);
	QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
	output.write(text);
}

RegDef *byName(DeviceMap &map, const QString &name) {
	for (RegDef &def : map.regs)
		if (def.name == name) return &def;
	return nullptr;
}

/* the lines of `after` that are not in `before`, and the other way round (a plain LCS line diff) */
void lineDiff(const QByteArray &before, const QByteArray &after, QStringList &removed, QStringList &added) {
	const QList<QByteArray> a = before.split('\n'), b = after.split('\n');
	const int n = int(a.size()), m = int(b.size());
	std::vector<std::vector<int>> lcs(size_t(n + 1), std::vector<int>(size_t(m + 1), 0));
	for (int i = n - 1; i >= 0; i--)
		for (int j = m - 1; j >= 0; j--)
			lcs[size_t(i)][size_t(j)] = a[i] == b[j] ? lcs[size_t(i + 1)][size_t(j + 1)] + 1
					: std::max(lcs[size_t(i + 1)][size_t(j)], lcs[size_t(i)][size_t(j + 1)]);
	int i = 0, j = 0;
	while (i < n && j < m) {
		if (a[i] == b[j]) {
			i++;
			j++;
		} else if (lcs[size_t(i + 1)][size_t(j)] >= lcs[size_t(i)][size_t(j + 1)]) {
			removed << QString::fromUtf8(a[i++]);
		} else {
			added << QString::fromUtf8(b[j++]);
		}
	}
	while (i < n) removed << QString::fromUtf8(a[i++]);
	while (j < m) added << QString::fromUtf8(b[j++]);
}

/* a map as Python's json.dump(indent=2) writes one */
const char *PRETTY_MAP = R"JSON({
  "format": "evre-map/1",
  "device": "Pretty device",
  "slave": 1,
  "generated_from": [
    "a.md"
  ],
  "registers": [
    {
      "addr": "0xD000",
      "name": "VOLTAGE",
      "access": "ro",
      "group": "Power",
      "type": "f32",
      "unit": "V",
      "note": "kept: the Studio does not know this key"
    },
    {
      "addr": "0xD004",
      "name": "MODE",
      "access": "rw",
      "group": "Power",
      "type": "u8",
      "enum": {
        "0x00": "off",
        "0x10": "on"
      }
    },
    {
      "addr": "0xD006",
      "name": "FLAGS",
      "access": "ro",
      "group": "Power",
      "type": "u16",
      "fields": [
        {
          "name": "READY",
          "bits": "0",
          "color": "green"
        },
        {
          "name": "LEVEL",
          "bits": "7:4"
        }
      ]
    }
  ]
}
)JSON";

/* the keys that are new in the format */
const char *LIMITS_MAP = R"JSON({ "format": "evre-map/1", "device": "Limits", "registers": [
  { "addr": "0x0010", "name": "SETPOINT", "type": "f32", "unit": "V", "access": "rw",
    "min": 2.0, "max": 3.65, "default": 3.5, "decimals": 2 },
  { "addr": "0x0014", "name": "WATCHDOG", "type": "u8", "unit": "s", "access": "rw",
    "min": 10, "max": 255, "default": "off", "special": { "0": "off" } },
  { "addr": "0x0015", "name": "LOAD", "type": "i16", "unit": "mA", "special": { "-1": "not measured" } },
  { "addr": "0x0018", "name": "LEVEL", "type": "i16", "unit": "bar", "scale": 0.01, "special": { "0.5": "half" } }
] }
)JSON";

const char *BASE_MAP = R"JSON({
  "format": "evre-map/1",
  "device": "Base device",
  "slave": 1,
  "registers": [
    { "addr": "0x0000", "name": "ID", "type": "u16", "access": "ro", "group": "Info" },
    { "addr": "0x0002", "name": "SPEED", "type": "i16", "unit": "rpm", "access": "rw", "group": "Drive" },
    { "addr": "0x0004", "name": "TEMP", "type": "f32", "unit": "C", "access": "ro", "group": "Info" },
    { "addr": "0x0008", "name": "OLD", "type": "u8", "access": "ro", "group": "Info" }
  ]
}
)JSON";

const char *OVERLAY_MAP = R"JSON({
  "format": "evre-map/1",
  "extends": "base/base.json",
  "device": "Overlay device",
  "registers": [
    { "addr": "0x0002", "danger": true, "unit": null },
    { "addr": "0x0008", "remove": true },
    { "addr": "0x0010", "name": "EXTRA", "type": "u8", "access": "rw", "group": "Extra" }
  ]
}
)JSON";

/* every key the CSV has a column for, with the characters its compact columns escape */
const char *CSV_MAP = R"JSON({ "format": "evre-map/1", "device": "Csv",
  "registers": [
  { "addr": "0x0010", "name": "MODE", "type": "u8", "access": "rw", "group": "A, B", "desc": "has \u0022quotes\u0022, commas",
    "enum": { "0x0": "off", "0x1": "on; really" }, "persist": true, "plot": false, "default": 1, "notes": "line 1\nline 2" },
  { "addr": "0x0012", "name": "FLAGS", "type": "u16", "access": "rw", "write": "w1c", "group": "A, B",
    "fields": [ { "name": "OVER|RUN", "bits": "0", "access": "w1c", "desc": "a # in it" },
                { "name": "CODE", "bits": "7:4", "values": { "1": "a=b", "2": "{x}" } } ] },
  { "addr": "0x0014", "name": "LEVEL", "type": "i16", "unit": "bar", "scale": 0.01, "decimals": 2, "min": -1, "max": 5,
    "special": { "-1": "unset" }, "format": "hex", "danger": true, "group": "C" },
  { "addr": "0x0016", "name": "KEY", "type": "bytes", "size": 16, "access": "wo", "group": "C" }
] }
)JSON";

} // namespace

class MapTest : public QObject {
	Q_OBJECT

private:
	QTemporaryDir tmp_;
	QString path(const QString &name) const { return tmp_.filePath(name); }
	DeviceMap loadText(const QString &name, const QByteArray &text) {
		writeFile(path(name), text);
		DeviceMap map;
		QString err;
		if (!map.load(path(name), err)) qWarning("load %s: %s", qPrintable(name), qPrintable(err));
		return map;
	}

private slots:
	void initTestCase() { QVERIFY(tmp_.isValid()); }

	/* every map we ship, and the ones named in EVRE_MAP_TEST_FILES, save back byte for byte */
	void roundTrip_data() {
		QTest::addColumn<QString>("file");
		const QDir maps(QCoreApplication::applicationDirPath() + QStringLiteral("/maps"));
		for (const QString &name : maps.entryList({ QStringLiteral("*.json") }, QDir::Files))
			if (!readFile(maps.filePath(name)).contains("\"evre-bus/")) /* a bus file there is no map: busFile below */
				QTest::newRow(qPrintable(name)) << maps.filePath(name);
		for (const QString &file : qEnvironmentVariable("EVRE_MAP_TEST_FILES").split(QLatin1Char(';'), Qt::SkipEmptyParts))
			QTest::newRow(qPrintable(QFileInfo(file).fileName())) << file;
		QTest::newRow("pretty") << QString(); /* PRETTY_MAP, written in init below */
	}
	void roundTrip() {
		QFETCH(QString, file);
		if (file.isEmpty()) {
			file = path(QStringLiteral("pretty.json"));
			writeFile(file, PRETTY_MAP);
		}
		DeviceMap map;
		QString err;
		QVERIFY2(map.load(file, err), qPrintable(err));
		QVERIFY(!map.regs.isEmpty());
		const QByteArray before = readFile(file);
		const QByteArray after = map.toJson(file);
		QStringList removed, added;
		lineDiff(before, after, removed, added);
		QVERIFY2(before == after, qPrintable(QStringLiteral("- %1\n+ %2").arg(removed.join("\n- "), added.join("\n+ "))));
		/* a copy of the map saves the same */
		const DeviceMap copy = map;
		QCOMPARE(copy.toJson(file), before);
	}

	/* one register edited in a compact map: its line changes, nothing else */
	void compactEdit() {
		const QString file = QCoreApplication::applicationDirPath() + QStringLiteral("/maps/example_device.json");
		DeviceMap map;
		QString err;
		QVERIFY2(map.load(file, err), qPrintable(err));
		RegDef *supply = byName(map, QStringLiteral("SUPPLY_I"));
		QVERIFY(supply);
		supply->unit = QStringLiteral("mA");
		const QByteArray before = readFile(file), after = map.toJson(file);
		QStringList removed, added;
		lineDiff(before, after, removed, added);
		QCOMPARE(removed.size(), 1);
		QCOMPARE(added.size(), 1);
		QVERIFY(removed[0].contains(QLatin1String("\"SUPPLY_I\"")));
		QCOMPARE(added[0], QString(removed[0]).replace(QLatin1String("\"unit\": \"A\""), QLatin1String("\"unit\": \"mA\"")));
		/* and it reads back */
		writeFile(path(QStringLiteral("compact.json")), after);
		DeviceMap again;
		QVERIFY2(again.load(path(QStringLiteral("compact.json")), err), qPrintable(err));
		QCOMPARE(byName(again, QStringLiteral("SUPPLY_I"))->unit, QStringLiteral("mA"));
		QCOMPARE(again.regs.size(), map.regs.size());
	}

	/* pretty: the edited register is written again in the same layout, its unknown key kept */
	void prettyEdit() {
		DeviceMap map = loadText(QStringLiteral("pretty.json"), PRETTY_MAP);
		RegDef *voltage = byName(map, QStringLiteral("VOLTAGE"));
		QVERIFY(voltage);
		voltage->unit = QStringLiteral("mV");
		voltage->desc = QStringLiteral("new description");
		const QByteArray after = map.toJson(path(QStringLiteral("pretty.json")));
		QStringList removed, added;
		lineDiff(PRETTY_MAP, after, removed, added);
		QCOMPARE(removed, QStringList{ QStringLiteral("      \"unit\": \"V\",") });
		/* desc goes after unit, where the Studio's key order puts it among the keys there */
		QCOMPARE(added, (QStringList{ QStringLiteral("      \"unit\": \"mV\","),
				QStringLiteral("      \"desc\": \"new description\",") }));
		QVERIFY(after.contains("\"note\": \"kept: the Studio does not know this key\""));
		QVERIFY(after.contains("\"generated_from\""));
	}

	/* hex value names stay hex, also when the names change */
	void hexNames() {
		DeviceMap map = loadText(QStringLiteral("pretty.json"), PRETTY_MAP);
		RegDef *mode = byName(map, QStringLiteral("MODE"));
		QVERIFY(mode && mode->enumHex);
		QCOMPARE(mode->enumValues.value(0x10), QStringLiteral("on"));
		mode->enumValues.insert(0x20, QStringLiteral("boost"));
		const QByteArray after = map.toJson(path(QStringLiteral("pretty.json")));
		QVERIFY(after.contains("\"0x20\": \"boost\""));
		QVERIFY(after.contains("\"0x10\": \"on\""));
		/* a field edited keeps its unknown key; the other field stays as written */
		RegDef *flags = byName(map, QStringLiteral("FLAGS"));
		flags->fields[0].name = QStringLiteral("READY_NOW");
		const QByteArray again = map.toJson(path(QStringLiteral("pretty.json")));
		QVERIFY(again.contains("\"name\": \"READY_NOW\""));
		QVERIFY(again.contains("\"color\": \"green\""));
		QVERIFY(again.contains("\"bits\": \"7:4\""));
	}

	/* a register added lands in address order; one removed takes nothing else with it */
	void addAndRemove() {
		const QString file = QCoreApplication::applicationDirPath() + QStringLiteral("/maps/example_device.json");
		DeviceMap map;
		QString err;
		QVERIFY2(map.load(file, err), qPrintable(err));
		RegDef added;
		added.addr = 0xD016;
		added.name = QStringLiteral("NEW_ONE");
		added.group = QStringLiteral("Sensors");
		map.regs.push_back(added);
		map.sort();
		map.regs.removeIf([](const RegDef &d) { return d.name == QLatin1String("COUNTER"); });
		const QByteArray before = readFile(file), after = map.toJson(file);
		QStringList removedLines, addedLines;
		lineDiff(before, after, removedLines, addedLines);
		QCOMPARE(removedLines.size(), 1);
		QVERIFY(removedLines[0].contains(QLatin1String("COUNTER")));
		QCOMPARE(addedLines.size(), 1);
		QVERIFY(addedLines[0].contains(QLatin1String("\"NEW_ONE\"")));
		QVERIFY(addedLines[0].startsWith(QLatin1String("    { \"addr\": \"0xD016\"")));
		/* the blank lines between the groups are still there */
		QCOMPARE(after.count("\n\n"), before.count("\n\n"));
	}

	/* a copied register: one of the two is the original, the other is new */
	void copiedRegister() {
		DeviceMap map = loadText(QStringLiteral("pretty.json"), PRETTY_MAP);
		RegDef copy = *byName(map, QStringLiteral("VOLTAGE"));
		copy.addr = 0xD010;
		copy.name = QStringLiteral("VOLTAGE_2");
		map.regs.push_back(copy);
		map.sort();
		const QByteArray after = map.toJson(path(QStringLiteral("pretty.json")));
		QStringList removed, added;
		lineDiff(PRETTY_MAP, after, removed, added);
		QVERIFY2(removed.isEmpty(), qPrintable(removed.join('\n')));
		QVERIFY(after.contains("\"name\": \"VOLTAGE_2\""));
		QCOMPARE(after.count("\"note\""), 2); /* the copy has the unknown key too */
	}

	/* min, max, default, special and decimals */
	void newKeys() {
		DeviceMap map = loadText(QStringLiteral("limits.json"), LIMITS_MAP);
		const RegDef *set = byName(map, QStringLiteral("SETPOINT"));
		QVERIFY(set);
		QCOMPARE(set->min, 2.0);
		QCOMPARE(set->max, 3.65);
		QCOMPARE(set->defaultValue, 3.5);
		QCOMPARE(set->decimals, 2);
		QCOMPARE(formatNumber(*set, 3.456), QStringLiteral("3.46"));
		QVERIFY(limitProblem(*set, 3.6).isEmpty());
		QVERIFY(limitProblem(*set, 3.65f).isEmpty()); /* a float read back is on the limit */
		QCOMPARE(limitProblem(*set, 3.7), QStringLiteral("above the maximum 3.65 V"));
		QCOMPARE(limitProblem(*set, 1.5), QStringLiteral("below the minimum 2 V"));

		const RegDef *dog = byName(map, QStringLiteral("WATCHDOG"));
		QCOMPARE(dog->defaultValue, 0.0); /* "default": "off" */
		QVERIFY(limitProblem(*dog, 0).isEmpty()); /* below min, but a special value */
		QVERIFY(!limitProblem(*dog, 5).isEmpty());
		QByteArray raw;
		QString err;
		QVERIFY(encodeValue(*dog, QStringLiteral("OFF"), raw, err));
		QCOMPARE(raw, QByteArray(1, '\0'));
		QCOMPARE(formatDecoded(*dog, raw), QStringLiteral("off"));
		QCOMPARE(formatDecoded(*dog, QByteArray(1, '\x1E')), QString());

		const RegDef *load = byName(map, QStringLiteral("LOAD"));
		QCOMPARE(formatDecoded(*load, QByteArray("\xFF\xFF", 2)), QStringLiteral("not measured"));
		const RegDef *level = byName(map, QStringLiteral("LEVEL"));
		QCOMPARE(formatDecoded(*level, QByteArray("\x32\x00", 2)), QStringLiteral("half")); /* 50 x 0.01 */

		/* unchanged: as written ("default": "off", 2.0 not 2); changed: the new values */
		QCOMPARE(map.toJson(path(QStringLiteral("limits.json"))), QByteArray(LIMITS_MAP));
		byName(map, QStringLiteral("SETPOINT"))->max = 3.6;
		const QByteArray after = map.toJson(path(QStringLiteral("limits.json")));
		QVERIFY(after.contains("\"max\": 3.6,"));
		QVERIFY(after.contains("\"min\": 2.0,"));
		QVERIFY(after.contains("\"default\": \"off\""));
	}

	/* an overlay: merged on load, only the differences saved */
	void overlay() {
		QDir(tmp_.path()).mkpath(QStringLiteral("base"));
		writeFile(path(QStringLiteral("base/base.json")), BASE_MAP);
		DeviceMap map = loadText(QStringLiteral("overlay.json"), OVERLAY_MAP);
		QVERIFY(map.isOverlay());
		QCOMPARE(map.device, QStringLiteral("Overlay device"));
		QCOMPARE(map.regs.size(), 4); /* ID, SPEED, TEMP, EXTRA: OLD removed */
		const RegDef *speed = byName(map, QStringLiteral("SPEED"));
		QVERIFY(speed && speed->danger && speed->unit.isEmpty() && speed->rw);
		QVERIFY(!byName(map, QStringLiteral("OLD")));
		QVERIFY(byName(map, QStringLiteral("EXTRA")));

		const QString file = path(QStringLiteral("overlay.json"));
		QCOMPARE(map.toJson(file), QByteArray(OVERLAY_MAP));

		/* a base register changed: the overlay gets an item with just that key */
		byName(map, QStringLiteral("TEMP"))->unit = QStringLiteral("K");
		/* SPEED back to the base's unit: its "unit": null goes */
		byName(map, QStringLiteral("SPEED"))->unit = QStringLiteral("rpm");
		/* the base's ID removed */
		map.regs.removeIf([](const RegDef &d) { return d.name == QLatin1String("ID"); });
		const QByteArray after = map.toJson(file);
		QVERIFY2(after.contains("{ \"addr\": \"0x0004\", \"unit\": \"K\" }"), after.constData());
		QVERIFY2(after.contains("{ \"addr\": \"0x0002\", \"danger\": true }"), after.constData());
		QVERIFY2(after.contains("{ \"addr\": \"0x0000\", \"remove\": true }"), after.constData());
		QVERIFY(after.contains("{ \"addr\": \"0x0008\", \"remove\": true }"));
		QVERIFY(!after.contains("\"Base device\""));

		/* saved and loaded again: the same map */
		writeFile(file, after);
		DeviceMap again;
		QString err;
		QVERIFY2(again.load(file, err), qPrintable(err));
		QCOMPARE(again.regs.size(), 3);
		QCOMPARE(byName(again, QStringLiteral("TEMP"))->unit, QStringLiteral("K"));
		QCOMPARE(byName(again, QStringLiteral("SPEED"))->unit, QStringLiteral("rpm"));

		/* saved somewhere else: "extends" follows */
		QDir(tmp_.path()).mkpath(QStringLiteral("elsewhere/deeper"));
		const QByteArray moved = again.toJson(path(QStringLiteral("elsewhere/deeper/overlay.json")));
		QVERIFY2(moved.contains("\"extends\": \"../../base/base.json\""), moved.constData());

		/* flattened: a whole map without "extends" */
		const QByteArray flat = again.toJson(path(QStringLiteral("flat.json")), true);
		QVERIFY(!flat.contains("extends"));
		writeFile(path(QStringLiteral("flat.json")), flat);
		DeviceMap whole;
		QVERIFY2(whole.load(path(QStringLiteral("flat.json")), err), qPrintable(err));
		QVERIFY(!whole.isOverlay());
		QCOMPARE(whole.regs.size(), 3);
		QCOMPARE(whole.device, QStringLiteral("Overlay device"));
		QCOMPARE(byName(whole, QStringLiteral("TEMP"))->unit, QStringLiteral("K"));
	}

	/* an overlay that extends itself stops with an error, not a hang */
	void overlayLoop() {
		writeFile(path(QStringLiteral("loop.json")), R"({ "extends": "loop.json", "registers": [] })");
		DeviceMap map;
		QString err;
		QVERIFY(!map.load(path(QStringLiteral("loop.json")), err));
		QVERIFY2(err.contains(QLatin1String("8 maps deep")), qPrintable(err));
	}

	/* a new map: written the Studio's way, and read back the same */
	void newMap() {
		DeviceMap map;
		map.device = QStringLiteral("Fresh");
		map.deviceId = 0x1234;
		map.loginAddr = 0xF000;
		RegDef def;
		def.addr = 0x0010;
		def.name = QStringLiteral("SET");
		def.type = RegType::F32;
		def.rw = true;
		def.group = QStringLiteral("Settings");
		def.min = 0;
		def.max = 10;
		def.special = { { -1, QStringLiteral("unset") } };
		map.regs.push_back(def);
		QString err;
		QVERIFY2(map.save(path(QStringLiteral("fresh.json")), err), qPrintable(err));
		DeviceMap again;
		QVERIFY2(again.load(path(QStringLiteral("fresh.json")), err), qPrintable(err));
		QCOMPARE(again.deviceId, uint16_t(0x1234));
		QCOMPARE(again.loginAddr, uint16_t(0xF000));
		QCOMPARE(again.regs.size(), 1);
		QCOMPARE(again.regs[0].max, 10.0);
		QCOMPARE(again.regs[0].special.size(), 1);
		QCOMPARE(again.regs[0].special[0].name, QStringLiteral("unset"));
		/* and that file saves back byte for byte */
		QCOMPARE(again.toJson(path(QStringLiteral("fresh.json"))), readFile(path(QStringLiteral("fresh.json"))));
	}

	/* map settings changed: only their lines move */
	void settingsEdit() {
		DeviceMap map = loadText(QStringLiteral("pretty.json"), PRETTY_MAP);
		map.device = QStringLiteral("Renamed");
		map.usbVid = 0x1209;
		map.usbPid = 0x0001;
		const QByteArray after = map.toJson(path(QStringLiteral("pretty.json")));
		QStringList removed, added;
		lineDiff(PRETTY_MAP, after, removed, added);
		QCOMPARE(removed, QStringList{ QStringLiteral("  \"device\": \"Pretty device\",") });
		QVERIFY2(added.contains(QStringLiteral("  \"device\": \"Renamed\",")), qPrintable(added.join('\n')));
		QVERIFY2(added.contains(QStringLiteral("  \"usb\": {")), qPrintable(added.join('\n')));
		QVERIFY(after.contains("\"generated_from\""));
		writeFile(path(QStringLiteral("settings.json")), after);
		DeviceMap again;
		QString err;
		QVERIFY2(again.load(path(QStringLiteral("settings.json")), err), qPrintable(err));
		QCOMPARE(again.usbVid, uint16_t(0x1209));
	}

	/* the keys a specification needs: write behaviour, notes, persistence, protocol, group notes */
	void specKeys() {
		const char *text = R"JSON({ "format": "evre-map/1", "device": "Spec", "notes": "line 1\nline 2",
  "protocol": { "transport": "serial", "baud": 115200, "timeout_ms": 200 },
  "groups": { "Drive": { "notes": "moves things" } },
  "registers": [
  { "addr": "0x0010", "name": "GO", "type": "u8", "access": "rw", "write": "action", "group": "Drive" },
  { "addr": "0x0011", "name": "FAULTS", "type": "u8", "access": "rw", "write": "w1c", "group": "Drive",
    "fields": [ { "name": "OVER", "bits": "0", "access": "w1c" }, { "name": "CODE", "bits": "7:4", "access": "ro" } ] },
  { "addr": "0x0012", "name": "KEY", "type": "u32", "access": "wo", "group": "Drive" },
  { "addr": "0x0016", "name": "LIMIT", "type": "f32", "access": "rw", "persist": true, "group": "Drive",
    "notes": "saved in flash" }
] }
)JSON";
		DeviceMap map = loadText(QStringLiteral("spec.json"), text);
		QCOMPARE(map.notes, QStringLiteral("line 1\nline 2"));
		QCOMPARE(map.protocol.transport, QStringLiteral("serial"));
		QCOMPARE(map.protocol.baud, 115200);
		QCOMPARE(map.protocol.timeoutMs, 200);
		QCOMPARE(map.groupNotes.value(QStringLiteral("Drive")), QStringLiteral("moves things"));
		QCOMPARE(byName(map, QStringLiteral("GO"))->write, WriteKind::Action);
		const RegDef *faults = byName(map, QStringLiteral("FAULTS"));
		QCOMPARE(faults->write, WriteKind::WriteOneToClear);
		QCOMPARE(faults->fields[0].access, FieldAccess::WriteOneToClear);
		QCOMPARE(faults->fields[1].access, FieldAccess::ReadOnly);
		const RegDef *key = byName(map, QStringLiteral("KEY"));
		QVERIFY(key->rw && !key->readable && !isPollable(*key));
		QCOMPARE(accessText(*key), QStringLiteral("wo"));
		QVERIFY(byName(map, QStringLiteral("LIMIT"))->persist);
		QCOMPARE(byName(map, QStringLiteral("LIMIT"))->notes, QStringLiteral("saved in flash"));
		QCOMPARE(map.toJson(path(QStringLiteral("spec.json"))), QByteArray(text));
		/* changed and saved: read back the same */
		byName(map, QStringLiteral("GO"))->write = WriteKind::Normal;
		byName(map, QStringLiteral("KEY"))->readable = true;
		map.protocol.tcpPort = 1210;
		map.groupNotes.insert(QStringLiteral("Other"), QStringLiteral("x"));
		writeFile(path(QStringLiteral("spec2.json")), map.toJson(path(QStringLiteral("spec2.json"))));
		DeviceMap again;
		QString err;
		QVERIFY2(again.load(path(QStringLiteral("spec2.json")), err), qPrintable(err));
		QCOMPARE(byName(again, QStringLiteral("GO"))->write, WriteKind::Normal);
		QCOMPARE(accessText(*byName(again, QStringLiteral("KEY"))), QStringLiteral("rw"));
		QCOMPARE(again.protocol.tcpPort, 1210);
		QCOMPARE(again.protocol.baud, 115200);
		QCOMPARE(again.groupNotes.size(), 2);
		QCOMPARE(byName(again, QStringLiteral("FAULTS"))->fields[1].access, FieldAccess::ReadOnly);
	}

	/* a UTF-8 BOM and Windows line ends: kept, and an edited register gets them too */
	void bomAndCrlf() {
		QByteArray text = QByteArray("\xEF\xBB\xBF") + QByteArray(PRETTY_MAP).replace("\n", "\r\n");
		DeviceMap map = loadText(QStringLiteral("crlf.json"), text);
		QCOMPARE(map.regs.size(), 3);
		const QString file = path(QStringLiteral("crlf.json"));
		QCOMPARE(map.toJson(file), text);
		byName(map, QStringLiteral("VOLTAGE"))->desc = QStringLiteral("edited");
		RegDef added;
		added.addr = 0xD100;
		added.name = QStringLiteral("ADDED");
		added.group = QStringLiteral("Power");
		map.regs.push_back(added);
		const QByteArray after = map.toJson(file);
		QVERIFY(after.startsWith("\xEF\xBB\xBF{"));
		QVERIFY(!QByteArray(after).replace("\r\n", "").contains('\n')); /* every line end is CRLF */
		QVERIFY(after.contains("\"desc\": \"edited\""));
		QVERIFY(after.contains("\"name\": \"ADDED\""));
	}

	/* the example map written for others: every register in each, and the C and Python valid */
	void exports() {
		const QString file = QCoreApplication::applicationDirPath() + QStringLiteral("/maps/example_device.json");
		DeviceMap map;
		QString err;
		QVERIFY2(map.load(file, err), qPrintable(err));
		ExportOptions options;
		options.source = QStringLiteral("example_device.json");
		options.prefix = QStringLiteral("ex");

		const QString md = QString::fromUtf8(exportMarkdown(map, options));
		for (const RegDef &def : map.regs) QVERIFY2(md.contains(QStringLiteral("### %1 (`%2`)").arg(def.name, addrText(def.addr))),
				qPrintable(def.name));
		QVERIFY(md.contains(QLatin1String("| Byte order | little endian |")));
		QVERIFY(md.contains(QStringLiteral("| Range | 0 … 100 % |"))); /* FAN_SPEED */
		/* the bit diagrams are ASCII: + - | and spaces, digits and the names */
		const int fence = int(md.indexOf(QLatin1String("```\n")));
		QVERIFY(fence > 0);
		const QString diagram = md.mid(fence + 4, md.indexOf(QLatin1String("```"), fence + 4) - fence - 4);
		QVERIFY(diagram.contains(QLatin1String("+----+")));
		for (const QChar c : diagram) QVERIFY2(c.unicode() < 128, "a diagram of ASCII only");

		const QByteArray header = exportCHeader(map, options);
		QVERIFY(header.contains("#define EX_FAN_SPEED_ADDR 0xd084u"));
		QVERIFY(header.contains("#define EX_FAN_SPEED_MAX 100"));
		QVERIFY(header.contains("#define EX_SETPOINT_MAX 120.0f"));
		QVERIFY(header.contains("#define EX_LED_MODE_BLINK 2u"));
		QVERIFY(header.contains("#define EX_STATE_MODE_MSK 0x3u"));
		const QString gcc = QStandardPaths::findExecutable(QStringLiteral("gcc"));
		if (gcc.isEmpty()) {
			qWarning("gcc not on PATH: the header is not compiled");
		} else {
			writeFile(path(QStringLiteral("ex.h")), header);
			writeFile(path(QStringLiteral("ex.c")), "#include \"ex.h\"\nint main(void) { return (int)(EX_FAN_SPEED_ADDR"
					" + EX_STATE_MODE_MSK + EX_LED_MODE_BLINK) + (EX_SETPOINT_MAX > 0.0f); }\n");
			QProcess cc;
			cc.setWorkingDirectory(tmp_.path());
			cc.start(gcc, { QStringLiteral("-Wall"), QStringLiteral("-Wextra"), QStringLiteral("-Werror"), QStringLiteral("-fsyntax-only"),
					QStringLiteral("ex.c") });
			QVERIFY(cc.waitForFinished(30000));
			QVERIFY2(cc.exitCode() == 0, cc.readAllStandardError().constData());
		}

		const QByteArray module = exportPython(map, options);
		QVERIFY(module.contains("EX_FAN_SPEED = 0xd084"));
		const QString python = QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()
				? QStandardPaths::findExecutable(QStringLiteral("python")) : QStandardPaths::findExecutable(QStringLiteral("python3"));
		if (python.isEmpty()) {
			qWarning("python not on PATH: the module is not imported");
		} else {
			writeFile(path(QStringLiteral("ex_map.py")), module);
			QProcess run;
			run.setWorkingDirectory(tmp_.path());
			run.start(python, { QStringLiteral("-c"), QStringLiteral("import ex_map as m; r = m.REGISTERS['FAN_SPEED']; "
					"assert r['max'] == 100 and r['addr'] == 0xD084, r; assert m.REGISTERS['STATE']['fields'][0]['width'] == 2; "
					"print(len(m.REGISTERS))") });
			QVERIFY(run.waitForFinished(30000));
			QVERIFY2(run.exitCode() == 0, run.readAllStandardError().constData());
			QCOMPARE(run.readAllStandardOutput().trimmed(), QByteArray::number(map.regs.size()));
		}
	}

	/* CSV out and back in: the same registers (every key the CSV has a column for) */
	void csvRoundTrip() {
		DeviceMap map = loadText(QStringLiteral("csv.json"), CSV_MAP);
		QCOMPARE(map.regs.size(), 4);
		QVERIFY(!map.regs[0].plottable && map.regs[1].plottable); /* "plot": false read; plotted when not given */
		const QByteArray csv = exportCsv(map);
		QVector<RegDef> back;
		QString err;
		QVERIFY2(importCsv(csv, back, err), qPrintable(err));
		QCOMPARE(back.size(), map.regs.size());
		for (int i = 0; i < back.size(); i++) {
			const RegDef &a = map.regs[i], &b = back[i];
			QCOMPARE(b.addr, a.addr);
			QCOMPARE(b.name, a.name);
			QCOMPARE(b.type, a.type);
			QCOMPARE(b.size, a.size);
			QCOMPARE(b.unit, a.unit);
			QCOMPARE(b.rw, a.rw);
			QCOMPARE(b.readable, a.readable);
			QCOMPARE(b.write, a.write);
			QCOMPARE(b.persist, a.persist);
			QCOMPARE(b.plottable, a.plottable);
			QCOMPARE(b.danger, a.danger);
			QCOMPARE(b.hex, a.hex);
			QCOMPARE(b.group, a.group);
			QCOMPARE(b.desc, a.desc);
			QCOMPARE(b.notes, a.notes);
			QCOMPARE(b.scale, a.scale);
			QCOMPARE(b.decimals, a.decimals);
			QCOMPARE(b.hasMin(), a.hasMin());
			QCOMPARE(b.hasDefault(), a.hasDefault());
			QCOMPARE(b.enumValues, a.enumValues);
			QCOMPARE(b.enumHex, a.enumHex);
			QCOMPARE(b.special.size(), a.special.size());
			QCOMPARE(b.fields.size(), a.fields.size());
			for (int f = 0; f < b.fields.size(); f++) {
				QCOMPARE(b.fields[f].name, a.fields[f].name);
				QCOMPARE(b.fields[f].lsb, a.fields[f].lsb);
				QCOMPARE(b.fields[f].width, a.fields[f].width);
				QCOMPARE(b.fields[f].access, a.fields[f].access);
				QCOMPARE(b.fields[f].desc, a.fields[f].desc);
				QCOMPARE(b.fields[f].values, a.fields[f].values);
			}
		}
		/* a sheet with only some columns */
		QVERIFY2(importCsv("Name,Addr,Unit\nSPEED,0x20,rpm\nTEMP,34,C\n", back, err), qPrintable(err));
		QCOMPARE(back.size(), 2);
		QCOMPARE(back[1].addr, uint16_t(34));
		QCOMPARE(back[0].type, RegType::U16);
		QVERIFY(!importCsv("name,unit\nX,V\n", back, err));
		QVERIFY(!importCsv("addr,name\nnope,X\n", back, err) && err.contains(QLatin1String("line 2")));
	}

	void checks() {
		DeviceMap map;
		RegDef a;
		a.addr = 0x0010;
		a.name = QStringLiteral("A");
		a.type = RegType::U32;
		a.size = 4;
		RegDef b = a;
		b.addr = 0x0012; /* inside A */
		b.name = QStringLiteral("A");
		b.type = RegType::U16;
		b.size = 2;
		b.min = 5;
		b.max = 1;
		BitField wide;
		wide.name = QStringLiteral("WIDE");
		wide.lsb = 12;
		wide.width = 8; /* past bit 15 */
		b.fields.push_back(wide);
		RegDef f = a;
		f.addr = 0x0020;
		f.name = QStringLiteral("F");
		f.type = RegType::F32;
		BitField x;
		x.name = QStringLiteral("X");
		f.fields.push_back(x);
		map.regs = { a, b, f };
		const QVector<MapIssue> issues = checkMap(map);
		auto has = [&](int reg, bool error, const char *text) {
			for (const MapIssue &issue : issues)
				if (issue.reg == reg && issue.error == error && issue.text.contains(QLatin1String(text))) return true;
			return false;
		};
		QVERIFY(has(1, true, "is also"));
		QVERIFY(has(1, false, "overlaps A"));
		QVERIFY(has(1, true, "min is above max"));
		QVERIFY(has(1, true, "goes past bit 15"));
		QVERIFY(has(2, true, "need an integer type"));
		QVERIFY(!has(0, true, ""));
	}

	/* Fast EVRe's "streams": read, kept as written, written the Studio's way when changed or new */
	void streams() {
		DeviceMap map;
		QString err;
		const QString file = QCoreApplication::applicationDirPath() + QStringLiteral("/maps/example_fast.json");
		QVERIFY2(map.load(file, err), qPrintable(err));
		QCOMPARE(map.streams.size(), 1);
		const StreamDef &adc = map.streams[0];
		QCOMPARE(adc.name, QStringLiteral("ADC"));
		QCOMPARE(adc.addr, uint16_t(0xDC00));
		QCOMPARE(adc.size, 1024);
		QCOMPARE(adc.rate, 10000.0);
		QCOMPARE(adc.enable, QStringLiteral("ADC_STREAM"));
		QCOMPARE(adc.channels.size(), 2);
		QCOMPARE(adc.channels[0].name, QStringLiteral("I_LOAD"));
		QCOMPARE(adc.channels[0].type, RegType::I16);
		QCOMPARE(adc.channels[1].scale, 0.001);
		QCOMPARE(adc.recordSize(), 4);
		QCOMPARE(adc.recordsPerBlock(), 254);
		QVERIFY(checkMap(map).isEmpty());
		/* the rate changed: only the streams' text changes */
		const QByteArray before = readFile(file);
		DeviceMap edited = map;
		edited.streams[0].rate = 20000;
		QStringList removed, added;
		lineDiff(before, edited.toJson(file), removed, added);
		QVERIFY2(!removed.isEmpty() && removed.join(QString()).contains(QLatin1String("10000"))
				&& !removed.join(QString()).contains(QLatin1String("UPTIME")), qPrintable(removed.join(QLatin1Char('\n'))));
		DeviceMap back;
		writeFile(path(QStringLiteral("fast_edited.json")), edited.toJson(file));
		QVERIFY2(back.load(path(QStringLiteral("fast_edited.json")), err), qPrintable(err));
		QCOMPARE(back.streams[0].rate, 20000.0);
		QCOMPARE(back.streams[0].channels.size(), 2);
		/* a new map: the streams after the registers they name */
		DeviceMap fresh;
		fresh.device = QStringLiteral("New");
		fresh.regs = map.regs;
		fresh.streams = map.streams;
		const QByteArray text = fresh.toJson(path(QStringLiteral("fresh.json")));
		QVERIFY(text.indexOf("\"registers\"") < text.indexOf("\"streams\""));
		writeFile(path(QStringLiteral("fresh.json")), text);
		QVERIFY2(back.load(path(QStringLiteral("fresh.json")), err), qPrintable(err));
		QCOMPARE(back.streams.size(), 1);
		QCOMPARE(back.streams[0].channels[0].scale, 0.0005);
		/* what makes a stream unreadable stops the load */
		QVERIFY(!loadText(QStringLiteral("nokey.json"), "{ \"registers\": [], \"streams\": [ { \"name\": \"S\", "
				"\"addr\": \"0xD100\", \"size\": 64, \"channels\": [] } ] }").streams.size());
		DeviceMap broken;
		writeFile(path(QStringLiteral("badtype.json")), "{ \"registers\": [], \"streams\": [ { \"name\": \"S\", "
				"\"addr\": \"0xD100\", \"size\": 64, \"rate\": 1, \"channels\": [ { \"name\": \"C\", \"type\": \"i12\" } ] } ] }");
		QVERIFY(!broken.load(path(QStringLiteral("badtype.json")), err) && err.contains(QLatin1String("i12")));
	}

	/* every refusal of the checker for a stream */
	void streamChecks() {
		DeviceMap map;
		QString err;
		QVERIFY(map.load(QCoreApplication::applicationDirPath() + QStringLiteral("/maps/example_fast.json"), err));
		auto refused = [&](const std::function<void(DeviceMap &)> &change, const char *text) {
			DeviceMap m = map;
			change(m);
			for (const MapIssue &issue : checkMap(m))
				if (issue.error && issue.text.contains(QLatin1String(text))) return true;
			qWarning("not refused: %s", text);
			return false;
		};
		QVERIFY(refused([](DeviceMap &m) { m.streams[0].addr = 0xCF00; }, "not inside the device bank"));
		QVERIFY(refused([](DeviceMap &m) { m.streams[0].addr = 0xDF00; m.streams[0].size = 0x101; }, "not inside the device bank"));
		QVERIFY(refused([](DeviceMap &m) { m.streams[0].addr = 0xD000; }, "shares bytes with the register UPTIME"));
		QVERIFY(refused([](DeviceMap &m) { StreamDef s = m.streams[0]; s.name = QStringLiteral("B"); s.addr = 0xDE00;
				m.streams.push_back(s); }, "shares bytes with the stream ADC"));
		QVERIFY(refused([](DeviceMap &m) { m.streams[0].size = 11; }, "holds no block"));
		QVERIFY(refused([](DeviceMap &m) { m.streams[0].channels.clear(); }, "no channel"));
		QVERIFY(refused([](DeviceMap &m) { m.streams[0].channels[1].type = RegType::Bytes; }, "never bytes"));
		QVERIFY(refused([](DeviceMap &m) { m.streams[0].rate = 0; }, "rate above 0"));
		QVERIFY(refused([](DeviceMap &m) { m.streams[0].enable = QStringLiteral("NOPE"); }, "enable names no register"));
		QVERIFY(refused([](DeviceMap &m) { m.streams[0].enable = QStringLiteral("UPTIME"); }, "cannot write"));
		QVERIFY(refused([](DeviceMap &m) { m.streams[0].rateReg = QStringLiteral("NOPE"); }, "rate_reg names no register"));
		QVERIFY(refused([](DeviceMap &m) { m.streams[0].name = QStringLiteral("UPTIME"); }, "also a register's"));
		QVERIFY(refused([](DeviceMap &m) { m.streams[0].channels[1].name = QStringLiteral("I_LOAD"); }, "used twice"));
		QVERIFY(refused([](DeviceMap &m) { m.streams[0].name.clear(); }, "no name"));
		/* a stream that names a readable number as its rate is fine */
		DeviceMap rated = map;
		rated.streams[0].rateReg = QStringLiteral("UPTIME");
		QVERIFY(checkMap(rated).isEmpty());
	}

	/* a bus file: loaded, saved with the maps relative to it and the keys it does not know kept; its checks */
	void busFile() {
		writeFile(path(QStringLiteral("bus.json")),
				"{ \"format\": \"evre-bus/1\", \"name\": \"Bench\", \"owner\": \"lab 2\",\n"
				"  \"devices\": [ { \"name\": \"D1\", \"slave\": 1, \"map\": \"motor.json\", \"note\": \"left\" },\n"
				"               { \"name\": \"D2\", \"slave\": 2, \"map\": \"motor.json\", \"poll\": false } ],\n"
				"  \"broadcasts\": [ { \"name\": \"Stop all\", \"register\": \"SPEED\", \"value\": 0 } ] }\n");
		BusFile bus;
		QString err;
		QVERIFY2(bus.load(path(QStringLiteral("bus.json")), err), qPrintable(err));
		QCOMPARE(bus.name, QStringLiteral("Bench"));
		QCOMPARE(bus.devices.size(), 2);
		QCOMPARE(int(bus.devices[1].slave), 2);
		QVERIFY(bus.devices[0].poll && !bus.devices[1].poll);
		QCOMPARE(QFileInfo(bus.mapPath(0)).absoluteFilePath(), QFileInfo(path(QStringLiteral("motor.json"))).absoluteFilePath());
		QVERIFY(checkBus(bus).isEmpty());
		QVERIFY(bus.save(path(QStringLiteral("bus2.json")), err));
		BusFile again;
		QVERIFY(again.load(path(QStringLiteral("bus2.json")), err));
		QCOMPARE(again.devices[0].map, QStringLiteral("motor.json")); /* relative, beside it */
		QVERIFY(!again.devices[1].poll);
		QCOMPARE(again.presets.size(), 1);
		QCOMPARE(again.presets[0].name, QStringLiteral("Stop all"));
		QCOMPARE(again.presets[0].reg, QStringLiteral("SPEED"));
		QCOMPARE(again.presets[0].value, QStringLiteral("0"));
		const QByteArray saved = readFile(path(QStringLiteral("bus2.json")));
		QVERIFY(saved.contains("\"owner\": \"lab 2\"") && saved.contains("\"note\": \"left\""));

		BusFile wrong = bus;
		wrong.devices[1].slave = 1;               /* an address twice */
		QVERIFY(checkBus(wrong).join(' ').contains(QLatin1String("another device's too")));
		wrong = bus;
		wrong.devices[1].slave = 0;               /* the broadcast address */
		QVERIFY(checkBus(wrong).join(' ').contains(QLatin1String("broadcast")));
		wrong = bus;
		wrong.devices[1].name = QStringLiteral("D1_A"); /* D1_A_X: two readings */
		QVERIFY(checkBus(wrong).join(' ').contains(QLatin1String("both ways")));
		wrong = bus;
		wrong.devices[1].name = QStringLiteral("2nd"); /* not a name */
		QVERIFY(!checkBus(wrong).isEmpty());
		QCOMPARE(nextBusDevice(bus).name, QStringLiteral("D3"));
		BusFile full = bus;
		full.devices.clear();
		for (int slave = 1; slave <= 255; slave++) {
			BusDevice device;
			device.slave = uint8_t(slave);
			full.devices << device;
		}
		QCOMPARE(int(nextBusDevice(full).slave), 0); /* every address taken: none, never a duplicate */
		QVERIFY(nextBusDevice(full).name.isEmpty());
		QVERIFY(!bus.load(path(QStringLiteral("pretty.json")), err)); /* a map is not a bus */

		/* the example bus shipped beside the maps: two devices of the example map, two broadcasts */
		BusFile example;
		QVERIFY2(example.load(QCoreApplication::applicationDirPath() + QStringLiteral("/maps/example_bus.json"), err),
				qPrintable(err));
		QVERIFY2(checkBus(example).isEmpty(), qPrintable(checkBus(example).join(QStringLiteral("; "))));
		QCOMPARE(example.devices.size(), 2);
		QVERIFY(QFileInfo::exists(example.mapPath(0)) && QFileInfo::exists(example.mapPath(1)));
		QCOMPARE(example.presets.size(), 2);
		QCOMPARE(example.presets[1].reg, QStringLiteral("FAN_SPEED"));
		QCOMPARE(example.presets[1].value, QStringLiteral("50"));

		/* a broadcast's register: as named, then the map's own name on each device */
		QCOMPARE(broadcastNames({}, QStringLiteral("FAN_SPEED")), QStringList{ QStringLiteral("FAN_SPEED") });
		QCOMPARE(broadcastNames({ QStringLiteral("D1"), QStringLiteral("D2") }, QStringLiteral("FAN_SPEED")),
				(QStringList{ QStringLiteral("FAN_SPEED"), QStringLiteral("D1_FAN_SPEED"), QStringLiteral("D2_FAN_SPEED") }));
	}

	/* a broadcast: the reserved bank's writable registers always; elsewhere only on devices with one register map */
	void broadcastRule() {
		DeviceMap motor;
		RegDef speed;
		speed.addr = 0xD010;
		speed.name = QStringLiteral("SPEED");
		speed.rw = true;
		RegDef state = speed;
		state.addr = 0xD000;
		state.name = QStringLiteral("STATE");
		state.rw = false;
		motor.regs = { state, speed };
		DeviceMap twin = motor;
		twin.regs[0].uid = 7; /* the Studio's numbering is not the map */
		DeviceMap supply = motor;
		supply.regs[1].name = QStringLiteral("VOUT");
		QVERIFY(broadcastRefusal({ &motor, &twin }, 0xD010, 2).isEmpty());
		QVERIFY(!broadcastRefusal({ &motor, &twin }, 0xD000, 2).isEmpty());  /* read-only */
		QVERIFY(!broadcastRefusal({ &motor, &twin }, 0xD020, 2).isEmpty());  /* no register there */
		QVERIFY(broadcastRefusal({ &motor, &supply }, 0xD010, 2).contains(QLatin1String("different register maps")));
		QVERIFY(broadcastRefusal({ &motor, &supply }, 0xA004, 2).isEmpty()); /* CONFIG */
		QVERIFY(broadcastRefusal({ &motor, &supply }, 0xA006, 1).isEmpty()); /* MSG_CNT */
		QVERIFY(!broadcastRefusal({ &motor, &supply }, 0xA000, 2).isEmpty()); /* DEVICE_ID */
		QVERIFY(!broadcastRefusal({ &motor, &supply }, 0xA002, 2).isEmpty()); /* STATUS */
		QVERIFY(!broadcastRefusal({ &motor, &supply }, 0xA003, 3).isEmpty()); /* STATUS's high byte, then CONFIG */
		QVERIFY(!broadcastRefusal({ &motor }, 0xA000, 2).isEmpty());          /* ... with one device too */
		QVERIFY(broadcastRefusal({ &motor }, 0xD010, 2).isEmpty());           /* one device: its own writable register */
		QVERIFY(!broadcastRefusal({ &motor }, 0xD000, 2).isEmpty());          /* ... never a read-only one */
		QVERIFY(!broadcastRefusal({}, 0xD010, 2).isEmpty());                  /* no map: nothing to go by */
		QVERIFY(!broadcastRefusal({ &motor, &twin }, 0xD010, 0).isEmpty());   /* nothing to write */
		QVERIFY(!broadcastRefusal({ &motor, &supply }, 0xA100, 8).isEmpty()); /* past the reserved bank's end */
		/* with the bytes: never AUTO_SEND on (CONFIG bit 3) everywhere at once; CONFIG with it clear may go */
		QVERIFY(broadcastRefusal({ &motor, &supply }, 0xA004, QByteArray::fromHex("084F")).contains(QLatin1String("AUTO_SEND")));
		QVERIFY(!broadcastRefusal({ &motor, &supply }, 0xA004, QByteArray::fromHex("0C4F00")).isEmpty()); /* + MSG_CNT */
		QVERIFY(broadcastRefusal({ &motor, &supply }, 0xA004, QByteArray::fromHex("044F")).isEmpty());
		QVERIFY(broadcastRefusal({ &motor, &supply }, 0xA006, QByteArray::fromHex("08")).isEmpty()); /* MSG_CNT 8 */
	}
};

QTEST_GUILESS_MAIN(MapTest)
#include "map_test.moc"
