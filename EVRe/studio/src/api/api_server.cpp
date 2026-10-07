/* SPDX-License-Identifier: Apache-2.0 */
/* The API server: the EVRe pass-through, the JSON-lines commands (one handler
 * each) and the register reads they share (see api_server.h). */
#include "api/api_server.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <utility>

#include "evre/master.h"
#include "io/fast_stream.h"
#include "io/reg_table.h"
#include "model/bus_file.h"

namespace {

constexpr int MIN_STREAM_MS = 5;
constexpr int DEFAULT_STREAM_MS = 100;
constexpr int MIN_FAST_PERIOD_MS = 10;   /* a fast channel's period: its min, max and mean */
/* a pass-through client that does not read its blocks: past this many bytes waiting, the next blocks are not sent to
 * it (their numbers tell it what it missed), so it cannot fill the Studio's memory */
constexpr qint64 MAX_BLOCK_BACKLOG = 8 * 1024 * 1024;
constexpr int MAX_LINE = 64 * 1024;      /* a JSON line longer than this: the client is dropped */
constexpr uint8_t ERROR_PERMISSION_DENIED = 3; /* the EVRe error code of a refused write */

/* "0xD004" (hex) or "53252" (decimal) -> 0xD004; false past 0xFFFF */
bool addressFromText(const QString &text, uint16_t &addr) {
	bool ok = false;
	const uint number = parseAddress(text, &ok);
	if (!ok || number > 0xFFFF) return false;
	addr = uint16_t(number);
	return true;
}

/* an address in a request: a JSON number, or a text as above */
bool addressFromJson(const QJsonValue &value, uint16_t &addr) {
	if (!value.isDouble()) return addressFromText(value.toString(), addr);
	const int number = value.toInt(-1);
	if (number < 0 || number > 0xFFFF) return false;
	addr = uint16_t(number);
	return true;
}

/* A register's value in JSON: an integer, a number with decimals if the
 * register has them (f32, or a scale or offset), hex text for byte arrays,
 * null while there is no valid value. */
QJsonValue valueJson(const RegDef &def, const QByteArray &raw) {
	if (raw.size() < def.size) return QJsonValue::Null;
	if (def.type == RegType::Bytes) return QString::fromLatin1(raw.toHex());
	const double value = decodeNumber(def, raw);
	if (!std::isfinite(value)) return QJsonValue::Null;
	if (!def.isFloat()) return QJsonValue(qint64(value));
	return QJsonValue(value);
}

/* one entry of the "list" answer */
QJsonObject registerJson(const RegDef &def) {
	QJsonObject entry{
		{ QStringLiteral("name"), def.name },
		{ QStringLiteral("addr"), addrText(def.addr) },
		{ QStringLiteral("type"), typeName(def.type) },
		{ QStringLiteral("size"), def.size },
		{ QStringLiteral("access"), accessText(def) },
		{ QStringLiteral("group"), def.group },
	};
	if (!def.unit.isEmpty()) entry.insert(QStringLiteral("unit"), def.unit);
	if (def.danger) entry.insert(QStringLiteral("danger"), true);
	if (!def.desc.isEmpty()) entry.insert(QStringLiteral("desc"), def.desc);
	if (def.slave) entry.insert(QStringLiteral("slave"), int(def.slave)); /* a device of a bus */
	/* what a client needs to write it well: the limits, the default, the names of single values */
	if (def.hasMin()) entry.insert(QStringLiteral("min"), def.min);
	if (def.hasMax()) entry.insert(QStringLiteral("max"), def.max);
	if (def.hasDefault()) entry.insert(QStringLiteral("default"), def.defaultValue);
	if (def.persist) entry.insert(QStringLiteral("persist"), true);
	if (!def.plottable) entry.insert(QStringLiteral("plot"), false);
	if (def.write == WriteKind::Action) entry.insert(QStringLiteral("write"), QStringLiteral("action"));
	if (def.write == WriteKind::WriteOneToClear) entry.insert(QStringLiteral("write"), QStringLiteral("w1c"));
	if (!def.special.isEmpty()) {
		QJsonObject special;
		for (const SpecialValue &value : def.special)
			special.insert(QString::number(value.value, 'g', QLocale::FloatingPointShortest), value.name);
		entry.insert(QStringLiteral("special"), special);
	}
	return entry;
}

/* one entry of the "list" answer's "streams": the stream, its rate and state, its channels by the names the other
 * commands take (STREAM.CHANNEL) */
QJsonObject streamJson(const ApiServer::FastState &stream) {
	QJsonArray channels;
	for (const StreamChannel &channel : stream.def.channels) {
		QJsonObject entry{
			{ QStringLiteral("name"), stream.def.name + QLatin1Char('.') + channel.name },
			{ QStringLiteral("type"), typeName(channel.type) },
		};
		if (!channel.unit.isEmpty()) entry.insert(QStringLiteral("unit"), channel.unit);
		if (!channel.desc.isEmpty()) entry.insert(QStringLiteral("desc"), channel.desc);
		channels.append(entry);
	}
	QJsonObject entry{
		{ QStringLiteral("name"), stream.def.name },
		{ QStringLiteral("rate"), stream.rate },
		{ QStringLiteral("on"), stream.on },
		{ QStringLiteral("channels"), channels },
	};
	if (!stream.def.desc.isEmpty()) entry.insert(QStringLiteral("desc"), stream.def.desc);
	return entry;
}

/* where channel c lies in a record of the stream: the sizes of the channels before it */
int channelOffset(const StreamDef &def, int channel) {
	int offset = 0;
	for (int c = 0; c < channel && c < def.channels.size(); c++) offset += typeSize(def.channels[c].type);
	return offset;
}

/* A value of a set, as the text a user would type into the table (what
 * encodeValue takes): true / false as 1 / 0, numbers at full precision. */
QString typedText(const QJsonValue &value) {
	if (value.isBool()) return value.toBool() ? QStringLiteral("1") : QStringLiteral("0");
	if (value.isDouble()) return QString::number(value.toDouble(), 'g', 17);
	return value.toString();
}

/* One register to read, and one block read that covers several of one device. */
struct RegSpan {
	uint8_t slave;           /* the table's (RegDef::slave) */
	uint16_t addr;
	int size;
};
struct BlockRead {
	uint8_t slave;
	uint16_t addr;
	int end;                 /* one past the last byte */
	QVector<RegSpan> regs;
};

/* Registers by device and address, merged into as few reads as the poller
 * makes (model/device_map.h): a register joins the block before it when both
 * are of one device, in the same bank (0xD0xx) and at most MAX_BLOCK_GAP bytes
 * lie between them. */
QVector<BlockRead> mergeIntoBlocks(QVector<RegSpan> regs) {
	std::sort(regs.begin(), regs.end(),
			[](const RegSpan &a, const RegSpan &b) { return regKey(a.slave, a.addr) < regKey(b.slave, b.addr); });
	QVector<BlockRead> blocks;
	for (const RegSpan &reg : regs) {
		const bool joins = !blocks.isEmpty() && blocks.last().slave == reg.slave && sameBank(reg.addr, blocks.last().addr)
				&& int(reg.addr) - blocks.last().end <= MAX_BLOCK_GAP;
		if (joins) {
			blocks.last().end = std::max(blocks.last().end, int(reg.addr) + reg.size);
			blocks.last().regs << reg;
		} else {
			blocks.push_back({ reg.slave, reg.addr, int(reg.addr) + reg.size, { reg } });
		}
	}
	return blocks;
}

/* Where the answer to one pass-through request goes: back to the client (if it
 * is still connected) with the request's slave, offset and count, as the
 * device itself would answer. */
struct EvreAnswer {
	QPointer<QTcpSocket> socket;
	uint8_t slave;
	uint16_t addr, cnt;

