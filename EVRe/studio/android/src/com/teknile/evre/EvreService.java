// SPDX-License-Identifier: Apache-2.0
// EVRe Studio's link and recording with the screen off (the phone's plan, A.4 and A-6): while a link is wanted, a
// foreground service of type connectedDevice with a notification ("Connected: 127.0.0.1:1210 · recording 00:12:34"),
// so Android does not stop the app in the background. The service holds nothing itself: the app's process goes on
// polling and recording while it is up. Android 14 wants the type in the manifest, its permission
// (FOREGROUND_SERVICE_CONNECTED_DEVICE) and one of its prerequisites: CHANGE_NETWORK_STATE, declared for a TCP link
// (a USB link's permission request is one too). Also the screen kept on while connected (A-5), the orientation a screen
// asks for (A-16) and the task moved to the background by a back at the root, on the activity's thread; a short buzz
// (the view's haptic feedback, which follows the phone's touch feedback setting) and the animator scale (0: Remove
// animations). Called by src/phone/phone_system.cpp.
package com.teknile.evre;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;
import android.provider.Settings;
import android.util.Log;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.view.WindowManager;

public final class EvreService extends Service {
    private static final String TAG = "EVRe";
    private static final String CHANNEL = "evre_link";
    private static final int NOTIFICATION = 1;
    private static final int ASK_NOTIFICATIONS = 4711;

    private static String title = "EVRe Studio";
    private static String text = "";
    private static volatile boolean running;
    private static boolean asked;

    // the service started with its notification, or the notification's text changed while it runs
    public static void hold(Context context, String newTitle, String newText) {
        title = newTitle;
        text = newText;
        Context app = context.getApplicationContext();
        if (running) {
            NotificationManager manager = (NotificationManager) app.getSystemService(Context.NOTIFICATION_SERVICE);
            if (manager != null) manager.notify(NOTIFICATION, build(app));
            return;
        }
        askNotifications(context);
        Intent intent = new Intent(app, EvreService.class);
        try {
            if (Build.VERSION.SDK_INT >= 26) app.startForegroundService(intent);
            else app.startService(intent);
        } catch (RuntimeException e) {
            Log.w(TAG, "the link's service did not start: " + e);
        }
    }

    // the service and its notification gone
    public static void release(Context context) {
        Context app = context.getApplicationContext();
        app.stopService(new Intent(app, EvreService.class));
    }

    // the screen kept on (or let go) while the activity shows
    public static void keepScreenOn(Context context, final boolean on) {
        if (!(context instanceof Activity)) return;
        final Activity activity = (Activity) context;
        activity.runOnUiThread(() -> {
            if (on) activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            else activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        });
    }

    // ActivityInfo's SCREEN_ORIENTATION_* (sensorLandscape, sensorPortrait, unspecified)
    public static void orientation(Context context, final int orientation) {
        if (!(context instanceof Activity)) return;
        final Activity activity = (Activity) context;
        activity.runOnUiThread(() -> activity.setRequestedOrientation(orientation));
    }

    // Android's back with nothing open while a link is wanted or a recording runs: the task to the background as Home
    // does, not finished (finishing the last activity ended the process: the link, the service and the recording)
    public static void toBack(Context context) {
        if (!(context instanceof Activity)) return;
        final Activity activity = (Activity) context;
        activity.runOnUiThread(() -> activity.moveTaskToBack(true));
    }

    // a short buzz: a write the device took, a link up. The view's own feedback: none when the phone's touch feedback
    // is off, and no VIBRATE permission
    public static void haptic(Context context) {
        if (!(context instanceof Activity)) return;
        final Activity activity = (Activity) context;
        activity.runOnUiThread(() -> {
            View view = activity.getWindow().getDecorView();
            view.performHapticFeedback(Build.VERSION.SDK_INT >= 30 ? HapticFeedbackConstants.CONFIRM
                                                                   : HapticFeedbackConstants.VIRTUAL_KEY);
        });
    }

    // the animator duration scale: 0 when the phone's animations are removed (Accessibility, or Developer options)
    public static float animationScale(Context context) {
        return Settings.Global.getFloat(context.getContentResolver(), Settings.Global.ANIMATOR_DURATION_SCALE, 1f);
    }

    // Android 13 and newer show a service's notification only with this permission: asked once, at the first link
    private static void askNotifications(Context context) {
        if (asked || Build.VERSION.SDK_INT < 33 || !(context instanceof Activity)) return;
        asked = true;
        Activity activity = (Activity) context;
        if (activity.checkSelfPermission("android.permission.POST_NOTIFICATIONS") != PackageManager.PERMISSION_GRANTED)
            activity.requestPermissions(new String[] { "android.permission.POST_NOTIFICATIONS" }, ASK_NOTIFICATIONS);
    }

    private static Notification build(Context context) {
        Notification.Builder builder = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(context, CHANNEL)
                                                                   : new Notification.Builder(context);
        Intent open = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());
        int icon = context.getApplicationInfo().icon;
        builder.setContentTitle(title)
                .setContentText(text)
                .setSmallIcon(icon != 0 ? icon : android.R.drawable.stat_notify_sync)
                .setOngoing(true)
                .setOnlyAlertOnce(true);
        if (open != null)
            builder.setContentIntent(PendingIntent.getActivity(context, 0, open, PendingIntent.FLAG_IMMUTABLE));
        return builder.build();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            if (manager != null)
                manager.createNotificationChannel(new NotificationChannel(CHANNEL, "EVRe Studio",
                        NotificationManager.IMPORTANCE_LOW));
        }
        try {
            if (Build.VERSION.SDK_INT >= 29)
                startForeground(NOTIFICATION, build(this), ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
            else
                startForeground(NOTIFICATION, build(this));
            running = true;
        } catch (RuntimeException e) {
            Log.w(TAG, "the link's service could not come to the foreground: " + e);
            stopSelf();
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        running = false;
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
