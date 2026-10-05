/* SPDX-License-Identifier: Apache-2.0 */
/* The EVRe master: the request queue, pipelining, answer matching, timeouts
 * and the keep-alive (see master.h). */
#include "evre/master.h"

#include <QTimer>
#include <utility>

#include "evre/registers.h"

namespace evre {

namespace {

constexpr int KEEPALIVE_MS = 400;            /* well inside a server's ~1 s limit for a silent client */
constexpr uint16_t KEEPALIVE_BYTES = 2;      /* a read of DEVICE_ID: every EVRe device has it */

Result failure(const QString &message) {
	Result result;
	result.message = message;
	return result;
}

} // namespace

Master::Master(QObject *parent)
	: QObject(parent), timeoutTimer_(new QTimer(this)), keepAliveTimer_(new QTimer(this)) {
	/* children, not plain members: a timer that stayed behind in the thread the
	 * master was made in could not be started from the thread it runs in */
	timeoutTimer_->setSingleShot(true);
	timeoutTimer_->setTimerType(Qt::PreciseTimer);
	connect(timeoutTimer_, &QTimer::timeout, this, &Master::onTimeout);
	keepAliveTimer_->setInterval(KEEPALIVE_MS);
	connect(keepAliveTimer_, &QTimer::timeout, this, &Master::sendKeepAlive);
}

void Master::setLink(Link *link) {
	if (link_) disconnect(link_, nullptr, this, nullptr);
	clear();
	parser_.clear();
	link_ = link;
	if (link_) {
		connect(link_, &Link::received, this, &Master::onReceived);
		keepAliveTimer_->start();
	} else {
		keepAliveTimer_->stop();
	}
}

/* ----------------------------------------------------------------- requests */

void Master::read(uint16_t addr, uint16_t count, Callback callback) {
	readFrom(slave_, addr, count, std::move(callback));
}

void Master::write(uint16_t addr, const QByteArray &data, Callback callback) {
	writeTo(slave_, addr, data, std::move(callback));
}

void Master::writeNoAck(uint16_t addr, const QByteArray &data, Callback callback) {
	writeNoAckTo(slave_, addr, data, std::move(callback));
}

void Master::readFrom(uint8_t slave, uint16_t addr, uint16_t count, Callback callback) {
	enqueue({ slave, READ, addr, count, {}, std::move(callback) });
}

void Master::writeTo(uint8_t slave, uint16_t addr, const QByteArray &data, Callback callback) {
	enqueue({ slave, WRITE_ACK, addr, uint16_t(data.size()), data, std::move(callback) });
}

void Master::writeNoAckTo(uint8_t slave, uint16_t addr, const QByteArray &data, Callback callback) {
	enqueue({ slave, WRITE, addr, uint16_t(data.size()), data, std::move(callback) });
}

void Master::clear() {
	/* both lists are emptied before any callback runs: a callback may queue anew */
	const QList<Pending> sent = std::exchange(pending_, {});
	const QQueue<Request> waiting = std::exchange(queue_, {});
	timeoutTimer_->stop();
	const Result cancelled = failure(tr("cancelled"));
	for (const Pending &p : sent) {
		if (p.request.callback) p.request.callback(cancelled);
	}
	for (const Request &request : waiting) {
		if (request.callback) request.callback(cancelled);
	}
}

/* a READ or a WRITE_ACK to the broadcast address would be answered by every device at once: refused unsent */
void Master::enqueue(Request request) {
	if (request.slave == BROADCAST && request.fn != WRITE) {
		if (request.callback)
			request.callback(failure(tr("slave 0 is the broadcast address: only a WRITE without acknowledge goes to it")));
		return;
	}
	queue_.enqueue(std::move(request));
	sendQueued();
}

/* Sends from the head of the queue as far as the in-flight limit allows. */
void Master::sendQueued() {
	while (!queue_.isEmpty() && pending_.size() < inFlight_ && !nextMustWait()) {
		Request request = queue_.dequeue();
		if (!link_ || !link_->isOpen()) {
			if (request.callback) request.callback(failure(tr("not connected")));
			continue;
		}
		const QByteArray frame = build(request.slave, request.fn, request.addr, request.cnt, request.data);
		link_->send(frame);
		stats_.tx++;
		emit frameSent(frame);
		if (request.fn == WRITE) { /* no answer comes: done once sent */
			Result sentOk;
			sentOk.ok = true;
			if (request.callback) request.callback(sentOk);
			continue;
		}
		Pending sent;
		sent.request = std::move(request);
		sent.sinceSent.start();
		pending_.push_back(std::move(sent));
		if (pending_.size() == 1) armTimeout();
	}
}

/* Only reads overlap. A write waits for everything sent before it, and
 * nothing is sent while a write is unanswered: a write followed by a read of
 * the same register must see the new value. */
bool Master::nextMustWait() const {
	if (pending_.isEmpty()) return false;
	return queue_.head().fn != READ || pending_.last().request.fn != READ;
}

/* ------------------------------------------------------------------ answers */

void Master::onReceived(const QByteArray &bytes) {
	parser_.feed(bytes);
	Frame frame;
	while (parser_.next(frame)) {
		stats_.rx++;
		stats_.badFrames = quint64(parser_.badFrames());
		const int index = findPending(frame);
		if (index < 0) {
			emit frameReceived(frame.raw, fnName(frame.fn) + tr(" (not a pending request)"));
			emit unsolicited(frame);
			continue;
		}
		emit frameReceived(frame.raw, fnName(frame.fn));
		finish(index, resultOf(frame));
	}
}

/* The oldest pending request this frame answers: from its slave, the same
 * offset and count (every answer echoes them), and the answer its function
 * code asks for or an error. -1 when there is none: a frame the device sent
 * by itself (AUTO_SEND: its read-only block at 0xD000) is not taken for the
 * answer to a read of part of it. Nor is an answer with another count than
 * asked: the request times out, and the frame is reported as unsolicited. */
int Master::findPending(const Frame &answer) const {
	for (int i = 0; i < pending_.size(); i++) {
		const Request &request = pending_[i].request;
		const uint8_t expected = request.fn == READ ? READ_RESP : WRITE_ACK_RESP;
		if (answer.slave == request.slave && answer.addr == request.addr && answer.cnt == request.cnt
				&& (answer.fn == expected || answer.fn == ERROR_RESP))
			return i;
	}
	return -1;
}

/* findPending took the frame only with the count asked for: a READ_RESP here holds the bytes asked for */
Result Master::resultOf(const Frame &answer) const {
	Result result;
	if (answer.fn == ERROR_RESP) {
		result.error = answer.data.isEmpty() ? 0 : uint8_t(answer.data[0]);
		result.message = errorName(result.error);
	} else {
		result.ok = true;
		result.data = answer.data;
	}
	return result;
}

/* Takes a request off the pending list, counts it, hands it its result and
 * sends what may go now. */
void Master::finish(int index, Result result) {
	const Pending done = pending_.takeAt(index);
	result.latencyMs = double(done.sinceSent.nsecsElapsed()) / 1e6;
	if (result.ok) {
		stats_.ok++;
		stats_.lastLatencyMs = result.latencyMs;
		/* an exponential average over roughly the last 20 answers */
		stats_.avgLatencyMs = stats_.avgLatencyMs == 0 ? result.latencyMs
				: 0.95 * stats_.avgLatencyMs + 0.05 * result.latencyMs;
	} else if (result.error != 0) {
		stats_.errors++;
	}
	if (index == 0) armTimeout();
	if (done.request.callback) done.request.callback(result);
	sendQueued();
}

/* --------------------------------------------------- timeout and keep-alive */

/* The timer runs for the oldest pending request only, for the time it has left. */
void Master::armTimeout() {
	if (pending_.isEmpty()) {
		timeoutTimer_->stop();
		return;
	}
	const qint64 left = timeoutMs_ - pending_.first().sinceSent.elapsed();
	timeoutTimer_->start(int(left > 0 ? left : 0));
}

void Master::onTimeout() {
	/* the oldest request has waited too long; a late answer may still come:
	 * the offset check skips it, or it is reported as unsolicited */
	if (pending_.isEmpty()) return;
	if (pending_.first().sinceSent.elapsed() < timeoutMs_) {
		armTimeout(); /* fired a little early: wait the rest */
		return;
	}
	stats_.timeouts++;
	Result timedOut = failure(tr("timeout (%1 ms)").arg(timeoutMs_));
	timedOut.timedOut = true;
	finish(0, timedOut);
}

/* Every KEEPALIVE_MS while a link is set: a short read, when nothing is
 * queued or waiting for an answer at that moment. */
void Master::sendKeepAlive() {
	if (!keepAlive_ || !link_ || !link_->isOpen() || !pending_.isEmpty() || !queue_.isEmpty()) return;
	if (slave_ == BROADCAST) return; /* nobody would answer it */
	enqueue({ slave_, READ, DEVICE_ID, KEEPALIVE_BYTES, {}, {} });
}

} // namespace evre
