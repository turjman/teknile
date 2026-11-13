/* SPDX-License-Identifier: Apache-2.0 */
/* What the phone's system does for the app: see phone_system.h. On Android each call is one of EvreService's static
 * methods (android/src/com/teknile/evre/EvreService.java), with the activity as its context. */
#include "phone/phone_system.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>

namespace {

/* ActivityInfo's SCREEN_ORIENTATION_UNSPECIFIED, _SENSOR_PORTRAIT, _SENSOR_LANDSCAPE: either way up */
constexpr int ANY = -1, SENSOR_PORTRAIT = 7, SENSOR_LANDSCAPE = 6;
const char *const SERVICE = "com/teknile/evre/EvreService";

class AndroidSystem : public SystemBridge {
public:
	void holdService(const QString &title, const QString &text) override {
		QJniObject::callStaticMethod<void>(SERVICE, "hold", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
				context().object(), QJniObject::fromString(title).object<jstring>(),
				QJniObject::fromString(text).object<jstring>());
	}
	void releaseService() override {
		QJniObject::callStaticMethod<void>(SERVICE, "release", "(Landroid/content/Context;)V", context().object());
	}
	void keepScreenOn(bool on) override {
		QJniObject::callStaticMethod<void>(SERVICE, "keepScreenOn", "(Landroid/content/Context;Z)V", context().object(),
				jboolean(on));
	}
	void requestOrientation(Orientation orientation) override {
		const int value = orientation == Portrait ? SENSOR_PORTRAIT : orientation == Landscape ? SENSOR_LANDSCAPE : ANY;
		QJniObject::callStaticMethod<void>(SERVICE, "orientation", "(Landroid/content/Context;I)V", context().object(),
				jint(value));
	}

private:
	static QJniObject context() { return QNativeInterface::QAndroidApplication::context(); }
};

} // namespace

SystemBridge *SystemBridge::system() {
	static AndroidSystem android;
	return &android;
}

#else

SystemBridge *SystemBridge::system() { return nullptr; }

#endif
