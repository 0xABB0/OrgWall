#include <music.midi/midi_port_priv.h>

#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
    #error "This file should only be compiled on Android"
#endif

#include <jni.h>
#include <string.h>

extern void mel_midi_port_push_chunk(Mel_Midi_Port* port, const Mel_Midi_Chunk* chunk);

static JavaVM* mel__midi_vm;

JavaVM* mel_midi_android_vm(void) { return mel__midi_vm; }

JNIEnv* mel_midi_android_env(void)
{
    if (mel__midi_vm == NULL) return NULL;
    JNIEnv* env = NULL;
    if ((*mel__midi_vm)->GetEnv(mel__midi_vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        (*mel__midi_vm)->AttachCurrentThread(mel__midi_vm, &env, NULL);
    }
    return env;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    (void)reserved;
    mel__midi_vm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL Java_orgwall_melody_midi_MelodyMidi_nativeReceive(
    JNIEnv* env, jclass cls, jlong native_port, jbyteArray data, jint offset, jint count, jlong timestamp_ns)
{
    (void)cls;
    Mel_Midi_Port* port = (Mel_Midi_Port*)(uintptr_t)native_port;
    if (port == NULL || data == NULL || count <= 0) return;

    jbyte raw[3];
    jsize n = count > 3 ? 3 : count;
    (*env)->GetByteArrayRegion(env, data, offset, n, raw);

    Mel_Midi_Chunk chunk = {0};
    chunk.timestamp_us = (uint64_t)timestamp_ns / 1000ULL;
    chunk.length = (int32_t)n;
    for (jsize i = 0; i < n; i++) chunk.data[i] = (uint8_t)raw[i];

    mel_midi_port_push_chunk(port, &chunk);
}
