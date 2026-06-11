package orgwall.melody.hid;

import android.content.Context;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;

import java.lang.reflect.Method;
import java.util.HashMap;
import java.util.Map;

public final class MelodyHid {
    private static Context context;
    private static UsbManager manager;
    private static boolean inited;

    private static final Map<Integer, UsbDevice> devices = new HashMap<>();
    private static final Map<Integer, UsbDeviceConnection> connections = new HashMap<>();

    private MelodyHid() {}

    private static synchronized boolean ensureInited() {
        if (inited) return manager != null;
        inited = true;
        context = resolveAppContext();
        if (context == null) return false;
        manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        return manager != null;
    }

    public static synchronized int[] enumerate() {
        if (!ensureInited()) return new int[0];
        devices.clear();
        HashMap<String, UsbDevice> list = manager.getDeviceList();
        int[] ids = new int[list.size()];
        int n = 0;
        for (UsbDevice d : list.values()) {
            int id = d.getDeviceId();
            devices.put(id, d);
            ids[n++] = id;
        }
        return ids;
    }

    public static synchronized int openFd(int deviceId) {
        if (!ensureInited()) return -1;
        UsbDevice d = devices.get(deviceId);
        if (d == null) return -1;
        if (!manager.hasPermission(d)) return -1;
        UsbDeviceConnection conn = manager.openDevice(d);
        if (conn == null) return -1;
        for (int i = 0; i < d.getInterfaceCount(); i++) {
            UsbInterface iface = d.getInterface(i);
            if (iface.getInterfaceClass() == UsbConstants.USB_CLASS_HID) {
                conn.claimInterface(iface, true);
            }
        }
        connections.put(deviceId, conn);
        return conn.getFileDescriptor();
    }

    public static synchronized void closeDevice(int deviceId) {
        UsbDeviceConnection conn = connections.remove(deviceId);
        if (conn != null) conn.close();
    }

    public static synchronized int vendorId(int deviceId) {
        UsbDevice d = devices.get(deviceId);
        return d != null ? d.getVendorId() : 0;
    }

    public static synchronized int productId(int deviceId) {
        UsbDevice d = devices.get(deviceId);
        return d != null ? d.getProductId() : 0;
    }

    public static synchronized String productName(int deviceId) {
        UsbDevice d = devices.get(deviceId);
        if (d == null) return "";
        String name = d.getProductName();
        return name != null ? name : "";
    }

    private static Context resolveAppContext() {
        try {
            Class<?> at = Class.forName("android.app.ActivityThread");
            Method ca = at.getMethod("currentApplication");
            Object app = ca.invoke(null);
            if (app instanceof Context) return ((Context) app).getApplicationContext();
        } catch (ReflectiveOperationException ignored) {}
        return null;
    }
}
