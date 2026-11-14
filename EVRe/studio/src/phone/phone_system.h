/* SPDX-License-Identifier: Apache-2.0 */
/* What the phone's system does for the app (the phone's plan, A.4, A-5, A-6, A-16), as PhoneSession asks for it:
 *
 *   the service       Android's foreground service of type connectedDevice with its notification while a link is
 *                     wanted, so the link and a recording go on with the screen off (EvreService.java); its text the
 *                     link's state and the recording's time
 *   the screen        kept on while connected (A-5, the setting android/keepScreenOn, on by default)
 *   the orientation   the one a screen asks for (A-16): landscape for the Chart and the Monitor, any (as the phone
 *                     is held) for the Registers and the Log
 *   to the background Android's back with nothing open while a link is wanted or a recording runs: the task moved
 *                     to the background as Home does (Activity.moveTaskToBack), not finished, so the link, the service
 *                     and the recording go on
 *
 * On Android Java does it (system()); elsewhere nothing does, and the test sets a stand-in that notes what it was
 * asked (PhoneSession::setSystemBridgeForTests). Called on the window's thread. */
#pragma once

#include <QString>

class SystemBridge {
public:
	enum Orientation { Any, Portrait, Landscape };

	virtual ~SystemBridge() = default;
	/* the service up with its notification, or its text changed while it is up */
	virtual void holdService(const QString &title, const QString &text) = 0;
	/* the service and its notification gone */
	virtual void releaseService() = 0;
	virtual void keepScreenOn(bool on) = 0;
	virtual void requestOrientation(Orientation orientation) = 0;
	virtual void moveToBack() = 0;

	/* Android's (EvreService.java); nullptr elsewhere */
	static SystemBridge *system();
};
