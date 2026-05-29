#include <midi/midi_port_priv.h>

#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
    #error "This file should only be compiled on Android"
#endif

#include <jni.h>
#include <string.h>

extern void mel_midi_port_push_chunk(Mel_Midi_Port* port, const Mel_Midi_Chunk* chunk);

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