	EvreAnswer(QTcpSocket *to, const evre::Frame &request)
		: socket(to), slave(request.slave), addr(request.addr), cnt(request.cnt) {}
	void send(uint8_t fn, const QByteArray &data = {}) const {
		if (socket) socket->write(evre::build(slave, fn, addr, cnt, data));
	}
	void sendError(uint8_t code) const { send(evre::ERROR_RESP, QByteArray(1, char(code))); }
};

} // namespace

ApiServer::ApiServer(evre::Master *master, RegTable *table, QObject *parent)
	: QObject(parent), master_(master), table_(table), evreServer_(new QTcpServer(this)),
	  jsonServer_(new QTcpServer(this)) {
	connect(evreServer_, &QTcpServer::newConnection, this, &ApiServer::acceptEvreClients);
	connect(jsonServer_, &QTcpServer::newConnection, this, &ApiServer::acceptJsonClients);
}

ApiServer::~ApiServer() { stop(); }

bool ApiServer::start(quint16 evrePort, quint16 jsonPort, bool network, QString &err) {
	stop();
	const QHostAddress where = network ? QHostAddress::Any : QHostAddress::LocalHost;
	if (!evreServer_->listen(where, evrePort)) {
		err = tr("EVRe port %1: %2").arg(evrePort).arg(evreServer_->errorString());
		return false;
	}
	if (!jsonServer_->listen(where, jsonPort)) {
		err = tr("JSON port %1: %2").arg(jsonPort).arg(jsonServer_->errorString());
		evreServer_->close();
		return false;
	}
	evrePort_ = evrePort;
	jsonPort_ = jsonPort;
	return true;
}

void ApiServer::stop() {
	evreServer_->close();
	jsonServer_->close();
	/* abort() emits disconnected at once, whose handler removes the client:
	 * iterate over copies */
	for (QTcpSocket *socket : evreClients_.keys()) socket->abort();
	evreClients_.clear();
	blockWatches_.clear();
	const auto jsonClients = std::exchange(jsonClients_, {});
	for (const ClientPtr &client : jsonClients) {
		stopStream(*client);
		if (client->socket) client->socket->abort();
	}
}

bool ApiServer::running() const { return evreServer_->isListening(); }

/* -------------------------------------------------------------- permissions */

namespace {

QString writesOff() { return ApiServer::tr("API writes are off in EVRe Studio (tick \"Allow API writes\")"); }

QString dangerRefusal(const RegDef &def) {
	return ApiServer::tr("%1 is a ⚠ register: tick \"including ⚠ registers\" in EVRe Studio").arg(def.name);
}

bool overlaps(const RegDef &def, uint16_t addr, int count) {
	return int(def.addr) < addr + count && int(def.addr) + def.size > addr;
}

} // namespace

QString ApiServer::writeRefusal(uint8_t tableSlave, uint16_t addr, int count) const {
	if (!allowWrites_) return writesOff();
	if (allowDanger_) return {};
	for (const RegValue &row : table_->rows())
		if (row.def.danger && row.def.slave == tableSlave && overlaps(row.def, addr, count)) return dangerRefusal(row.def);
	return {};
}

QString ApiServer::broadcastWriteRefusal(uint16_t addr, int count) const {
	if (!allowWrites_) return writesOff();
	if (allowDanger_) return {};
	for (const RegValue &row : table_->rows())
		if (row.def.danger && overlaps(row.def, addr, count)) return dangerRefusal(row.def);
	return {};
}

/* ---------------------------------------------------------------- registers */

