#include <music.midi/midi_port_priv.h>

#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
    #error "This file should only be compiled on Android"
#endif

#include <jni.h>

#include <stdlib.h>
#include <string.h>

#include <platform/android/jni.h>

#define MEL_MIDI_MAX_DEVICES 64

static char mel__midi_name_pool[MEL_MIDI_MAX_DEVICES][128];

static jclass    mel__midi_class;
static jclass    mel__midi_handle_class;
static jmethodID mel__midi_enumerate;
static jmethodID mel__midi_open;
static jmethodID mel__midi_close;
static jfieldID  mel__midi_handle_name;
static bool      mel__midi_inited;
static bool      mel__midi_init_failed;

static bool mel__midi_resolve(JNIEnv* env)
{
    if (mel__midi_inited) return mel__midi_class != NULL;
    if (mel__midi_init_failed) return false;
    mel__midi_inited = true;

    jclass local = (*env)->FindClass(env, "orgwall/melody/midi/MelodyMidi");
    if (local == NULL) { (*env)->ExceptionClear(env); mel__midi_init_failed = true; return false; }
    mel__midi_class = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);

    jclass handle_local = (*env)->FindClass(env, "orgwall/melody/midi/MelodyMidi$Handle");
    if (handle_local == NULL) { (*env)->ExceptionClear(env); mel__midi_init_failed = true; return false; }
    mel__midi_handle_class = (*env)->NewGlobalRef(env, handle_local);
    (*env)->DeleteLocalRef(env, handle_local);

    mel__midi_enumerate = (*env)->GetStaticMethodID(env, mel__midi_class, "enumerateInputs", "()[Ljava/lang/String;");
    mel__midi_open      = (*env)->GetStaticMethodID(env, mel__midi_class, "openInput",       "(IJ)Lorgwall/melody/midi/MelodyMidi$Handle;");
    mel__midi_close     = (*env)->GetStaticMethodID(env, mel__midi_class, "close",           "(Lorgwall/melody/midi/MelodyMidi$Handle;)V");
    mel__midi_handle_name = (*env)->GetFieldID(env, mel__midi_handle_class, "name", "Ljava/lang/String;");

    if (mel__midi_enumerate == NULL || mel__midi_open == NULL || mel__midi_close == NULL || mel__midi_handle_name == NULL) {
        (*env)->ExceptionClear(env);
        mel__midi_init_failed = true;
        return false;
    }
    return true;
}

static void mel__copy_jstring_utf8(JNIEnv* env, jstring jstr, char* out, size_t out_size)
{
    if (jstr == NULL || out_size == 0) { if (out_size) out[0] = 0; return; }
    const char* chars = (*env)->GetStringUTFChars(env, jstr, NULL);
    if (chars == NULL) { out[0] = 0; return; }
    strncpy(out, chars, out_size - 1);
    out[out_size - 1] = 0;
    (*env)->ReleaseStringUTFChars(env, jstr, chars);
}

int32_t mel_midi_port_platform_enumerate(Mel_Midi_Port_Info* out_infos, int32_t max_count)
{
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || !mel__midi_resolve(env)) return 0;

    jobjectArray names = (*env)->CallStaticObjectMethod(env, mel__midi_class, mel__midi_enumerate);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
    if (names == NULL) return 0;

    jsize total = (*env)->GetArrayLength(env, names);
    int32_t cap = max_count < MEL_MIDI_MAX_DEVICES ? max_count : MEL_MIDI_MAX_DEVICES;
    if (total > cap) total = cap;

    for (jsize i = 0; i < total; i++) {
        jstring jname = (jstring)(*env)->GetObjectArrayElement(env, names, i);
        if (out_infos) {
            mel__copy_jstring_utf8(env, jname, mel__midi_name_pool[i], sizeof(mel__midi_name_pool[i]));
            out_infos[i].id       = (int32_t)i;
            out_infos[i].name     = mel__midi_name_pool[i];
            out_infos[i].is_input = true;
        }
        (*env)->DeleteLocalRef(env, jname);
    }

    (*env)->DeleteLocalRef(env, names);
    return (int32_t)total;
}

Mel_Midi_Port* mel_midi_port_platform_open_input(int32_t id, Mel_Midi_Port* port)
{
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || !mel__midi_resolve(env)) return NULL;

    jobject handle_local = (*env)->CallStaticObjectMethod(
        env, mel__midi_class, mel__midi_open, (jint)id, (jlong)(uintptr_t)port);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return NULL; }
    if (handle_local == NULL) return NULL;

    jobject handle_global = (*env)->NewGlobalRef(env, handle_local);

    jstring jname = (*env)->GetObjectField(env, handle_local, mel__midi_handle_name);
    if (jname != NULL) {
        const char* chars = (*env)->GetStringUTFChars(env, jname, NULL);
        if (chars != NULL) {
            port->name = strdup(chars);
            (*env)->ReleaseStringUTFChars(env, jname, chars);
        }
        (*env)->DeleteLocalRef(env, jname);
    }

    (*env)->DeleteLocalRef(env, handle_local);
    port->platform_handle = handle_global;
    return port;
}

void mel_midi_port_platform_close(Mel_Midi_Port* port)
{
    if (port == NULL || port->platform_handle == NULL) return;
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL) return;

    jobject handle = (jobject)port->platform_handle;
    (*env)->CallStaticVoidMethod(env, mel__midi_class, mel__midi_close, handle);
    (*env)->ExceptionClear(env);
    (*env)->DeleteGlobalRef(env, handle);
    port->platform_handle = NULL;
}
