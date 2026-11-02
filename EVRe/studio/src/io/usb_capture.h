/* SPDX-License-Identifier: Apache-2.0 */
/* Listen, the USB side: what another program says to a device on a port that program holds, read from the system's
 * own USB capture (USBPcap on Windows, usbmon on Linux). Nothing here sends anything or knows EVRe: the bytes come out
 * as they went over the wire, one stream each way, and the parser above finds the frames.
 *
 *   UsbTarget        the device and the endpoints a capture listens to
 *   UsbPortState     what Listen can do with a port: the Listen line, the pill's refusal, Connect
 *   UsbEvent         one bulk transfer's bytes, as heard
 *   UsbPcapDecoder   USBPcap's pcap stream -> events, on every system, so CI checks the Windows format on Linux too
 *   UsbmonDecoder    usbmon's binary records -> events, the same
 *   UsbCapture       a source of heard bytes
 *   FileCapture      a capture replayed from a file: what the tests listen to
 *   usbmonPortState  a port to its USB device on Linux, from files only, so a test runs it on a tree of its own
 *   UsbmonCapture    the live capture on Linux (usbmon_capture.h, built there only)
 *
 * The decoders take bytes as they come: a stream may be cut at any byte, and what is not a whole record yet waits for
 * the next feed. Windows's live source (USBPcap's program) and its lookup come with their own part.
 */
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

class QTimer;

/* USB's words: Out is the other program's bytes, In the device's. */
enum class UsbDirection { Out, In };

struct UsbPortState;

/* What a capture listens to. The endpoints are their addresses (bit 7 set on an IN endpoint); a list left empty hears
 * every bulk endpoint of that direction, for a lookup that could not read the descriptors. */
struct UsbTarget {
	enum Kind { Cdc, Ftdi, OtherBridge };

	QString port;               /* COM13, ttyACM0 */
	int bus = 0;                /* the root hub: a capture opens one bus, so the decoders do not filter on it */
	int address = 0;            /* the device's address on that bus */
	quint16 vid = 0;
	quint16 pid = 0;
	Kind kind = Cdc;
	QVector<int> inEndpoints;
	QVector<int> outEndpoints;
	int inMaxPacket = 0;        /* the IN endpoint's wMaxPacketSize; 0: not known (FTDI then goes by FTDI_PACKET) */

	/* What Listen can do with this port now, a state forced by a test first. The window's thread */
	static UsbPortState stateOf(const QString &port);
	/* The system's own answer, the one place it is asked: each system's lookup and its install check come in here
	 * (usb_capture.cpp). It only reads files, on any thread; a system without a lookup yet says NoCapture */
	static UsbPortState liveStateOf(const QString &port);
	/* tests: the state stateOf gives this port from now on, whatever the system says (the window's thread) */
	static void stateForTests(const QString &port, const UsbPortState &state);
};

/* What Listen can do with a port. Everything but Listenable is a reason it cannot, said on the Listen line and, when
 * Connect is tried, in the pill */
struct UsbPortState {
	enum Kind {
		NoCapture,      /* nothing on this system can hear USB */
		Listenable,
		NotUsb,         /* a motherboard's COM port, a Bluetooth or a virtual one */
		NoUsbPcap,      /* Windows: USBPcap is not installed */
		NoUsbmon,       /* Linux: usbmon is not loaded, or its device cannot be read */
	};
	Kind kind = NoCapture;
	UsbTarget target;           /* Listenable: what to listen to; NoUsbmon: what would be */
	QString device;             /* NoUsbmon: the file that cannot be read, /dev/usbmon2 */

	/* still the device a capture was started on? Plugged in again it has a new address, which a filter on the old one
	 * would never hear */
	bool holds(const UsbTarget &running) const {
		return kind != NotUsb && target.bus == running.bus && target.address == running.address;
	}
};

/* What sysfs says of a port, and whether its bus can be read: sys and dev are /sys and /dev, or a tree a test made.
 *
 *   <sys>/class/tty/ttyACM0/device  -> .../usb2/2-3/2-3:1.0              a link to the tty's interface
 *   <sys>/class/tty/ttyUSB0/device  -> .../usb2/2-1/2-1:1.0/ttyUSB0      a bridge's: one level more
 *   .../usb2/2-3                    busnum, devnum, idVendor, idProduct: the walk up ends here
 *   .../usb2/2-3/2-3:1.1            bInterfaceClass, bInterfaceNumber, ep_81/, ep_01/
 *   .../ep_81                       bEndpointAddress, type, wMaxPacketSize
 *
 * Nothing here needs Linux, so the tests run on every system; but a link cannot be made everywhere, nor an interface's
 * colon on Windows: `links` gives what a link leads to (full path -> full path), and one it does not name is followed
 * by the file system. */
UsbPortState usbmonPortState(const QString &sys, const QString &dev, const QString &port,
		const QHash<QString, QString> &links = {});

/* One transfer's bytes. Only bulk transfers of the target with data become events; the capture's own stamp is kept
 * for a later Monitor column (the frames are laid on the host's clock at arrival, as a serial link's are). */