int ApiServer::rowByName(const QString &name) const {
	const QString wanted = name.trimmed();
	uint16_t addr = 0;
	if (wanted.startsWith(QLatin1String("0x"), Qt::CaseInsensitive) && addressFromText(wanted, addr))
		return rowByKey(regKey(rawTableSlave(), addr));
	const auto &rows = table_->rows();
	for (int i = 0; i < rows.size(); i++)
		if (rows[i].def.name.compare(wanted, Qt::CaseInsensitive) == 0) return i;
	return -1;
}

int ApiServer::rowByKey(RegKey key) const {
	const auto &rows = table_->rows();
	for (int i = 0; i < rows.size(); i++)
		if (regKey(rows[i].def) == key) return i;
	return -1;
}

uint8_t ApiServer::target(uint8_t tableSlave) const { return requestSlave(tableSlave, master_->slave()); }

/* raw requests (no register named) go to the master's slave: the device selected in the Studio */
uint8_t ApiServer::rawTableSlave() const { return bus_ ? master_->slave() : 0; }

bool ApiServer::namedKeys(const QJsonObject &request, QVector<RegKey> &keys, QString &error, QVector<FastRef> *fast,
		const QVector<FastState> *streams) const {
	/* "names": [..], "name": "..", or both */
	QStringList names;
	for (const QJsonValue &name : request.value(QStringLiteral("names")).toArray()) names << name.toString();
	if (request.contains(QStringLiteral("name"))) names << request.value(QStringLiteral("name")).toString();
	for (const QString &name : names) {
		const int row = rowByName(name);
		if (row >= 0) {
			keys << regKey(table_->rows()[row].def);
			continue;
		}
		/* a fast channel by its line's name, STREAM.CHANNEL in any case */
		bool found = false;
		for (int s = 0; fast && streams && !found && s < streams->size(); s++) {
			const StreamDef &def = (*streams)[s].def;
			for (int c = 0; !found && c < def.channels.size(); c++) {
				if ((def.name + QLatin1Char('.') + def.channels[c].name).compare(name.trimmed(), Qt::CaseInsensitive) != 0)
					continue;
				*fast << FastRef{ s, c };
				found = true;
			}
		}
		if (found) continue;
		error = fast && name.contains(QLatin1Char('.'))
				? tr("no register or fast channel \"%1\" in the map (a fast channel is STREAM.CHANNEL, as \"list\" "
					 "names it)").arg(name)
				: tr("no register \"%1\" in the map").arg(name);
		return false;
	}
	return true;
}

QString ApiServer::fastRefusal(const FastState &stream, bool needRecord) const {
	if (!stream.on)
		return tr("fast stream %1 is off: start it in EVRe Studio (Fast streams), or start the Studio with --fast %1")
				.arg(stream.def.name);
	if (needRecord && !stream.hasRecord) return tr("fast stream %1 is on, but no record has come yet").arg(stream.def.name);
	return {};
}

/* {"NAME": value, ...}, names as in the map */
QJsonObject ApiServer::valuesJson(const QHash<RegKey, QByteArray> &raw) const {
	QJsonObject values;
	for (auto it = raw.cbegin(); it != raw.cend(); ++it) {
		const int row = rowByKey(it.key());
		if (row < 0) continue;
		const RegDef &def = table_->rows()[row].def;
		values.insert(def.name, valueJson(def, it.value()));
	}
	return values;
}

/* {"STATE": "MODE=run  READY", "LED_MODE": "blink", ...}: the registers with bit fields or an enum */
QJsonObject ApiServer::decodedJson(const QHash<RegKey, QByteArray> &raw) const {
	QJsonObject decoded;
	for (auto it = raw.cbegin(); it != raw.cend(); ++it) {
		const int row = rowByKey(it.key());
		if (row < 0) continue;
		const RegDef &def = table_->rows()[row].def;
		const QString text = formatDecoded(def, it.value());
		if (!text.isEmpty()) decoded.insert(def.name, text);
	}
	return decoded;
}

void ApiServer::readRegisters(const QVector<RegKey> &keys, ReadDone done) {
	QVector<RegSpan> regs;
	for (RegKey key : keys) {
		const int row = rowByKey(key);
		if (row < 0) continue;
		const RegDef &def = table_->rows()[row].def;
		regs.push_back({ def.slave, def.addr, def.size });
	}
	const QVector<BlockRead> blocks = mergeIntoBlocks(std::move(regs));
	if (blocks.isEmpty()) {
		done(ReadResult());
		return;
	}
	/* the blocks are answered one by one; the last answer reports them all */
	struct Progress {
		int blocksLeft;
		ReadResult result;
	};
	auto progress = std::make_shared<Progress>(Progress{ int(blocks.size()), ReadResult() });
	for (const BlockRead &block : blocks) {
		const uint16_t count = uint16_t(block.end - block.addr);
		master_->readFrom(target(block.slave), block.addr, count, [this, progress, block, done](const evre::Result &r) {
			ReadResult &result = progress->result;
			if (r.ok) {
				for (const RegSpan &reg : block.regs) {
					const QByteArray raw = r.data.mid(reg.addr - block.addr, reg.size);
					const RegKey key = regKey(reg.slave, reg.addr);
					result.raw.insert(key, raw);
					const int row = rowByKey(key);
					if (row >= 0) table_->setRaw(row, raw); /* the table sees it too */
				}
			} else if (result.ok) {
				result.ok = false;
				result.error = tr("%1: %2").arg(addrText(block.addr), r.message);
			}
			if (--progress->blocksLeft == 0) done(result);
		});
	}
}

/* -------------------------------------------------------- EVRe pass-through */

