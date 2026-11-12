/* SPDX-License-Identifier: Apache-2.0 */
/* The bridge to Android's USB host API: the Java layer (android/src/com/teknile/evre/EvreUsb.java) called over JNI,
 * and its native methods, which pass each word to the sink of the device being opened. Android only (CMakeLists.txt).
 *
 * Java calls the native methods from its reader thread and from Android's main thread; the sink is swapped under a
 * mutex, so a word that comes while close() runs goes nowhere rather than to a link that is gone.
 */
#include "evre/usb_bridge.h"

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMutex>
#include <QMutexLocker>

#include <jni.h>

namespace evre {

namespace {

constexpr char JAVA_CLASS[] = "com/teknile/evre/EvreUsb";

QMutex sinkMutex;
UsbBridge::Sink *currentSink = nullptr;

/* the sink of the device being opened, while the mutex is held: what a native method tells it */
template <typename Tell> void tellSink(Tell tell) {
	QMutexLocker locker(&sinkMutex);
	if (currentSink) tell(currentSink);
}

QString fromJava(JNIEnv *env, jstring text) {
	if (!text) return {};
	const char *chars = env->GetStringUTFChars(text, nullptr);
	const QString result = QString::fromUtf8(chars);
	env->ReleaseStringUTFChars(text, chars);
	return result;
}

void onAsking(JNIEnv *, jclass) { tellSink([](UsbBridge::Sink *sink) { sink->usbAsking(); }); }
void onOpened(JNIEnv *, jclass) { tellSink([](UsbBridge::Sink *sink) { sink->usbOpened(); }); }
void onRefused(JNIEnv *, jclass) { tellSink([](UsbBridge::Sink *sink) { sink->usbRefused(); }); }

void onFailed(JNIEnv *env, jclass, jstring why) {
	const QString text = fromJava(env, why);
	tellSink([&text](UsbBridge::Sink *sink) { sink->usbFailed(text); });
}

/* the one copy of the bytes: Java's array into a QByteArray, which the sink queues on */
void onBytes(JNIEnv *env, jclass, jbyteArray bytes, jint length) {
	QByteArray data(int(length), Qt::Uninitialized);
	env->GetByteArrayRegion(bytes, 0, length, reinterpret_cast<jbyte *>(data.data()));
	tellSink([&data](UsbBridge::Sink *sink) { sink->usbBytes(data); });
}

void onGone(JNIEnv *, jclass) { tellSink([](UsbBridge::Sink *sink) { sink->usbGone(); }); }

QJniObject context() { return QNativeInterface::QAndroidApplication::context(); }

class JniUsbBridge : public UsbBridge {
public:
	JniUsbBridge() {
		const JNINativeMethod methods[] = {
			{ "onAsking", "()V", reinterpret_cast<void *>(onAsking) },
			{ "onOpened", "()V", reinterpret_cast<void *>(onOpened) },
			{ "onRefused", "()V", reinterpret_cast<void *>(onRefused) },
			{ "onFailed", "(Ljava/lang/String;)V", reinterpret_cast<void *>(onFailed) },
			{ "onBytes", "([BI)V", reinterpret_cast<void *>(onBytes) },
			{ "onGone", "()V", reinterpret_cast<void *>(onGone) },
		};
		QJniEnvironment env;
		if (!env.registerNativeMethods(JAVA_CLASS, methods, int(std::size(methods))))
			qWarning("EvreUsb: the native methods could not be registered");
	}

	/* each device a line "name\tvid\tpid\tproduct" (Java's), so one call brings the list */
	QVector<Device> devices() override {
		QVector<Device> list;
		const QJniObject lines = QJniObject::callStaticObjectMethod(JAVA_CLASS, "devices",
				"(Landroid/content/Context;)Ljava/lang/String;", context().object());
		for (const QString &line : lines.toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
			const QStringList parts = line.split(QLatin1Char('\t'));
			if (parts.size() < 3) continue;
			Device device;
			device.name = parts[0];
			device.vid = quint16(parts[1].toUInt());
			device.pid = quint16(parts[2].toUInt());
			device.product = parts.value(3);
			list << device;
		}
		return list;
	}

	void open(const QString &name, qint32 baud, Sink *sink) override {
		close();
		{
			QMutexLocker locker(&sinkMutex);
			currentSink = sink;
		}
		QJniObject::callStaticMethod<void>(JAVA_CLASS, "open", "(Landroid/content/Context;Ljava/lang/String;I)V",
				context().object(), QJniObject::fromString(name).object<jstring>(), jint(baud));
	}

	void send(const QByteArray &bytes) override {
		QJniEnvironment env;
		jbyteArray array = env->NewByteArray(jsize(bytes.size()));
		env->SetByteArrayRegion(array, 0, jsize(bytes.size()), reinterpret_cast<const jbyte *>(bytes.constData()));
		QJniObject::callStaticMethod<void>(JAVA_CLASS, "write", "([B)V", array);
		env->DeleteLocalRef(array);
	}

	/* the sink first: once Java's close() has stopped its reader, nothing more is said of this device */
	void close() override {
		{
			QMutexLocker locker(&sinkMutex);
			currentSink = nullptr;
		}
		QJniObject::callStaticMethod<void>(JAVA_CLASS, "close", "()V");
	}
};

} // namespace

UsbBridge *UsbBridge::system() {
	static JniUsbBridge bridge;
	return &bridge;
}

} // namespace evre