struct UsbEvent {
	UsbDirection direction = UsbDirection::In;
	int endpoint = 0;           /* the address: bit 7 set on IN */
	int transfer = 0;           /* 3: bulk, the number both captures use */
	int status = 0;             /* as captured: 0 on every IN; an OUT is taken at its submission, where usbmon says -115 */
	QByteArray data;            /* bridges' framing already taken off */
	qint64 stamp = 0;           /* microseconds since 1970, UTC */
};

/* What the two formats share: the buffer of a stream cut anywhere, the target's filter and FTDI's status bytes. */
class UsbDecoder {
public:
	static constexpr int BULK = 3;
	static constexpr int FTDI_PACKET = 64;      /* FT232R, FT231X: the rule when wMaxPacketSize is not known */
	/* no record is longer than usbmon's ring (1200 KiB) or USBPcap's snap length; a longer one is a broken stream,
	 * and waiting for it would hold the bytes for ever */
	static constexpr int MAX_RECORD = 2 * 1024 * 1024;

	explicit UsbDecoder(const UsbTarget &target) : target_(target) {}
	virtual ~UsbDecoder() = default;
	UsbDecoder(const UsbDecoder &) = delete;
	UsbDecoder &operator=(const UsbDecoder &) = delete;

	/* the next bytes of the stream; the events of every record now whole go to the end of `out` */
	void feed(const QByteArray &bytes, QVector<UsbEvent> &out);
	/* bytes of a record not whole yet: at the end of a stream, what a cut left behind */
	qsizetype pendingBytes() const { return buffer_.size(); }
	/* empty, or why the stream cannot be read: nothing more is taken from it */
	const QString &error() const { return error_; }

protected:
	/* the record at `bytes`: its events added, its size returned; 0 when more bytes are needed or error_ was set */
	virtual qsizetype record(const char *bytes, qsizetype size, QVector<UsbEvent> &out) = 0;
	void fail(const QString &why) { error_ = why; }
	/* one direction's bytes of one transfer, heard if the target is that device and endpoint */
	void hear(UsbDirection direction, int device, int endpoint, int status, const char *bytes, qsizetype size,
			qint64 stamp, QVector<UsbEvent> &out) const;

private:
	QByteArray withoutFtdiStatus(const char *bytes, qsizetype size) const;

	UsbTarget target_;
	QByteArray buffer_;
	QString error_;
};

/* USBPcap's stream: a 24-byte pcap header (link type 249), then per record 16 bytes and the driver's packet, its own
 * length first (27 bytes on a bulk transfer) and the data after it. The driver logs a transfer twice: OUT's bytes come
 * with the submission, IN's with the completion, which must say status 0. */
class UsbPcapDecoder : public UsbDecoder {
public:
	using UsbDecoder::UsbDecoder;

protected:
	qsizetype record(const char *bytes, qsizetype size, QVector<UsbEvent> &out) override;

private:
	void packet(const char *bytes, qsizetype size, qint64 stamp, QVector<UsbEvent> &out) const;

	bool headerSeen_ = false;
};

/* usbmon's records: the 64-byte mon_bin_hdr and its len_cap bytes of data, one after another, as MON_IOCX_GETX gives
 * them. The same rule as USBPcap's: OUT from 'S', IN from 'C' with status 0. */
class UsbmonDecoder : public UsbDecoder {
public:
	using UsbDecoder::UsbDecoder;

	static constexpr int HEADER = 64;   /* a record's mon_bin_hdr */

	/* A record as MON_IOCX_GETX leaves it: the header goes on saying what usbmon kept even when the buffer took less,
	 * and then no record after it could be found. Its len_cap cut to what the `room` of the buffer held; its size */
	static qsizetype fitRecord(char *bytes, quint32 room);

protected:
	qsizetype record(const char *bytes, qsizetype size, QVector<UsbEvent> &out) override;
};

/* A source of heard bytes. start() returns at once: what is heard comes later, from the event loop, so a listener that
 * says "opened" after start() says it before the first bytes. */
class UsbCapture : public QObject {
	Q_OBJECT
public:
	using QObject::QObject;
	/* the system's live capture, unstarted; nullptr where it has none yet */
	static UsbCapture *live();
	virtual void start(const UsbTarget &target) = 0;
	virtual void stop() = 0;    /* nothing follows it, not even ended() */

signals:
	void data(UsbDirection direction, const QByteArray &bytes);
	void dropped(quint64 total);        /* captures the system says it lost, so far */
	void ended(const QString &why);     /* it stopped by itself */
};

/* A capture replayed from a file, .pcap (as USBPcap writes it) or .usbmon (the records as UsbmonDecoder takes them). */
class FileCapture : public UsbCapture {
	Q_OBJECT
public:
	enum Pace {
		AtOnce,     /* every transfer in one turn of the event loop */
		ByStamps,   /* at the distances of its stamps, the first at once */
	};
	explicit FileCapture(const QString &file, Pace pace = AtOnce, QObject *parent = nullptr);
	void start(const UsbTarget &target) override;
	void stop() override;

private:
	void next();

	QString file_;
	Pace pace_;
	QTimer *timer_;
	QVector<UsbEvent> events_;
	int next_ = 0;
	QString why_;               /* what ended() will say */
	bool running_ = false;
	QElapsedTimer clock_;
};