void ApiServer::acceptEvreClients() {
	while (QTcpSocket *socket = evreServer_->nextPendingConnection()) {
		socket->setSocketOption(QAbstractSocket::LowDelayOption, 1); /* answers leave at once */
		evreClients_.insert(socket, evre::Parser());
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readEvreFrames(socket); });
		connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
			evreClients_.remove(socket);
			blockWatches_.remove(socket);
			socket->deleteLater();
		});
	}
}

void ApiServer::readEvreFrames(QTcpSocket *socket) {
	auto parser = evreClients_.find(socket);
	if (parser == evreClients_.end()) return;
	parser->feed(socket->readAll());
	evre::Frame request;
	while (parser->next(request)) onEvreRequest(socket, request);
}

void ApiServer::onEvreRequest(QTcpSocket *socket, const evre::Frame &request) {
	requests_++;
	if (request.fn == evre::READ) passRead(socket, request);
	else if (request.fn == evre::WRITE || request.fn == evre::WRITE_ACK) passWrite(socket, request);
	/* anything else (an answer sent to us?) is not a request: ignored */
}

void ApiServer::passRead(QTcpSocket *socket, const evre::Frame &request) {
	if (!isConnected()) return; /* no device: no answer, as a silent device would be */
	if (bus_ && request.slave == evre::BROADCAST) return; /* nobody answers a broadcast READ */
	const EvreAnswer answer(socket, request);
	/* a bus: to the slave the frame names; one device: to the Studio's slave, whatever it names */
	const uint8_t to = bus_ ? request.slave : master_->slave();
	master_->readFrom(to, request.addr, request.cnt, [answer](const evre::Result &r) {
		if (r.ok) answer.send(evre::READ_RESP, r.data);
		else if (r.error) answer.sendError(r.error);
		/* timeout / link: nothing, the client times out as it would on the wire */
	});
}

void ApiServer::passWrite(QTcpSocket *socket, const evre::Frame &request) {
	const bool wantsAck = request.fn == evre::WRITE_ACK;
	const EvreAnswer answer(socket, request);
	if (bus_ && request.slave == evre::BROADCAST) {
		/* a broadcast WRITE, when the switches and the rule allow it; never answered, as on the wire. The switches
		 * first: they are cheap, and with writes off nothing goes, whatever the table holds */
		const bool allowed = !wantsAck && isConnected() && broadcastWriteRefusal(request.addr, request.cnt).isEmpty()
				&& broadcastRefusal(request.addr, request.data).isEmpty();
		if (allowed) master_->writeNoAckTo(evre::BROADCAST, request.addr, request.data);
		return;
	}
	/* exactly a fast stream's enable register (one device): the client asks for the stream's blocks */
	QString watched;
	bool watchOn = false;
	if (!bus_) {
		for (const FastState &stream : fastStreams()) {
			if (stream.enable.name.isEmpty() || stream.enable.addr != request.addr || stream.enable.size != request.cnt
					|| request.data.size() != request.cnt)
				continue;
			if (takeEnableWrite(socket, request, stream, wantsAck)) return;
			watched = stream.def.name;
			watchOn = decodeNumber(stream.enable, request.data) != 0;
			break;
		}
	}
	const uint8_t to = bus_ ? request.slave : master_->slave();
	if (!writeRefusal(bus_ ? to : 0, request.addr, request.cnt).isEmpty()) {
		if (wantsAck) answer.sendError(ERROR_PERMISSION_DENIED);
		return;
	}
	if (!isConnected()) return;
	/* the device always acknowledges (the queue waits for it); the client
	 * hears of it only if it asked */
	QPointer<QTcpSocket> client(socket);
	master_->writeTo(to, request.addr, request.data,
			[this, answer, wantsAck, client, watched, watchOn, slave = request.slave](const evre::Result &r) {
		/* the device took the client's own switch of a stream: its blocks follow it */
		if (r.ok && client && !watched.isEmpty()) watchBlocks(client, watched, slave, watchOn, true);
		if (!wantsAck) return;
		if (r.ok) answer.send(evre::WRITE_ACK_RESP);
		else if (r.error) answer.sendError(r.error);
	});
}

/* While the Studio streams it, a stream's enable is the Studio's: a client's 1 starts its blocks and its 0 ends them,
 * acknowledged here and never sent, so no client switches the Studio's stream off, and no write switch is needed (the
 * device is not written). While the Studio does not, the write is the client's own and goes to the device as any write;
 * only a 0 from a client whose blocks the Studio started is taken here (the device was never switched by it). */
bool ApiServer::takeEnableWrite(QTcpSocket *socket, const evre::Frame &request, const FastState &stream, bool wantsAck) {
	const bool on = decodeNumber(stream.enable, request.data) != 0;
	const auto watches = blockWatches_.constFind(socket);
	const bool own = watches != blockWatches_.cend() && watches->value(stream.def.name).own;
	const bool watching = watches != blockWatches_.cend() && watches->contains(stream.def.name);
	if (!stream.on && (on || !watching || own)) return false;
	watchBlocks(socket, stream.def.name, request.slave, on, false);
	if (wantsAck) EvreAnswer(socket, request).send(evre::WRITE_ACK_RESP);
	return true;
}

void ApiServer::watchBlocks(QTcpSocket *socket, const QString &stream, uint8_t slave, bool on, bool own) {
	if (!evreClients_.contains(socket)) return; /* it left meanwhile */
	if (!on) {
		auto watches = blockWatches_.find(socket);
		if (watches == blockWatches_.end()) return;
		watches->remove(stream);
		if (watches->isEmpty()) blockWatches_.erase(watches);
		return;
	}
	blockWatches_[socket].insert(stream, BlockWatch{ slave, own });
}

/* a frame at a stream's window, to each client that asked for the stream: as the device sent it, with the slave the
 * client named, as an answer would carry */
