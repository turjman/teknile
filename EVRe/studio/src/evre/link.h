/* SPDX-License-Identifier: Apache-2.0 */
/* A byte pipe to an EVRe device: TCP (any EVRe-over-TCP server) or a serial /
 * USB CDC port. The master above does not care which.
 *
 * The socket or port inside a link is its child: it lives in the thread the
 * link lives in, and moves with it.
 */
#pragma once

#include <QObject>
#include <QString>

class QTcpSocket;
class QSerialPort;

namespace evre {

/* What the master needs of a link: open it, send bytes, and hear what comes back. */
class Link : public QObject {
	Q_OBJECT
public:
	using QObject::QObject;
	virtual void open() = 0;                        /* opened() or closed(why) follows */
	virtual void close() = 0;
	virtual bool isOpen() const = 0;
	virtual void send(const QByteArray &bytes) = 0; /* dropped while not open */
	/* what was sent goes out now, waiting at most ms (a last frame before close(), which may drop what is queued) */
	virtual void flush(int ms) { (void) ms; }
	/* what still comes read and dropped until it stops (a quiet QUIET_MS) or ms passed: before close() */
	virtual void drain(int ms) { (void) ms; }
	virtual QString describe() const = 0;           /* where it goes, for the log */

signals:
	void opened();
	void closed(const QString &why); /* why is empty when close() was asked for */
	void received(const QByteArray &bytes);
};

/* TCP with Nagle off, so each request leaves at once. */
class TcpLink : public Link {
	Q_OBJECT
public:
	TcpLink(const QString &host, quint16 port, QObject *parent = nullptr);
	void open() override;
	void close() override;
	bool isOpen() const override;
	void send(const QByteArray &bytes) override;
	void flush(int ms) override;
	void drain(int ms) override;
	QString describe() const override;

private:
	QString host_;
	quint16 port_;
	QTcpSocket *socket_;
	bool closing_ = false; /* close() was called: the disconnect is ours, not news */
};

/* A serial port at 8N1, no flow control. */
class SerialLink : public Link {
	Q_OBJECT
public:
	SerialLink(const QString &port, qint32 baud, QObject *parent = nullptr);
	void open() override;
	void close() override;
	bool isOpen() const override;
	void send(const QByteArray &bytes) override;
	void flush(int ms) override;
	QString describe() const override;

private:
	QString portName_;
	qint32 baud_;
	QSerialPort *serial_;
};

} // namespace evre
