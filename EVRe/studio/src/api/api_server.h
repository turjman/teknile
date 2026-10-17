/* SPDX-License-Identifier: Apache-2.0 */
/* The Studio as a gateway: other programs (MATLAB, LabVIEW, Python, ...) reach
 * the device the Studio is connected to, through the Studio's one queue - so
 * they and the Studio never collide on the port.
 *
 *   port 1219  EVRe pass-through: the same frames as the device or any EVRe
 *              gateway (7B SLAVE FN OFF CNT DATA CRC 7D). Existing EVRe clients work.
 *   port 1220  JSON lines, by register name (examples/README.md):
 *                {"cmd":"get","names":["SUPPLY_V","STATE"]}
 *                -> {"ok":true,"values":{"SUPPLY_V":12.1,...},"decoded":{...}}
 *
 * Fast streams (Fast EVRe, one device): "list" names the map's streams and their channels (STREAM.CHANNEL), "get"
 * gives a channel's newest record and its time, "stream" a line of each channel's min, max and mean every period
 * (the records that came in it). On port 1219 a client that writes a stream's enable register gets the stream's
 * blocks as the device sent them: while the Studio streams it, the enable is the Studio's (1 starts the client's
 * blocks, 0 ends them, nothing reaches the device); while it does not, the write goes to the device as any write,
 * and its acknowledgement starts or ends them. Reading only: none of it needs a write switch.
 *
 * Writes from clients: refused until "Allow API writes" is on; registers
 * marked danger need "including ⚠" as well. Neither is remembered across
 * starts. By default only this PC may connect (127.0.0.1).
 *
 * Several devices on the link (a bus, model/bus_file.h): the registers are
 * named after their devices (D1_SUPPLY_V), so a JSON request reaches any
 * device's register by name; raw reads and writes by address go to the
 * device selected in the Studio. On the pass-through port a frame goes to
 * the slave it names, and a broadcast WRITE (slave 0) goes out as one when
 * the broadcast rule allows it (broadcastRefusal), never answered. With one
 * device every frame goes to the Studio's slave, whatever slave it names.
 *
 * The server lives in the engine's I/O thread and is a child of the engine, so
 * moveToThread takes it along. Every other QObject it uses is either its
 * child, moved along with it (the two TCP servers), or made in that thread
 * (the stream timers, children of the server; the client sockets, children of
 * the TCP servers).
 */
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>

#include "evre/frame.h"
#include "model/device_map.h"

class QTcpServer;
class QTcpSocket;
class QTimer;
class RegTable;
namespace evre {
class Master;
}

class ApiServer : public QObject {
	Q_OBJECT
public:
	ApiServer(evre::Master *master, RegTable *table, QObject *parent = nullptr);
	~ApiServer() override;

	/* what the engine knows and the server reports ("info") */
	std::function<bool()> isConnected = [] { return false; };
	std::function<QString()> linkName = [] { return QString(); };
	std::function<QString()> deviceName = [] { return QString(); };
	/* why a broadcast WRITE of these bytes at addr must not go out (model/bus_file.h); empty: it may */
	std::function<QString(uint16_t addr, const QByteArray &bytes)> broadcastRefusal =
			[](uint16_t, const QByteArray &) { return QString(); };
	/* Fast EVRe: each stream of the map as the engine has it now (none on a bus) */
	struct FastState {
		StreamDef def;
		RegDef enable;              /* its enable register; no name: none */
		bool on = false;            /* the Studio takes its blocks */
		double rate = 0;            /* records a second: as the device was set to once a block came, else the map's */
		bool hasRecord = false;     /* a record came since it was switched on */
		QByteArray newest;          /* that record, as it came */
		double newestTime = 0;      /* its time on the stream's clock, in seconds since 1970 */
	};
	std::function<QVector<FastState>()> fastStreams = [] { return QVector<FastState>(); };

