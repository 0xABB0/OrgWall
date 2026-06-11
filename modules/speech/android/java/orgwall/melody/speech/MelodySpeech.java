package orgwall.melody.speech;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.speech.tts.Voice;

import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Locale;

public final class MelodySpeech {
    private static final String PERMISSION = "android.permission.RECORD_AUDIO";
    private static final int REQUEST_CODE = 0x5350;

    private static final int DONE_OK = 0;
    private static final int DONE_ABORTED = 1;
    private static final int DONE_ERROR = 2;
    private static final int DONE_DENIED = 3;
    private static final int DONE_AUDIO = 4;
    private static final int DONE_NETWORK = 5;

    private static volatile Activity current;
    private static boolean lifecycleHooked;

    private static final Handler main = new Handler(Looper.getMainLooper());

    private static TextToSpeech tts;
    private static volatile boolean ttsReady;
    private static final ArrayList<Voice> voices = new ArrayList<Voice>();
    private static final HashMap<Long, String> utteranceTexts = new HashMap<Long, String>();

    private static SpeechRecognizer recognizer;
    private static volatile long listenToken;

    private MelodySpeech() {}

    private static native void nativeTtsDone(long token, int code);
    private static native void nativeTtsRange(long token, int byteOffset, int byteLength);
    private static native void nativeSttResult(long token, String text, boolean isFinal, float confidence);
    private static native void nativeSttDone(long token, int code);

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

