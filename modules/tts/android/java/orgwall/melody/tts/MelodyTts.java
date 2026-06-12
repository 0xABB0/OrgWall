package orgwall.melody.tts;

import android.app.Application;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.speech.tts.Voice;

import java.io.File;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Locale;

public final class MelodyTts {
    private static final int DONE_OK = 0;
    private static final int DONE_ABORTED = 1;
    private static final int DONE_ERROR = 2;

    private static final float RATE_MIN = 0.1f;
    private static final float RATE_MAX = 4.0f;

    private static final Handler main = new Handler(Looper.getMainLooper());

    private static volatile TextToSpeech tts;
    private static volatile boolean ready;
    private static final ArrayList<Voice> voices = new ArrayList<Voice>();
    private static final HashMap<Long, String> rangeTexts = new HashMap<Long, String>();
    private static final HashMap<Long, String> renderPaths = new HashMap<Long, String>();

    private MelodyTts() {}

    private static native void nativeReady(boolean ok);
    private static native void nativeDone(long token, int code);
    private static native void nativeRange(long token, int byteOffset, int byteLength);
    private static native void nativeRenderDone(long token, String path);

    private static Application application() {
        try {
            Class<?> at = Class.forName("android.app.ActivityThread");
            Method cur = at.getMethod("currentApplication");
            Object app = cur.invoke(null);
            return app instanceof Application ? (Application) app : null;
        } catch (Throwable t) {
            return null;
        }
    }

    private static long parseToken(String id) {
        try {
            return Long.parseLong(id);
        } catch (Throwable t) {
            return 0;
        }
    }

    private static void discard(long token) {
        synchronized (rangeTexts) { rangeTexts.remove(token); }
        String path;
        synchronized (renderPaths) { path = renderPaths.remove(token); }
        if (path != null) new File(path).delete();
    }

    private static void finishError(long token) {
        discard(token);
        nativeDone(token, DONE_ERROR);
    }

    private static float clampRate(float rate) {
        if (rate <= 0) return 1.0f;
        return Math.min(Math.max(rate, RATE_MIN), RATE_MAX);
    }

    private static void applyVoice(String voiceName) {
        if (voiceName == null || voiceName.length() == 0) return;
        for (Voice v : tts.getVoices()) {
            if (voiceName.equals(v.getName())) {
                tts.setVoice(v);
                return;
            }
        }
    }

    public static void ensure() {
        if (tts != null) return;
        final Context ctx = application();
        if (ctx == null) return;
        main.post(new Runnable() {
            @Override public void run() {
                if (tts != null) return;
                tts = new TextToSpeech(ctx, new TextToSpeech.OnInitListener() {
                    @Override public void onInit(int status) {
                        ready = status == TextToSpeech.SUCCESS;
                        nativeReady(ready);
                    }
                });
                tts.setOnUtteranceProgressListener(new UtteranceProgressListener() {
                    @Override public void onStart(String id) {}
                    @Override public void onDone(String id) {
                        long token = parseToken(id);
                        synchronized (rangeTexts) { rangeTexts.remove(token); }
                        String path;
                        synchronized (renderPaths) { path = renderPaths.remove(token); }
                        if (path != null) nativeRenderDone(token, path);
                        else nativeDone(token, DONE_OK);
                    }
                    @Override public void onError(String id) {
                        finishError(parseToken(id));
                    }
                    @Override public void onError(String id, int errorCode) {
                        finishError(parseToken(id));
                    }
                    @Override public void onStop(String id, boolean interrupted) {
                        long token = parseToken(id);
                        discard(token);
                        nativeDone(token, DONE_ABORTED);
                    }
                    @Override public void onRangeStart(String id, int start, int end, int frame) {
                        long token = parseToken(id);
                        String text;
                        synchronized (rangeTexts) { text = rangeTexts.get(token); }
                        if (text == null || start < 0 || end > text.length() || start >= end) return;
                        int off = text.substring(0, start).getBytes(StandardCharsets.UTF_8).length;
                        int len = text.substring(start, end).getBytes(StandardCharsets.UTF_8).length;
                        nativeRange(token, off, len);
                    }
                });
            }
        });
    }

