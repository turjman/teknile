// SPDX-License-Identifier: Apache-2.0
// EVRe Studio's way to a USB CDC device on Android (src/evre/usb_bridge.h, usb_bridge_jni.cpp): an app may not open
// /dev/tty*, so the device is opened through Android's USB host API. CDC only: the communication interface (class 2)
// and the data interface (class 10, a bulk endpoint each way); FTDI, CH340 and CP210x chips need their own requests.
//
// One device at a time. open() asks Android's permission when it is not granted yet; the reader thread reads the IN
// endpoint and hands each piece to the native side. Android wants transfers off its main thread: write() runs on the
// caller's (the engine's I/O thread), the reads on the reader thread.
package com.teknile.evre;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Build;

public final class EvreUsb {
    private static final String ACTION_PERMISSION = "com.teknile.evrestudio.USB_PERMISSION";
    private static final int READ_SIZE = 65536;  // API 28 and newer read more than 16 KB in one transfer
    private static final int READ_MS = 100;      // a read waits this long, so the reader sees a close soon
    private static final int WRITE_MS = 1000;
    private static final int CDC_SET_LINE_CODING = 0x20;
    private static final int CDC_SET_CONTROL_LINE_STATE = 0x22;
    private static final int CDC_DTR = 0x01;     // an STM32 CDC device sends nothing until DTR is on

    // the native side (usb_bridge_jni.cpp)
    private static native void onAsking();
    private static native void onOpened();
    private static native void onRefused();
    private static native void onFailed(String why);
    private static native void onBytes(byte[] bytes, int length);
    private static native void onGone();

    private static final Object lock = new Object();
    private static int generation = 0;           // each open() and close() a new one: an old answer is no answer
    private static String deviceName;            // the device open or being opened
    private static UsbDeviceConnection connection;
    private static UsbInterface control;
    private static UsbInterface data;
    private static UsbEndpoint in;
    private static UsbEndpoint out;
    private static Thread reader;
    private static volatile boolean reading;
    private static Context appContext;
    private static BroadcastReceiver receiver;

    private EvreUsb() {}

    // every device plugged in, a line each: "name\tvid\tpid\tproduct" (one string: one JNI call for the list)
    public static String devices(Context context) {
        UsbManager manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        StringBuilder lines = new StringBuilder();
        if (manager == null) return "";
        for (UsbDevice device : manager.getDeviceList().values()) {
            String product = null;
            try {
                product = device.getProductName(); // needs the permission on newer Android: then none
            } catch (SecurityException e) {
                product = null;
            }
            lines.append(device.getDeviceName()).append('\t').append(device.getVendorId()).append('\t')
                    .append(device.getProductId()).append('\t').append(product == null ? "" : product).append('\n');
        }
        return lines.toString();
    }