void ApiServer::passBlock(const QString &stream, const evre::Frame &frame) {
	for (auto it = blockWatches_.cbegin(); it != blockWatches_.cend(); ++it) {
		const auto watch = it->constFind(stream);
		if (watch == it->cend()) continue;
		QTcpSocket *socket = it.key();
		if (socket->bytesToWrite() > MAX_BLOCK_BACKLOG) continue;
		socket->write(evre::build(watch->slave, frame.fn, frame.addr, frame.cnt, frame.data));
	}
}

/* ----------------------------------------------------------------- JSON API */

void ApiServer::acceptJsonClients() {
	while (QTcpSocket *socket = jsonServer_->nextPendingConnection()) {
		socket->setSocketOption(QAbstractSocket::LowDelayOption, 1); /* answers leave at once */
		auto client = std::make_shared<JsonClient>();
		client->socket = socket;
		jsonClients_.insert(socket, client);
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readJsonLines(socket); });
		connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
			if (const ClientPtr leaving = jsonClients_.take(socket)) stopStream(*leaving);
			socket->deleteLater();
		});
	}
}

void ApiServer::readJsonLines(QTcpSocket *socket) {
	const ClientPtr client = jsonClients_.value(socket);
	if (!client) return;
	client->input.append(socket->readAll());
	qsizetype newline;
	while ((newline = client->input.indexOf('\n')) >= 0) {
		const QByteArray line = client->input.left(newline).trimmed();
		client->input.remove(0, newline + 1);
		if (!line.isEmpty()) onJsonLine(client, line);
		if (!jsonClients_.contains(socket)) return; /* it left while we answered */
	}
	if (client->input.size() > MAX_LINE) socket->abort();
}

void ApiServer::onJsonLine(const ClientPtr &client, const QByteArray &line) {
	requests_++;
	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
		fail(*client, tr("not a JSON object: %1").arg(parseError.errorString()), {});
		return;
	}
	const QJsonObject request = doc.object();
	const QJsonValue id = request.value(QStringLiteral("id"));
	const QString cmd = request.value(QStringLiteral("cmd")).toString().toLower();

	/* these three work without a device */
	if (cmd == QLatin1String("info")) cmdInfo(*client, id);
	else if (cmd == QLatin1String("list")) cmdList(*client, id);
	else if (cmd == QLatin1String("stop")) cmdStop(*client, id);
	else if (!isConnected()) fail(*client, tr("EVRe Studio is not connected to a device"), id);
	else if (cmd == QLatin1String("get")) cmdGet(client, request, id);
	else if (cmd == QLatin1String("stream")) cmdStream(client, request, id);
	else if (cmd == QLatin1String("set")) cmdSet(client, request, id);
	else if (cmd == QLatin1String("read")) cmdRead(client, request, id);
	else if (cmd == QLatin1String("write")) cmdWrite(client, request, id);
	else if (cmd == QLatin1String("broadcast")) cmdBroadcast(client, request, id);
	else fail(*client, tr("unknown cmd \"%1\": info, list, get, set, stream, stop, read, write, broadcast").arg(cmd), id);
}

void ApiServer::reply(JsonClient &client, QJsonObject answer, const QJsonValue &id) {
	if (!client.socket) return;
	if (!id.isUndefined() && !id.isNull()) answer.insert(QStringLiteral("id"), id);
	client.socket->write(QJsonDocument(answer).toJson(QJsonDocument::Compact) + '\n');
}

void ApiServer::replyOk(JsonClient &client, const QJsonValue &id, QJsonObject answer) {
	answer.insert(QStringLiteral("ok"), true);
	reply(client, answer, id);
}

void ApiServer::fail(JsonClient &client, const QString &why, const QJsonValue &id) {
	reply(client, { { QStringLiteral("ok"), false }, { QStringLiteral("error"), why } }, id);
}

ApiServer::ClientPtr ApiServer::stillThere(const WeakClient &client) {
	ClientPtr alive = client.lock();
	return alive && alive->socket ? alive : nullptr;
}

/* {"cmd":"info"}: the device, the link and the write switches */
void ApiServer::cmdInfo(JsonClient &client, const QJsonValue &id) {
	replyOk(client, id, {
		{ QStringLiteral("app"), QStringLiteral("EVRe Studio") },
		{ QStringLiteral("device"), deviceName() },
		{ QStringLiteral("connected"), isConnected() },
		{ QStringLiteral("link"), linkName() },
		{ QStringLiteral("registers"), int(table_->rows().size()) },
		{ QStringLiteral("writes"), allowWrites_ },
		{ QStringLiteral("danger_writes"), allowWrites_ && allowDanger_ },
		{ QStringLiteral("evre_port"), evrePort_ },
		{ QStringLiteral("json_port"), jsonPort_ },
	});
}

/* {"cmd":"list"}: every register of the map, and its fast streams with their channels */
void ApiServer::cmdList(JsonClient &client, const QJsonValue &id) {
	QJsonArray registers;
	for (const RegValue &row : table_->rows()) registers.append(registerJson(row.def));
	QJsonArray streams;
	for (const FastState &stream : fastStreams()) streams.append(streamJson(stream));
	replyOk(client, id, { { QStringLiteral("registers"), registers }, { QStringLiteral("streams"), streams } });
}

/* {"cmd":"stop"}: ends the stream, if there is one */
void ApiServer::cmdStop(JsonClient &client, const QJsonValue &id) {
	stopStream(client);
	replyOk(client, id);
}

/* {"cmd":"get","names":["SUPPLY_V","STATE"]}: fresh values, and the bit fields decoded. A fast channel
 * ("ADC.I_LOAD"): its newest record's value, and that record's time in "times". */
