/* SPDX-License-Identifier: Apache-2.0 */
/* The TCP and serial links: open, close, send, and what each reports back. */
#include "evre/link.h"

#include <QElapsedTimer>
#include <QSerialPort>
#include <QTcpSocket>

namespace evre {

namespace {

/* The errors after which the port is gone: unplugged, or held by another program. */
bool losesPort(QSerialPort::SerialPortError error) {
	switch (error) {
	case QSerialPort::ResourceError:
	case QSerialPort::PermissionError:
	case QSerialPort::DeviceNotFoundError:
	case QSerialPort::OpenError: return true;
	default: return false;
	}
}

} // namespace

/* ---------------------------------------------------------------------- TCP */

TcpLink::TcpLink(const QString &host, quint16 port, QObject *parent)
	: Link(parent), host_(host), port_(port), socket_(new QTcpSocket(this)) {
	connect(socket_, &QTcpSocket::connected, this, [this] {
		socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
		emit opened();
	});
	connect(socket_, &QTcpSocket::readyRead, this, [this] { emit received(socket_->readAll()); });
	connect(socket_, &QTcpSocket::disconnected, this, [this] {
		emit closed(closing_ ? QString() : tr("closed by %1").arg(describe()));
		closing_ = false;
	});
	connect(socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
		if (error == QAbstractSocket::RemoteHostClosedError) return; /* disconnected() says it */
		/* a connect that failed; while connected, disconnected() reports the end */
		if (socket_->state() != QAbstractSocket::ConnectedState) emit closed(socket_->errorString());
	});
}

void TcpLink::open() {
	closing_ = false;
	socket_->connectToHost(host_, port_);
}

void TcpLink::close() {
	closing_ = true;
	socket_->abort();
	emit closed(QString());
}

bool TcpLink::isOpen() const {
	return socket_->state() == QAbstractSocket::ConnectedState;
}

void TcpLink::send(const QByteArray &bytes) {
	if (isOpen()) socket_->write(bytes);
}

void TcpLink::flush(int ms) {
	if (isOpen() && socket_->bytesToWrite() > 0) socket_->waitForBytesWritten(ms);
}

/* Closed with bytes unread (a device sending by itself, AUTO_SEND), the connection is reset (RST) rather than ended,
 * and the device's side drops what it has not read yet: the AUTO_SEND off sent just before, so the device went on
 * sending (seen as a test failing now and then). Read until the device is quiet first: it stops once it has the off */
void TcpLink::drain(int ms) {
	constexpr int QUIET_MS = 20;
	QElapsedTimer waited;
	waited.start();
	while (isOpen() && waited.elapsed() < ms && socket_->waitForReadyRead(QUIET_MS)) socket_->readAll();
}

QString TcpLink::describe() const {
	return QStringLiteral("%1:%2").arg(host_).arg(port_);
}

/* ------------------------------------------------------------------- serial */

SerialLink::SerialLink(const QString &port, qint32 baud, QObject *parent)
	: Link(parent), portName_(port), baud_(baud), serial_(new QSerialPort(this)) {
	connect(serial_, &QSerialPort::readyRead, this, [this] { emit received(serial_->readAll()); });
	connect(serial_, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
		if (!losesPort(error)) return;
		const QString why = serial_->errorString();
		if (serial_->isOpen()) serial_->close();
		emit closed(why);
	});
}

void SerialLink::open() {
	serial_->setPortName(portName_);
	serial_->setBaudRate(baud_);
	serial_->setDataBits(QSerialPort::Data8);
	serial_->setParity(QSerialPort::NoParity);
	serial_->setStopBits(QSerialPort::OneStop);
	serial_->setFlowControl(QSerialPort::NoFlowControl);
	if (!serial_->open(QIODevice::ReadWrite)) {
		emit closed(serial_->errorString());
		return;
	}
	serial_->setDataTerminalReady(true); /* USB CDC devices often wait for DTR */
	serial_->clear();
	emit opened();
}

void SerialLink::close() {
	if (serial_->isOpen()) serial_->close();
	emit closed(QString());
}

bool SerialLink::isOpen() const {
	return serial_->isOpen();
}

void SerialLink::send(const QByteArray &bytes) {
	if (serial_->isOpen()) serial_->write(bytes);
}

void SerialLink::flush(int ms) {
	if (serial_->isOpen() && serial_->bytesToWrite() > 0) serial_->waitForBytesWritten(ms);
}

QString SerialLink::describe() const {
	return QStringLiteral("%1 @ %2").arg(portName_).arg(baud_);
}

} // namespace evre
