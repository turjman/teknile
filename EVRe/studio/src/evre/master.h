/* SPDX-License-Identifier: Apache-2.0 */
/* The EVRe master: requests queued, up to `inFlight` of them sent before
 * their answers come (1 = strictly one at a time; more pipelines them, for a
 * TCP server that takes several requests at once), each answer matched to
 * its request by slave, function code and offset, timeouts, statistics, and a
 * keep-alive while nothing else is asked (some servers drop a client that
 * stays silent for about a second, and a device may watch its host the same
 * way).
 *
 * An answer must echo its request's slave, offset and count. A device that
 * answers with another count is not taken for the answer: its request times
 * out, and the frame is reported as unsolicited.
 *
 * Several devices may share the link: each request goes to a slave, the
 * master's own (setSlave) or the one given (readFrom, writeTo, ...). Slave 0
 * is the broadcast address: every device hears it and none answers, so only
 * a WRITE (no acknowledge) may go to it; a READ or a WRITE_ACK to it fails
 * at once, unsent.
 *
 * The master's timers are its children, so moveToThread takes them along: the
 * master may be moved to an I/O thread, alone or as the child of another object.
 */
#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QQueue>
#include <functional>

#include "evre/frame.h"
#include "evre/link.h"

class QTimer;

namespace evre {

/* How a request ended, handed to its callback. */
struct Result {
	bool ok = false;
	uint8_t error = 0;     /* ERROR_RESP code, 0 when the failure was a timeout / the link */
	QString message;       /* what went wrong, in words */
	QByteArray data;       /* READ: the values */
	double latencyMs = 0;  /* from sending the request to its answer */
	bool timedOut = false; /* no answer in time (the device may be gone), not cancelled nor refused */
};

/* The master itself: see the top of this file. */
class Master : public QObject {
	Q_OBJECT
public:
	using Callback = std::function<void(const Result &)>;

	explicit Master(QObject *parent = nullptr);

	void setLink(Link *link);          /* takes no ownership; nullptr detaches */
	void setSlave(uint8_t slave) { slave_ = slave; }
	uint8_t slave() const { return slave_; }
	void setTimeoutMs(int ms) { timeoutMs_ = ms; }
	void setKeepAlive(bool on) { keepAlive_ = on; }
	/* requests sent before their answers come; 1 for a device on a UART */
	void setInFlight(int count) { inFlight_ = count < 1 ? 1 : count; }
	int inFlight() const { return inFlight_; }

	/* to the master's slave */
	void read(uint16_t addr, uint16_t count, Callback callback = {});
	void write(uint16_t addr, const QByteArray &data, Callback callback = {}); /* with acknowledge */
	void writeNoAck(uint16_t addr, const QByteArray &data, Callback callback = {}); /* called once sent */
	/* to the slave given: one device of several on the link; writeNoAckTo(evre::BROADCAST, ...) is a broadcast */
	void readFrom(uint8_t slave, uint16_t addr, uint16_t count, Callback callback = {});
	void writeTo(uint8_t slave, uint16_t addr, const QByteArray &data, Callback callback = {});
	void writeNoAckTo(uint8_t slave, uint16_t addr, const QByteArray &data, Callback callback = {});
	void clear();                      /* drop everything queued */

	int queued() const { return int(queue_.size() + pending_.size()); }

	struct Stats {
		quint64 tx = 0, rx = 0, ok = 0, errors = 0, timeouts = 0, badFrames = 0;
		double lastLatencyMs = 0, avgLatencyMs = 0;
	};
	const Stats &stats() const { return stats_; }

signals:
	void frameSent(const QByteArray &raw);
	void frameReceived(const QByteArray &raw, const QString &what);
	void unsolicited(const evre::Frame &frame); /* e.g. AUTO_SEND, or a late answer */

private:
	struct Request {
		uint8_t slave;
		uint8_t fn;
		uint16_t addr, cnt;
		QByteArray data;
		Callback callback;
	};
	struct Pending {
		Request request;
		QElapsedTimer sinceSent;
	};

	void enqueue(Request request);
	void sendQueued();
	bool nextMustWait() const;
	void onReceived(const QByteArray &bytes);
	int findPending(const Frame &answer) const;
	Result resultOf(const Frame &answer) const;
	void finish(int index, Result result);
	void armTimeout();
	void onTimeout();
	void sendKeepAlive();

	Link *link_ = nullptr;
	Parser parser_;
	QQueue<Request> queue_;           /* not sent yet */
	QList<Pending> pending_;          /* sent, answer not yet in; oldest first */
	QTimer *timeoutTimer_;            /* runs for the oldest pending request */
	QTimer *keepAliveTimer_;
	uint8_t slave_ = 1;
	int timeoutMs_ = 500;
	int inFlight_ = 1;
	bool keepAlive_ = true;
	Stats stats_;
};

} // namespace evre