void ApiServer::cmdGet(const ClientPtr &client, const QJsonObject &request, const QJsonValue &id) {
	QVector<RegKey> keys;
	QVector<FastRef> fast;
	const QVector<FastState> streams = fastStreams();
	QString error;
	if (!namedKeys(request, keys, error, &fast, &streams)) {
		fail(*client, error, id);
		return;
	}
	if (keys.isEmpty() && fast.isEmpty()) {
		fail(*client, tr("\"names\" is empty"), id);
		return;
	}
	/* the fast channels now, from the records that came: nothing is read for them */
	QJsonObject fastValues, times;
	for (const FastRef &ref : fast) {
		const FastState &stream = streams[ref.stream];
		const QString refusal = fastRefusal(stream, true);
		if (!refusal.isEmpty()) {
			fail(*client, refusal, id);
			return;
		}
		const StreamDef &def = stream.def;
		const QString name = def.name + QLatin1Char('.') + def.channels[ref.channel].name;
		const int offset = channelOffset(def, ref.channel);
		if (stream.newest.size() < offset + typeSize(def.channels[ref.channel].type)) continue;
		const double value = fast::channelValue(def.channels[ref.channel], stream.newest.constData() + offset);
		fastValues.insert(name, std::isfinite(value) ? QJsonValue(value) : QJsonValue(QJsonValue::Null));
		times.insert(name, stream.newestTime);
	}
	readRegisters(keys, [this, weak = WeakClient(client), id, fastValues, times](const ReadResult &read) {
		const ClientPtr asker = stillThere(weak);
		if (!asker) return;
		if (!read.ok) {
			fail(*asker, read.error, id);
			return;
		}
		QJsonObject values = valuesJson(read.raw);
		for (auto it = fastValues.begin(); it != fastValues.end(); ++it) values.insert(it.key(), it.value());
		QJsonObject answer{ { QStringLiteral("values"), values } };
		const QJsonObject decoded = decodedJson(read.raw);
		if (!decoded.isEmpty()) answer.insert(QStringLiteral("decoded"), decoded);
		if (!times.isEmpty()) answer.insert(QStringLiteral("times"), times);
		replyOk(*asker, id, answer);
	});
}

/* {"cmd":"stream","names":["SUPPLY_V","SUPPLY_I"],"ms":50}: a sample line every 50 ms. Fast channels
 * ("ADC.I_LOAD"): a line of each one's min, max and mean every "period_ms" (else "ms", else 100), beside. */
void ApiServer::cmdStream(const ClientPtr &client, const QJsonObject &request, const QJsonValue &id) {
	QVector<RegKey> keys;
	QVector<FastRef> fast;
	const QVector<FastState> streams = fastStreams();
	QString error;
	if (!namedKeys(request, keys, error, &fast, &streams)) {
		fail(*client, error, id);
		return;
	}
	if (keys.isEmpty() && fast.isEmpty()) { /* a stream of nothing: the same as stop */
		cmdStop(*client, id);
		return;
	}
	for (const FastRef &ref : fast) {
		const QString refusal = fastRefusal(streams[ref.stream], false);
		if (!refusal.isEmpty()) {
			fail(*client, refusal, id);
			return;
		}
	}
	const int ms = request.value(QStringLiteral("ms")).toInt(DEFAULT_STREAM_MS);
	startStream(client, keys, ms, fast, request.value(QStringLiteral("period_ms")).toInt(ms), id);
}

/* {"cmd":"set","values":{"LED_MODE":2}}: writes, then the values read back */
void ApiServer::cmdSet(const ClientPtr &client, const QJsonObject &request, const QJsonValue &id) {
	const QJsonObject values = request.value(QStringLiteral("values")).toObject();
	if (values.isEmpty()) {
		fail(*client, tr("\"values\" is empty: {\"cmd\":\"set\",\"values\":{\"NAME\":value}}"), id);
		return;
	}
	/* every value checked and encoded before the first write: all or nothing */
	struct Write {
		RegKey key = 0;
		QByteArray bytes;
	};
	QVector<Write> writes;
	for (auto it = values.begin(); it != values.end(); ++it) {
		Write write;
		const QString refusal = encodeForSet(it.key(), it.value(), write.key, write.bytes);
		if (!refusal.isEmpty()) {
			fail(*client, refusal, id);
			return;
		}
		writes.push_back(write);
	}
	/* written in order; the read-back is queued after the writes, so it runs
	 * once they are done */
	auto writeError = std::make_shared<QString>();
	QVector<RegKey> keys;
	for (const Write &write : writes) {
		keys << write.key;
		const uint8_t slave = regKeySlave(write.key);
		const uint16_t addr = regKeyAddr(write.key);
		master_->writeTo(target(slave), addr, write.bytes, [writeError, addr](const evre::Result &r) {
			if (!r.ok && writeError->isEmpty())
				*writeError = QStringLiteral("0x%1: %2").arg(addr, 4, 16, QLatin1Char('0')).arg(r.message);
		});
	}
	readRegisters(keys, [this, weak = WeakClient(client), writeError, id](const ReadResult &read) {
		const ClientPtr asker = stillThere(weak);
		if (!asker) return;
		if (!writeError->isEmpty()) {
			fail(*asker, *writeError, id);
		} else if (!read.ok) {
			fail(*asker, tr("written, but the read-back failed: %1").arg(read.error), id);
		} else {
			/* what the device holds now: it may have clamped the value */
			replyOk(*asker, id, { { QStringLiteral("values"), valuesJson(read.raw) } });
		}
	});
}