    public static boolean ready() {
        return ready && tts != null;
    }

    public static boolean rangesSupported() {
        return Build.VERSION.SDK_INT >= 26;
    }

    public static int voicesRefresh() {
        voices.clear();
        if (!ready()) return 0;
        try {
            for (Voice v : tts.getVoices()) {
                if (v.isNetworkConnectionRequired()) continue;
                voices.add(v);
            }
        } catch (Throwable t) {
            voices.clear();
        }
        return voices.size();
    }

    public static String voiceName(int idx) {
        return idx >= 0 && idx < voices.size() ? voices.get(idx).getName() : "";
    }

    public static String voiceLang(int idx) {
        if (idx < 0 || idx >= voices.size()) return "";
        Locale l = voices.get(idx).getLocale();
        return l != null ? l.toLanguageTag() : "";
    }

    public static Voice voiceFind(String name) {
        for (Voice v : voices)
            if (v.getName().equals(name)) return v;
        return null;
    }

    public static boolean speak(byte[] textUtf8, final String voiceName, float rate, float pitch, float volume, boolean wantRanges, final long token) {
        if (!ready()) return false;
        final String text = new String(textUtf8, StandardCharsets.UTF_8);
        if (wantRanges) synchronized (rangeTexts) { rangeTexts.put(token, text); }
        final float r = clampRate(rate);
        final float p = pitch > 0 ? pitch : 1.0f;
        final float vol = volume > 0 ? Math.min(volume, 1.0f) : 1.0f;
        main.post(new Runnable() {
            @Override public void run() {
                try {
                    applyVoice(voiceName);
                    tts.setSpeechRate(r);
                    tts.setPitch(p);
                    Bundle params = new Bundle();
                    params.putFloat(TextToSpeech.Engine.KEY_PARAM_VOLUME, vol);
                    int rc = tts.speak(text, TextToSpeech.QUEUE_ADD, params, String.valueOf(token));
                    if (rc != TextToSpeech.SUCCESS) finishError(token);
                } catch (Throwable t) {
                    finishError(token);
                }
            }
        });
        return true;
    }

    public static boolean render(byte[] textUtf8, final String voiceName, float rate, float pitch, final long token) {
        if (!ready()) return false;
        Context ctx = application();
        if (ctx == null) return false;
        final File out = new File(ctx.getCacheDir(), "mel_tts_render_" + token + ".wav");
        final String text = new String(textUtf8, StandardCharsets.UTF_8);
        synchronized (renderPaths) { renderPaths.put(token, out.getAbsolutePath()); }
        final float r = clampRate(rate);
        final float p = pitch > 0 ? pitch : 1.0f;
        main.post(new Runnable() {
            @Override public void run() {
                try {
                    applyVoice(voiceName);
                    tts.setSpeechRate(r);
                    tts.setPitch(p);
                    int rc = tts.synthesizeToFile(text, new Bundle(), out, String.valueOf(token));
                    if (rc != TextToSpeech.SUCCESS) finishError(token);
                } catch (Throwable t) {
                    finishError(token);
                }
            }
        });
        return true;
    }

    public static void stop() {
        main.post(new Runnable() {
            @Override public void run() {
                synchronized (rangeTexts) { rangeTexts.clear(); }
                ArrayList<String> paths = new ArrayList<String>();
                synchronized (renderPaths) {
                    paths.addAll(renderPaths.values());
                    renderPaths.clear();
                }
                for (String p : paths) new File(p).delete();
                if (tts != null) tts.stop();
            }
        });
    }

    public static void shutdown() {
        main.post(new Runnable() {
            @Override public void run() {
                synchronized (rangeTexts) { rangeTexts.clear(); }
                ArrayList<String> paths = new ArrayList<String>();
                synchronized (renderPaths) {
                    paths.addAll(renderPaths.values());
                    renderPaths.clear();
                }
                for (String p : paths) new File(p).delete();
                voices.clear();
                ready = false;
                if (tts != null) {
                    tts.stop();
                    tts.shutdown();
                    tts = null;
                }
            }
        });
    }
}