	bool start(quint16 evrePort, quint16 jsonPort, bool network, QString &err);
	void stop();
	bool running() const;
	quint16 evrePort() const { return evrePort_; }
	quint16 jsonPort() const { return jsonPort_; }

	void setAllowWrites(bool on) { allowWrites_ = on; }
	/* several devices on the link: frames go to the slave they name; deviceNames: the devices' names (D1, D2), so a
	 * broadcast may name a register by its map's own name too (model/bus_file.h, broadcastNames) */
	void setBus(bool on, const QStringList &deviceNames = {}) {
		bus_ = on;
		deviceNames_ = deviceNames;
	}
	void setAllowDanger(bool on) { allowDanger_ = on; }
	int clientCount() const { return int(evreClients_.size() + jsonClients_.size()); }
	quint64 requests() const { return requests_; }

	/* Fast EVRe, from the engine: a frame at a stream's window as it came (to the pass-through clients that asked for
	 * the stream), and the records of a good block of a stream the Studio takes (to the JSON streams' periods) */
	void passBlock(const QString &stream, const evre::Frame &frame);
	void fastRecords(const StreamDef &def, quint64 first, int count, const char *records);

private:
	/* One client of the JSON port. Held by a shared_ptr: an answer that comes
	 * after the client has left finds it gone (weak_ptr). */
	struct JsonClient {
		QPointer<QTcpSocket> socket;
		QByteArray input;               /* received, not yet a whole line */
		QTimer *streamTimer = nullptr;  /* null: no stream */
		QVector<RegKey> streamKeys;
		bool streamBusy = false;        /* a sample is being read: the next tick skips */
		QString streamTag;              /* the stream request's "id", sent with every sample */
		/* the fast channels of the stream: their period's numbers, sent and cleared every fastTimer tick */
		struct FastWatch {
			QString name;               /* STREAM.CHANNEL, as the map writes them */
			QString stream;
			int channel = 0;
			quint64 count = 0, first = 0;
			double min = 0, max = 0, sum = 0;
		};
		QTimer *fastTimer = nullptr;
		QVector<FastWatch> fastWatches;
	};
	using ClientPtr = std::shared_ptr<JsonClient>;
	using WeakClient = std::weak_ptr<JsonClient>;

	/* Fresh values of some registers: their raw bytes by register key. */
	struct ReadResult {
		bool ok = true;
		QString error;                  /* the first block read that failed */
		QHash<RegKey, QByteArray> raw;
	};
	using ReadDone = std::function<void(const ReadResult &)>;

	/* EVRe pass-through (:1219) */
	void acceptEvreClients();
	void readEvreFrames(QTcpSocket *socket);
	void onEvreRequest(QTcpSocket *socket, const evre::Frame &request);
	void passRead(QTcpSocket *socket, const evre::Frame &request);
	void passWrite(QTcpSocket *socket, const evre::Frame &request);

	/* JSON lines (:1220): one handler per command */
	void acceptJsonClients();
	void readJsonLines(QTcpSocket *socket);
	void onJsonLine(const ClientPtr &client, const QByteArray &line);
	void cmdInfo(JsonClient &client, const QJsonValue &id);
	void cmdList(JsonClient &client, const QJsonValue &id);
	void cmdStop(JsonClient &client, const QJsonValue &id);
	void cmdGet(const ClientPtr &client, const QJsonObject &request, const QJsonValue &id);
	void cmdStream(const ClientPtr &client, const QJsonObject &request, const QJsonValue &id);
	void cmdSet(const ClientPtr &client, const QJsonObject &request, const QJsonValue &id);
	void cmdRead(const ClientPtr &client, const QJsonObject &request, const QJsonValue &id);
	void cmdWrite(const ClientPtr &client, const QJsonObject &request, const QJsonValue &id);
	void cmdBroadcast(const ClientPtr &client, const QJsonObject &request, const QJsonValue &id);

