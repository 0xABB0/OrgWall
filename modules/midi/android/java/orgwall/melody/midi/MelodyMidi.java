package orgwall.melody.midi;

import android.content.Context;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.os.Handler;
import android.os.HandlerThread;

import java.io.IOException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

public final class MelodyMidi {
    private static Context context;
    private static MidiManager manager;
    private static Handler handler;
    private static boolean inited;

    private MelodyMidi() {}

    public static synchronized boolean ensureInited() {
        if (inited) return manager != null;
        inited = true;

        context = resolveAppContext();
        if (context == null) return false;

        manager = (MidiManager) context.getSystemService(Context.MIDI_SERVICE);
        if (manager == null) return false;

        HandlerThread t = new HandlerThread("MelodyMidi");
        t.start();
        handler = new Handler(t.getLooper());
        return true;
    }

    public static String[] enumerateInputs() {
        if (!ensureInited()) return new String[0];
        List<String> names = new ArrayList<>();
        for (MidiDeviceInfo info : manager.getDevices()) {
            if (info.getOutputPortCount() > 0) names.add(nameOf(info));
        }
        return names.toArray(new String[0]);
    }

    public static Handle openInput(int id, long nativePort) {
        if (!ensureInited()) return null;

        List<MidiDeviceInfo> filtered = new ArrayList<>();
        for (MidiDeviceInfo info : manager.getDevices()) {
            if (info.getOutputPortCount() > 0) filtered.add(info);
        }
        if (id < 0 || id >= filtered.size()) return null;

        MidiDeviceInfo info = filtered.get(id);
        final CountDownLatch latch = new CountDownLatch(1);
        final AtomicReference<MidiDevice> ref = new AtomicReference<>();

        manager.openDevice(info, new MidiManager.OnDeviceOpenedListener() {
            @Override
            public void onDeviceOpened(MidiDevice device) {
                ref.set(device);
                latch.countDown();
            }
        }, handler);

        try {
            if (!latch.await(2, TimeUnit.SECONDS)) return null;
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
            return null;
        }

        MidiDevice device = ref.get();
        if (device == null) return null;

        MidiOutputPort port = device.openOutputPort(0);
        if (port == null) {
            try { device.close(); } catch (IOException ignored) {}
            return null;
        }

        NativeReceiver receiver = new NativeReceiver(nativePort);
        port.connect(receiver);

        Handle h = new Handle();
        h.device = device;
        h.port = port;
        h.receiver = receiver;
        h.name = nameOf(info);
        return h;
    }

    public static void close(Handle h) {
        if (h == null) return;
        try {
            if (h.port != null) {
                if (h.receiver != null) h.port.disconnect(h.receiver);
                h.port.close();
            }
        } catch (IOException ignored) {}
        try {
            if (h.device != null) h.device.close();
        } catch (IOException ignored) {}
    }

    public static String handleName(Handle h) {
        return h == null ? null : h.name;
    }

    private static String nameOf(MidiDeviceInfo info) {
        String name = info.getProperties().getString(MidiDeviceInfo.PROPERTY_NAME);
        if (name == null || name.isEmpty()) {
            name = info.getProperties().getString(MidiDeviceInfo.PROPERTY_PRODUCT);
        }
        if (name == null || name.isEmpty()) name = "MIDI Device " + info.getId();
        return name;
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

    public static final class Handle {
        public MidiDevice device;
        public MidiOutputPort port;
        public NativeReceiver receiver;
        public String name;
    }

    public static final class NativeReceiver extends MidiReceiver {
        private final long nativePort;
        public NativeReceiver(long nativePort) { this.nativePort = nativePort; }

        @Override
        public void onSend(byte[] msg, int offset, int count, long timestamp) {
            nativeReceive(nativePort, msg, offset, count, timestamp);
        }
    }

    private static native void nativeReceive(long nativePort, byte[] msg, int offset, int count, long timestamp);
}
