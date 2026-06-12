package orgwall.melody.stt;

import android.app.Application;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.speech.RecognitionListener;
import android.speech.RecognitionSupport;
import android.speech.RecognitionSupportCallback;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Locale;
import java.util.concurrent.Executor;

public final class MelodyStt {
    private static final int DONE_OK = 0;
    private static final int DONE_ERROR = 1;
    private static final int DONE_DENIED = 2;
    private static final int DONE_AUDIO = 3;
    private static final int DONE_NETWORK = 4;
    private static final int DONE_BUSY = 5;
    private static final int DONE_UNSUPPORTED = 6;

    private static final int ON_DEVICE_UNKNOWN = 0;
    private static final int ON_DEVICE_CHECKING = 1;
    private static final int ON_DEVICE_SUPPORTED = 2;
    private static final int ON_DEVICE_UNSUPPORTED = 3;

    private static final Handler main = new Handler(Looper.getMainLooper());

    private static SpeechRecognizer recognizer;
    private static SpeechRecognizer supportProbe;
    private static volatile int onDeviceState = ON_DEVICE_UNKNOWN;

    private MelodyStt() {}

    private static native void nativeResult(long token, String text, boolean isFinal, float confidence);
    private static native void nativeDone(long token, int code);

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

    public static boolean available() {
        Context ctx = application();
        return ctx != null && SpeechRecognizer.isRecognitionAvailable(ctx);
    }

    public static String defaultLanguage() {
        return Locale.getDefault().toLanguageTag();
    }

    public static boolean biasingSupported() {
        return Build.VERSION.SDK_INT >= 33;
    }

    public static int onDeviceSupport() {
        if (Build.VERSION.SDK_INT < 33) return ON_DEVICE_UNSUPPORTED;
        synchronized (MelodyStt.class) {
            if (onDeviceState != ON_DEVICE_UNKNOWN) return onDeviceState;
            onDeviceState = ON_DEVICE_CHECKING;
        }
        main.post(new Runnable() {
            @Override public void run() { probeOnDevice(); }
        });
        return ON_DEVICE_CHECKING;
    }

    private static void probeOnDevice() {
        final Context ctx = application();
        try {
            if (ctx == null || !SpeechRecognizer.isOnDeviceRecognitionAvailable(ctx)) {
                onDeviceState = ON_DEVICE_UNSUPPORTED;
                return;
            }
            supportProbe = SpeechRecognizer.createOnDeviceSpeechRecognizer(ctx);
            Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
            intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
            intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, defaultLanguage());
            supportProbe.checkRecognitionSupport(intent, new Executor() {
                @Override public void execute(Runnable r) { main.post(r); }
            }, new RecognitionSupportCallback() {
                @Override public void onSupportResult(RecognitionSupport support) {
                    String tag = defaultLanguage();
                    boolean ok = support.getSupportedOnDeviceLanguages().contains(tag) || support.getInstalledOnDeviceLanguages().contains(tag);
                    onDeviceState = ok ? ON_DEVICE_SUPPORTED : ON_DEVICE_UNSUPPORTED;
                    probeDone();
                }
                @Override public void onError(int error) {
                    onDeviceState = ON_DEVICE_UNSUPPORTED;
                    probeDone();
                }
            });
        } catch (Throwable t) {
            onDeviceState = ON_DEVICE_UNSUPPORTED;
            probeDone();
        }
    }

    private static void probeDone() {
        if (supportProbe != null) {
            supportProbe.destroy();
            supportProbe = null;
        }
    }

    private static int mapError(int code) {
        switch (code) {
        case SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS: return DONE_DENIED;
        case SpeechRecognizer.ERROR_AUDIO: return DONE_AUDIO;
        case SpeechRecognizer.ERROR_NETWORK:
        case SpeechRecognizer.ERROR_NETWORK_TIMEOUT:
        case SpeechRecognizer.ERROR_SERVER:
        case SpeechRecognizer.ERROR_SERVER_DISCONNECTED: return DONE_NETWORK;
        case SpeechRecognizer.ERROR_RECOGNIZER_BUSY: return DONE_BUSY;
        case SpeechRecognizer.ERROR_LANGUAGE_NOT_SUPPORTED:
        case SpeechRecognizer.ERROR_LANGUAGE_UNAVAILABLE: return DONE_UNSUPPORTED;
        case SpeechRecognizer.ERROR_NO_MATCH:
        case SpeechRecognizer.ERROR_SPEECH_TIMEOUT: return DONE_OK;
        default: return DONE_ERROR;
        }
    }

    public static boolean listenStart(final long token, final String lang, final boolean partials, final boolean onDevice, final String[] biasing) {
        final Context ctx = application();
        if (ctx == null || !SpeechRecognizer.isRecognitionAvailable(ctx)) return false;
        if (onDevice && Build.VERSION.SDK_INT < 31) return false;
        main.post(new Runnable() {
            @Override public void run() {
                try {
                    if (recognizer != null) {
                        recognizer.destroy();
                        recognizer = null;
                    }
                    recognizer = onDevice ? SpeechRecognizer.createOnDeviceSpeechRecognizer(ctx) : SpeechRecognizer.createSpeechRecognizer(ctx);
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
                                nativeResult(token, texts.get(0), false, 0.0f);
                        }
                        @Override public void onResults(Bundle bundle) {
                            ArrayList<String> texts = bundle.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                            float[] conf = bundle.getFloatArray(SpeechRecognizer.CONFIDENCE_SCORES);
                            if (texts != null && !texts.isEmpty())
                                nativeResult(token, texts.get(0), true, conf != null && conf.length > 0 ? conf[0] : 0.0f);
                            nativeDone(token, DONE_OK);
                        }
                        @Override public void onError(int error) {
                            nativeDone(token, mapError(error));
                        }
                    });
                    Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
                    intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
                    intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, partials);
                    if (lang != null && lang.length() > 0)
                        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, lang);
                    if (onDevice)
                        intent.putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, true);
                    if (biasing != null && biasing.length > 0 && Build.VERSION.SDK_INT >= 33)
                        intent.putStringArrayListExtra(RecognizerIntent.EXTRA_BIASING_STRINGS, new ArrayList<String>(Arrays.asList(biasing)));
                    recognizer.startListening(intent);
                } catch (Throwable t) {
                    nativeDone(token, DONE_ERROR);
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

    public static void shutdown() {
        main.post(new Runnable() {
            @Override public void run() {
                if (recognizer != null) {
                    recognizer.cancel();
                    recognizer.destroy();
                    recognizer = null;
                }
                probeDone();
            }
        });
    }
}
