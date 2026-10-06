/* SPDX-License-Identifier: Apache-2.0 */
/* A map written for the people and the programs that implement or use the
 * device, and read back from a sheet:
 *
 *   Markdown   the specification: the device, how it is reached, a summary of
 *              the registers, then each group and each register in full (value
 *              names, special values, limits, bit fields with an ASCII diagram,
 *              notes). Enough to implement the device or a host from it.
 *   C header   #defines for firmware: address, size, enum values, each field's
 *              position and mask and its values, min, max, default
 *   Python     a module for host scripts: the same constants, and REGISTERS, a
 *              dict of every register's definition
 *   CSV        one row per register, every key in a column (value names, special
 *              values and fields in a compact text form); read back by importCsv
 *   device     the device side for the EVRe device library (lib/EVRe.h): the bank
 *   table      0xD000.. as packed read-only and read-write images in address
 *              order, their offsets checked, the defaults as start values, a
 *              bind function that serves them, and a check of the limits
 *   guard      EVRe Guard's table (lib/guard/evre_guard_desc.h): an entry for
 *   table      each register a host writes, its raw limits and listed values,
 *              in a .h (the table's name, the raw limits as typed constants)
 *              and a .cpp (the value list, the entries, the table)
 *
 * No window here: the Studio and command-line tools share these. */
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>

#include "model/device_map.h"

struct ExportOptions {
	QString source;  /* where the map came from, named in the output ("robot.json") */
	QString prefix;  /* C and Python names: PREFIX_REG_ADDR; empty: none */
};

QByteArray exportMarkdown(const DeviceMap &map, const ExportOptions &options = {});
QByteArray exportCHeader(const DeviceMap &map, const ExportOptions &options = {});
QByteArray exportPython(const DeviceMap &map, const ExportOptions &options = {});
QByteArray exportCsv(const DeviceMap &map);
/* The device table (C++). The library's permission is one boundary, so every
 * read-only register must come before every writable one, all inside
 * 0xD000..0xDFFF (the protocol bank 0xA000 is the library's own and left out):
 * false, with what is in the way in problems, if the map is not like that. */
bool exportDeviceTable(const DeviceMap &map, const ExportOptions &options, QByteArray &out, QStringList &problems);

/* What EVRe Guard's table holds for one register (exportGuard), worked out from the map: the type's raw limits
 * as the entry's bits, its listed values (the specials), and the map's own limits in raw units for the typed
 * constants. errors: why the register cannot have an entry (the export stops); warnings: what the entry takes
 * in place of what the map says (the map check shows both). */
struct GuardEntry {
	uint32_t min = 0, max = 0;            /* the entry's limits: an unsigned value, a signed one sign-extended, f32 bits */
	bool closed = false;                  /* only the listed values pass ("closed") */
	uint32_t zeroBits = 0;                /* bits a write must leave 0 ("reserved_zero") */
	QVector<QPair<uint32_t, QString>> values; /* listed, ascending as uint32_t, each with what the map calls it */
	bool hasRawMin = false, hasRawMax = false;
	double rawMin = 0, rawMax = 0;        /* the map's limits in raw units, as the entry rounds them */
	QStringList errors, warnings;
};
GuardEntry guardEntry(const RegDef &def);

/* EVRe Guard's table: header (for headerName, the name the source includes) and source. The registers a host
 * writes in 0xD000..0xDFFF, in address order; the reserved bank and the map's login register get no entry.
 * false, with the reasons in problems, when a register cannot have an entry. */
bool exportGuard(const DeviceMap &map, const ExportOptions &options, const QString &headerName, QByteArray &header,
		QByteArray &source, QStringList &problems);

/* The registers of a CSV (as exportCsv writes it, or a sheet with some of its
 * columns: "addr" and "name" are needed, the others have defaults). false with
 * err ("line 4: bad address") if it cannot be read. */
bool importCsv(const QByteArray &text, QVector<RegDef> &regs, QString &err);

/* a name as a C or Python identifier: "Power & supply" -> POWER_SUPPLY, "2nd" -> _2ND */
QString identifier(const QString &name);