	/* answers on the JSON port; the request's "id" goes back with them */
	void reply(JsonClient &client, QJsonObject answer, const QJsonValue &id);
	void replyOk(JsonClient &client, const QJsonValue &id, QJsonObject answer = {});
	void fail(JsonClient &client, const QString &why, const QJsonValue &id);
	/* the client an answer that came later is for, or null if it has left */
	static ClientPtr stillThere(const WeakClient &client);

	/* a fast channel a request names: its stream and its channel in fastStreams() */
	struct FastRef {
		int stream = 0, channel = 0;
	};
	void startStream(const ClientPtr &client, const QVector<RegKey> &keys, int ms, const QVector<FastRef> &fast,
			int periodMs, const QJsonValue &id);
	void streamTick(const WeakClient &client);
	void fastTick(const WeakClient &client);
	void sendSample(JsonClient &client, const ReadResult &read);
	void stopStream(JsonClient &client);

	/* permission for a write of [addr, addr+count) to a device (the table's slave, RegDef::slave): empty =
	 * allowed, else why not */
	QString writeRefusal(uint8_t tableSlave, uint16_t addr, int count) const;
	/* the same for a broadcast, which reaches every device: the write switch, then the danger registers of any
	 * device that [addr, addr+count) overlaps, in one pass */
	QString broadcastWriteRefusal(uint16_t addr, int count) const;
	/* a "name": value of a set, checked and encoded; empty = fine, else why not */
	QString encodeForSet(const QString &name, const QJsonValue &value, RegKey &key, QByteArray &bytes) const;
	/* the registers a request names, in order, and the fast channels; false and error set at an unknown name */
	bool namedKeys(const QJsonObject &request, QVector<RegKey> &keys, QString &error,
			QVector<FastRef> *fast = nullptr, const QVector<FastState> *streams = nullptr) const;
	/* why a stream's channels cannot be read now (off, or no record yet); empty: they can */
	QString fastRefusal(const FastState &stream, bool needRecord) const;
	/* the pass-through: a write of exactly a stream's enable register, taken by the Studio (true) or not */
	bool takeEnableWrite(QTcpSocket *socket, const evre::Frame &request, const FastState &stream, bool wantsAck);
	void watchBlocks(QTcpSocket *socket, const QString &stream, uint8_t slave, bool on, bool own);
	int rowByName(const QString &name) const;      /* also accepts an address, "0xA002": the selected device's */
	int rowByKey(RegKey key) const;
	QJsonObject valuesJson(const QHash<RegKey, QByteArray> &raw) const;
	QJsonObject decodedJson(const QHash<RegKey, QByteArray> &raw) const;
	/* fresh reads of these registers, merged into block reads; the table gets the values too */
	void readRegisters(const QVector<RegKey> &keys, ReadDone done);
	/* where a request for a device of the table goes; the table's slave of raw requests (the selected device) */
	uint8_t target(uint8_t tableSlave) const;
	uint8_t rawTableSlave() const;

	evre::Master *master_;
	RegTable *table_;   /* the I/O thread's table: this server lives in that thread */
	QTcpServer *evreServer_, *jsonServer_;
	quint16 evrePort_ = 0, jsonPort_ = 0;
	bool allowWrites_ = false, allowDanger_ = false;
	bool bus_ = false;
	QStringList deviceNames_;  /* a bus: its devices' names */
	QHash<QTcpSocket *, evre::Parser> evreClients_;
	/* the pass-through clients that asked for a stream's blocks, by the stream's name: the slave they named (the blocks
	 * come as from it), and whether their own write switched the device (own: their 0 goes to the device too) */
	struct BlockWatch {
		uint8_t slave = 0;
		bool own = false;
	};
	QHash<QTcpSocket *, QHash<QString, BlockWatch>> blockWatches_;
	QHash<QTcpSocket *, ClientPtr> jsonClients_;
	quint64 requests_ = 0;
};