QString ApiServer::encodeForSet(const QString &name, const QJsonValue &value, RegKey &key,
		QByteArray &bytes) const {
	const int row = rowByName(name);
	if (row < 0) return tr("no register \"%1\" in the map").arg(name);
	const RegDef &def = table_->rows()[row].def;
	if (!def.rw) return tr("%1 is read-only").arg(def.name);
	const QString refusal = writeRefusal(def.slave, def.addr, def.size);
	if (!refusal.isEmpty()) return refusal;
	QString err;
	if (!encodeValue(def, typedText(value), bytes, err)) return tr("%1: %2").arg(def.name, err);
	/* the map's limits hold for the API: there is no one to ask "write anyway?" */
	const QString outside = def.isNumeric() ? limitProblem(def, decodeNumber(def, bytes)) : QString();
	if (!outside.isEmpty()) return tr("%1: %2 is %3 (the map's limit)").arg(def.name, typedText(value), outside);
	key = regKey(def);
	return {};
}

/* {"cmd":"read","addr":"0xD000","count":16}: raw bytes, as hex */
void ApiServer::cmdRead(const ClientPtr &client, const QJsonObject &request, const QJsonValue &id) {
	uint16_t addr = 0;
	const int count = request.value(QStringLiteral("count")).toInt(0);
	if (!addressFromJson(request.value(QStringLiteral("addr")), addr) || count < 1 || count > 0xFFFF) {
		fail(*client, tr("read needs \"addr\" and \"count\""), id);
		return;
	}
	/* a raw read goes to the master's slave: the device selected in the Studio */
	master_->read(addr, uint16_t(count), [this, weak = WeakClient(client), id](const evre::Result &r) {
		const ClientPtr asker = stillThere(weak);
		if (!asker) return;
		if (r.ok) replyOk(*asker, id, { { QStringLiteral("hex"), QString::fromLatin1(r.data.toHex()) } });
		else fail(*asker, r.message, id);
	});
}

/* {"cmd":"write","addr":"0xD085","hex":"02"}: raw bytes written */
void ApiServer::cmdWrite(const ClientPtr &client, const QJsonObject &request, const QJsonValue &id) {
	uint16_t addr = 0;
	const QByteArray data = QByteArray::fromHex(request.value(QStringLiteral("hex")).toString().toLatin1());
	if (!addressFromJson(request.value(QStringLiteral("addr")), addr) || data.isEmpty()) {
		fail(*client, tr("write needs \"addr\" and \"hex\""), id);
		return;
	}
	const QString refusal = writeRefusal(rawTableSlave(), addr, int(data.size()));
	if (!refusal.isEmpty()) {
		fail(*client, refusal, id);
		return;
	}
	/* a raw write goes to the master's slave: the device selected in the Studio */
	master_->write(addr, data, [this, weak = WeakClient(client), id](const evre::Result &r) {
		const ClientPtr asker = stillThere(weak);
		if (!asker) return;
		if (r.ok) replyOk(*asker, id);
		else fail(*asker, r.message, id);
	});
}

/* {"cmd":"broadcast","name":"SPEED","value":0}: the value to every device at once (slave 0, a WRITE nobody
 * answers), where the broadcast rule allows it, then that register read back from each device that has it. The
 * register by its map's own name (SPEED) or by a device's (D1_SPEED), as model/bus_file.h (broadcastNames) says.
 * With one device it goes to slave 0 too: the device takes it as any EVRe device does. */
void ApiServer::cmdBroadcast(const ClientPtr &client, const QJsonObject &request, const QJsonValue &id) {
	const QString asked = request.value(QStringLiteral("name")).toString();
	QString name = asked;
	for (const QString &candidate : broadcastNames(bus_ ? deviceNames_ : QStringList(), asked)) {
		const int row = rowByName(candidate);
		if (row < 0) continue;
		name = table_->rows()[row].def.name;
		break;
	}
	RegKey key = 0;
	QByteArray bytes;
	QString refusal = encodeForSet(name, request.value(QStringLiteral("value")), key, bytes);
	const uint16_t addr = regKeyAddr(key);
	/* every device takes it: the write switches over every device's registers there, then the broadcast rule */
	if (refusal.isEmpty()) refusal = broadcastWriteRefusal(addr, int(bytes.size()));
	if (refusal.isEmpty()) refusal = broadcastRefusal(addr, bytes);
	if (!refusal.isEmpty()) {
		fail(*client, refusal, id);
		return;
	}
	QVector<RegKey> readBack;
	for (const RegValue &row : table_->rows())
		if (row.def.addr == addr && row.def.readable) readBack << regKey(row.def);
	master_->writeNoAckTo(evre::BROADCAST, addr, bytes);
	readRegisters(readBack, [this, weak = WeakClient(client), id](const ReadResult &read) {
		const ClientPtr asker = stillThere(weak);
		if (!asker) return;
		if (!read.ok) fail(*asker, tr("sent, but the read-back failed: %1").arg(read.error), id);
		else replyOk(*asker, id, { { QStringLiteral("values"), valuesJson(read.raw) } }); /* each device's now */
	});
}

/* ------------------------------------------------------------------ streams */