    private static void hookLifecycle(Application app) {
        if (lifecycleHooked || app == null) return;
        lifecycleHooked = true;
        app.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks() {
            @Override public void onActivityResumed(Activity a) { current = a; }
            @Override public void onActivityPaused(Activity a) { if (current == a) current = null; }
            @Override public void onActivityCreated(Activity a, Bundle b) {}
            @Override public void onActivityStarted(Activity a) {}
            @Override public void onActivityStopped(Activity a) {}
            @Override public void onActivitySaveInstanceState(Activity a, Bundle b) {}
            @Override public void onActivityDestroyed(Activity a) { if (current == a) current = null; }
        });
    }

    private static Activity resolveActivity() {
        Application app = application();
        hookLifecycle(app);
        if (current != null) return current;
        try {
            Class<?> gui = Class.forName("orgwall.melody.platform.MelGui");
            Method m = gui.getMethod("activity");
            Object a = m.invoke(null);
            if (a instanceof Activity) return (Activity) a;
        } catch (Throwable t) {
        }
        return null;
    }

    public static boolean micGranted(Context ctx) {
        if (ctx == null) ctx = application();
        return ctx != null && ctx.checkSelfPermission(PERMISSION) == PackageManager.PERMISSION_GRANTED;
    }

    public static boolean micGranted() {
        return micGranted(application());
    }

    public static boolean requestMic() {
        final Activity activity = resolveActivity();
        if (activity == null) return false;
        main.post(new Runnable() {
            @Override public void run() {
                activity.requestPermissions(new String[] { PERMISSION }, REQUEST_CODE);
            }
        });
        return true;
    }

    public static void ttsEnsure() {
        if (tts != null) return;
        final Context ctx = application();
        if (ctx == null) return;
        main.post(new Runnable() {
            @Override public void run() {
                if (tts != null) return;
                tts = new TextToSpeech(ctx, new TextToSpeech.OnInitListener() {
                    @Override public void onInit(int status) {
                        ttsReady = status == TextToSpeech.SUCCESS;
                    }
                });
                tts.setOnUtteranceProgressListener(new UtteranceProgressListener() {
                    @Override public void onStart(String id) {}
                    @Override public void onDone(String id) {
                        long token = parseToken(id);
                        utteranceTexts.remove(token);
                        nativeTtsDone(token, DONE_OK);
                    }
                    @Override public void onError(String id) {
                        long token = parseToken(id);
                        utteranceTexts.remove(token);
                        nativeTtsDone(token, DONE_ERROR);
                    }
                    @Override public void onStop(String id, boolean interrupted) {
                        long token = parseToken(id);
                        utteranceTexts.remove(token);
                        nativeTtsDone(token, DONE_ABORTED);
                    }
                    @Override public void onRangeStart(String id, int start, int end, int frame) {
                        long token = parseToken(id);
                        String text = utteranceTexts.get(token);
                        if (text == null || start < 0 || end > text.length() || start >= end) return;
                        int off = text.substring(0, start).getBytes(StandardCharsets.UTF_8).length;
                        int len = text.substring(start, end).getBytes(StandardCharsets.UTF_8).length;
                        nativeTtsRange(token, off, len);
                    }
                });
            }
        });
    }

    private static long parseToken(String id) {
        try {
            return Long.parseLong(id);
        } catch (Throwable t) {
            return 0;
        }
    }

    public static boolean ttsReady() {
        return ttsReady;
    }

    public static int voicesRefresh() {
        voices.clear();
        if (tts == null || !ttsReady) return 0;
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

    public static boolean speak(final String text, final String voiceName, final float rate, final float pitch, final float volume, final long token) {
        if (tts == null || !ttsReady) return false;
        utteranceTexts.put(token, text);
        main.post(new Runnable() {
            @Override public void run() {
                try {
                    if (voiceName != null && voiceName.length() > 0) {
                        for (Voice v : tts.getVoices()) {
                            if (voiceName.equals(v.getName())) {
                                tts.setVoice(v);
                                break;
                            }
                        }
                    }
                    tts.setSpeechRate(rate > 0 ? rate : 1.0f);
                    tts.setPitch(pitch > 0 ? pitch : 1.0f);
                    Bundle params = new Bundle();
                    params.putFloat(TextToSpeech.Engine.KEY_PARAM_VOLUME, volume > 0 ? volume : 1.0f);
                    int rc = tts.speak(text, TextToSpeech.QUEUE_ADD, params, String.valueOf(token));
                    if (rc != TextToSpeech.SUCCESS) {
                        utteranceTexts.remove(token);
                        nativeTtsDone(token, DONE_ERROR);
                    }
                } catch (Throwable t) {
                    utteranceTexts.remove(token);
                    nativeTtsDone(token, DONE_ERROR);
                }
            }
        });
        return true;
    }

    public static void ttsStop() {
        main.post(new Runnable() {
            @Override public void run() {
                if (tts != null) tts.stop();
            }
        });
    }

    public static boolean sttAvailable() {
        Context ctx = application();
        return ctx != null && SpeechRecognizer.isRecognitionAvailable(ctx);
    }

    private static int mapSttError(int code) {
        switch (code) {
        case SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS: return DONE_DENIED;
        case SpeechRecognizer.ERROR_AUDIO: return DONE_AUDIO;
        case SpeechRecognizer.ERROR_NETWORK:
        case SpeechRecognizer.ERROR_NETWORK_TIMEOUT: return DONE_NETWORK;
        case SpeechRecognizer.ERROR_NO_MATCH:
        case SpeechRecognizer.ERROR_SPEECH_TIMEOUT: return DONE_OK;
        default: return DONE_ERROR;
        }
    }

    public static boolean listenStart(final long token, final String lang, final boolean partials) {
        final Context ctx = application();
        if (ctx == null || !SpeechRecognizer.isRecognitionAvailable(ctx)) return false;
        listenToken = token;
        main.post(new Runnable() {
            @Override public void run() {
                try {
                    if (recognizer != null) {
                        recognizer.destroy();
                        recognizer = null;
                    }
                    recognizer = SpeechRecognizer.createSpeechRecognizer(ctx);
                    recognizer.setRecognitionListener(new RecognitionListener() {
                        @Override public void onReadyForSpeech(Bundle params) {}
                        @Override public void onBeginningOfSpeech() {}
                        @Override public void onRmsChanged(float rmsdB) {}
                        @Override public void onBufferReceived(byte[] buffer) {}
                        @Override public void onEndOfSpeech() {}
                        @Override public void onEvent(int eventType, Bundle params) {}
                        @Override public void onPartialResults(Bundle bundle) {
                            ArrayList<String> texts = bundle.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                            if (texts != null && !texts.isEmpty() && texts.get(0).length() > 0)
                                nativeSttResult(token, texts.get(0), false, 0.0f);
                        }
                        @Override public void onResults(Bundle bundle) {
                            ArrayList<String> texts = bundle.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                            float[] conf = bundle.getFloatArray(SpeechRecognizer.CONFIDENCE_SCORES);
                            if (texts != null && !texts.isEmpty())
                                nativeSttResult(token, texts.get(0), true, conf != null && conf.length > 0 ? conf[0] : 0.0f);
                            nativeSttDone(token, DONE_OK);
                        }
                        @Override public void onError(int error) {
                            nativeSttDone(token, mapSttError(error));
                        }
                    });
                    Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
                    intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
                    intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, partials);
                    if (lang != null && lang.length() > 0)
                        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, lang);
                    recognizer.startListening(intent);
                } catch (Throwable t) {
                    nativeSttDone(token, DONE_ERROR);
                }
            }
        });
        return true;
    }

    public static void listenStop() {
        main.post(new Runnable() {
            @Override public void run() {
                if (recognizer != null) recognizer.stopListening();
            }
        });
    }

    public static void listenCancel() {
        main.post(new Runnable() {
            @Override public void run() {
                if (recognizer != null) {
                    recognizer.cancel();
                    recognizer.destroy();
                    recognizer = null;
                }
            }
        });
    }

    public static String defaultLanguage() {
        return Locale.getDefault().toLanguageTag();
    }
}