    public static void open(Context context, String name, int baud) {
        close();
        final int mine;
        synchronized (lock) {
            mine = ++generation;
            deviceName = name;
            appContext = context.getApplicationContext();
            listen();
        }
        UsbManager manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        UsbDevice device = manager == null ? null : manager.getDeviceList().get(name);
        if (device == null) {
            onFailed("not plugged in");
            return;
        }
        if (manager.hasPermission(device)) {
            openAsync(manager, device, baud, mine);
            return;
        }
        // the answer comes as a broadcast to this package only: an explicit, mutable intent (Android adds the device
        // and the answer to it; Android 14 refuses a mutable implicit one)
        Intent intent = new Intent(ACTION_PERMISSION).setPackage(appContext.getPackageName());
        intent.putExtra("baud", baud);
        intent.putExtra("generation", mine);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 31) flags |= PendingIntent.FLAG_MUTABLE;
        PendingIntent asked = PendingIntent.getBroadcast(appContext, 0, intent, flags);
        onAsking();
        manager.requestPermission(device, asked);
    }

    public static void write(byte[] bytes) {
        UsbDeviceConnection link;
        UsbEndpoint endpoint;
        synchronized (lock) {
            link = connection;
            endpoint = out;
        }
        if (link == null || endpoint == null) return; // dropped while not open, as the serial link drops it
        int sent = 0;
        while (sent < bytes.length) {
            int n = link.bulkTransfer(endpoint, bytes, sent, bytes.length - sent, WRITE_MS);
            if (n <= 0) return; // unplugged: the detach says so
            sent += n;
        }
    }

    public static void close() {
        Thread stopping;
        synchronized (lock) {
            generation++;
            deviceName = null;
            reading = false;
            stopping = reader;
            reader = null;
        }
        if (stopping != null && stopping != Thread.currentThread()) {
            try {
                stopping.join(READ_MS * 5);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
        synchronized (lock) {
            if (connection != null) {
                if (control != null) connection.releaseInterface(control);
                if (data != null) connection.releaseInterface(data);
                connection.close();
            }
            connection = null;
            control = null;
            data = null;
            in = null;
            out = null;
        }
    }

    // the permission's answer and the device unplugged; registered once, for the app's life
    private static void listen() {
        if (receiver != null) return;
        receiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                String action = intent.getAction();
                UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
                if (ACTION_PERMISSION.equals(action)) {
                    int asked = intent.getIntExtra("generation", -1);
                    synchronized (lock) {
                        if (asked != generation) return; // closed or opened again meanwhile
                    }
                    if (device == null || !intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                        onRefused();
                        return;
                    }
                    UsbManager manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
                    openAsync(manager, device, intent.getIntExtra("baud", 115200), asked);
                } else if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action) && device != null) {
                    synchronized (lock) {
                        if (!device.getDeviceName().equals(deviceName)) return;
                    }
                    // the reader ends and is joined off Android's main thread
                    new Thread(() -> {
                        close();
                        onGone();
                    }, "EvreUsbGone").start();
                }
            }
        };
        IntentFilter filter = new IntentFilter(ACTION_PERMISSION);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        if (Build.VERSION.SDK_INT >= 33) appContext.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        else appContext.registerReceiver(receiver, filter);
    }

    // the opening's transfers off Android's main thread (the permission's answer comes on it)
    private static void openAsync(UsbManager manager, UsbDevice device, int baud, int mine) {
        new Thread(() -> openNow(manager, device, baud, mine), "EvreUsbOpen").start();
    }

    private static void openNow(UsbManager manager, UsbDevice device, int baud, int mine) {
        UsbInterface comm = null;
        UsbInterface bulk = null;
        UsbEndpoint bulkIn = null;
        UsbEndpoint bulkOut = null;
        for (int i = 0; i < device.getInterfaceCount(); i++) {
            UsbInterface face = device.getInterface(i);
            if (face.getInterfaceClass() == UsbConstants.USB_CLASS_COMM && comm == null) comm = face;
            if (face.getInterfaceClass() == UsbConstants.USB_CLASS_CDC_DATA && bulk == null) {
                for (int e = 0; e < face.getEndpointCount(); e++) {
                    UsbEndpoint endpoint = face.getEndpoint(e);
                    if (endpoint.getType() != UsbConstants.USB_ENDPOINT_XFER_BULK) continue;
                    if (endpoint.getDirection() == UsbConstants.USB_DIR_IN) bulkIn = endpoint;
                    else bulkOut = endpoint;
                }
                if (bulkIn != null && bulkOut != null) bulk = face;
            }
        }
        if (bulk == null) {
            onFailed("not a USB CDC device (no data interface with a bulk endpoint each way)");
            return;
        }
        UsbDeviceConnection link = manager.openDevice(device);
        if (link == null) {
            onFailed("Android did not open the device");
            return;
        }
        if ((comm != null && !link.claimInterface(comm, true)) || !link.claimInterface(bulk, true)) {
            link.close();
            onFailed("its interfaces are held by another app");
            return;
        }
        if (comm != null) {
            byte[] coding = { (byte) baud, (byte) (baud >> 8), (byte) (baud >> 16), (byte) (baud >> 24), 0, 0, 8 }; // 8N1
            link.controlTransfer(0x21, CDC_SET_LINE_CODING, 0, comm.getId(), coding, coding.length, WRITE_MS);
            link.controlTransfer(0x21, CDC_SET_CONTROL_LINE_STATE, CDC_DTR, comm.getId(), null, 0, WRITE_MS);
        }
        synchronized (lock) {
            if (mine != generation) { // closed while it opened
                link.close();
                return;
            }
            connection = link;
            control = comm;
            data = bulk;
            in = bulkIn;
            out = bulkOut;
            reading = true;
            final UsbDeviceConnection readFrom = link;
            final UsbEndpoint endpoint = bulkIn;
            reader = new Thread(() -> read(readFrom, endpoint), "EvreUsbReader");
            reader.start();
        }
        onOpened();
    }

    // a read that times out returns -1 as an unplug does: the detach broadcast tells them apart
    private static void read(UsbDeviceConnection link, UsbEndpoint endpoint) {
        byte[] buffer = new byte[READ_SIZE];
        while (reading) {
            int n = link.bulkTransfer(endpoint, buffer, buffer.length, READ_MS);
            if (n > 0 && reading) onBytes(buffer, n);
        }
    }
}