void ApiServer::startStream(const ClientPtr &client, const QVector<RegKey> &keys, int ms, const QVector<FastRef> &fast,
		int periodMs, const QJsonValue &id) {
	stopStream(*client);
	client->streamKeys = keys;
	client->streamTag = id.isString() ? id.toString() : id.isDouble() ? QString::number(id.toDouble()) : QString();
	QJsonObject answer{ { QStringLiteral("streaming"), int(keys.size() + fast.size()) } };
	/* made here, in the I/O thread, and children of the server */
	if (!keys.isEmpty()) {
		client->streamTimer = new QTimer(this);
		client->streamTimer->setInterval(std::max(ms, MIN_STREAM_MS));
		client->streamTimer->setTimerType(Qt::PreciseTimer); /* coarse timers drift ~20 % on Windows */
		connect(client->streamTimer, &QTimer::timeout, this, [this, weak = WeakClient(client)] { streamTick(weak); });
		client->streamTimer->start();
		answer.insert(QStringLiteral("ms"), client->streamTimer->interval());
	}
	if (!fast.isEmpty()) {
		const QVector<FastState> streams = fastStreams();
		for (const FastRef &ref : fast) {
			JsonClient::FastWatch watch;
			watch.stream = streams[ref.stream].def.name;
			watch.channel = ref.channel;
			watch.name = watch.stream + QLatin1Char('.') + streams[ref.stream].def.channels[ref.channel].name;
			client->fastWatches << watch;
		}
		client->fastTimer = new QTimer(this);
		client->fastTimer->setInterval(std::max(periodMs, MIN_FAST_PERIOD_MS));
		client->fastTimer->setTimerType(Qt::PreciseTimer);
		connect(client->fastTimer, &QTimer::timeout, this, [this, weak = WeakClient(client)] { fastTick(weak); });
		client->fastTimer->start();
		answer.insert(QStringLiteral("fast"), int(fast.size()));
		answer.insert(QStringLiteral("period_ms"), client->fastTimer->interval());
	}
	replyOk(*client, id, answer);
}

/* A good block's records, into the period of every JSON stream that watches one of the stream's channels. The period
 * holds the records that came in it: a block comes a little after its records were taken. */
void ApiServer::fastRecords(const StreamDef &def, quint64 first, int count, const char *records) {
	const int size = def.recordSize();
	if (count <= 0 || size <= 0) return;
	for (const ClientPtr &client : std::as_const(jsonClients_)) {
		for (JsonClient::FastWatch &watch : client->fastWatches) {
			/* the map loaded again meanwhile: a channel of another layout is not read */
			if (watch.stream != def.name || watch.channel >= def.channels.size()
					|| watch.name != def.name + QLatin1Char('.') + def.channels[watch.channel].name)
				continue;
			const StreamChannel &channel = def.channels[watch.channel];
			const char *at = records + channelOffset(def, watch.channel);
			for (int k = 0; k < count; k++, at += size) {
				const double value = fast::channelValue(channel, at);
				if (!std::isfinite(value)) continue;
				if (watch.count == 0) {
					watch.first = first + quint64(k);
					watch.min = watch.max = value;
				}
				watch.min = std::min(watch.min, value);
				watch.max = std::max(watch.max, value);
				watch.sum += value;
				watch.count++;
			}
		}
	}
}

/* {"t": seconds since 1970, "fast": {"ADC.I_LOAD": {"n":..,"min":..,"max":..,"mean":..,"first":..}}, "stream": the
 * tag}: each channel's records of the period that ended now; a channel without one has "n": 0 and nulls */
void ApiServer::fastTick(const WeakClient &weak) {
	const ClientPtr client = stillThere(weak);
	if (!client) return;
	QJsonObject channels;
	for (JsonClient::FastWatch &watch : client->fastWatches) {
		QJsonObject numbers{ { QStringLiteral("n"), qint64(watch.count) } };
		const bool any = watch.count > 0;
		numbers.insert(QStringLiteral("min"), any ? QJsonValue(watch.min) : QJsonValue(QJsonValue::Null));
		numbers.insert(QStringLiteral("max"), any ? QJsonValue(watch.max) : QJsonValue(QJsonValue::Null));
		numbers.insert(QStringLiteral("mean"), any ? QJsonValue(watch.sum / double(watch.count)) : QJsonValue(QJsonValue::Null));
		if (any) numbers.insert(QStringLiteral("first"), qint64(watch.first));
		channels.insert(watch.name, numbers);
		watch.count = 0;
		watch.sum = 0;
	}
	if (!isConnected()) return; /* as the samples: none while the Studio has no link */
	QJsonObject line{
		{ QStringLiteral("t"), double(QDateTime::currentMSecsSinceEpoch()) / 1000.0 },
		{ QStringLiteral("fast"), channels },
	};
	if (!client->streamTag.isEmpty()) line.insert(QStringLiteral("stream"), client->streamTag);
	reply(*client, line, {});
}

void ApiServer::streamTick(const WeakClient &weak) {
	const ClientPtr client = stillThere(weak);
	/* one read at a time: a slow device lowers the rate */
	if (!client || client->streamBusy || !isConnected()) return;
	client->streamBusy = true;
	readRegisters(client->streamKeys, [this, weak](const ReadResult &read) {
		const ClientPtr asker = stillThere(weak);
		if (!asker) return;
		asker->streamBusy = false;
		sendSample(*asker, read);
	});
}

/* {"t": seconds since 1970, "values": {..}, "stream": the tag}, or "ok": false and the error */
void ApiServer::sendSample(JsonClient &client, const ReadResult &read) {
	QJsonObject sample{ { QStringLiteral("t"), double(QDateTime::currentMSecsSinceEpoch()) / 1000.0 } };
	if (read.ok) {
		sample.insert(QStringLiteral("values"), valuesJson(read.raw));
	} else {
		sample.insert(QStringLiteral("ok"), false);
		sample.insert(QStringLiteral("error"), read.error);
	}
	if (!client.streamTag.isEmpty()) sample.insert(QStringLiteral("stream"), client.streamTag);
	reply(client, sample, {});
}

void ApiServer::stopStream(JsonClient &client) {
	for (QTimer **timer : { &client.streamTimer, &client.fastTimer }) {
		if (!*timer) continue;
		(*timer)->stop();
		(*timer)->deleteLater();
		*timer = nullptr;
	}
	client.streamKeys.clear();
	client.fastWatches.clear();
	client.streamBusy = false;
}
