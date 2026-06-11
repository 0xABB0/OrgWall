#include <input/android/android.h>

#include <jni.h>

JNIEXPORT void JNICALL Java_orgwall_melody_input_MelodyInput_nativeKey(JNIEnv* env, jclass cls, jint action, jint key_code, jint meta_state, jint unicode, jboolean repeat)
{
    (void)env;
    (void)cls;
    mel_input_android_handle_key((i32)action, (i32)key_code, (i32)meta_state, (i32)unicode, repeat == JNI_TRUE);
}

JNIEXPORT void JNICALL Java_orgwall_melody_input_MelodyInput_nativeMotion(JNIEnv* env, jclass cls, jint source, jint action, jint pointer_id, jfloat x, jfloat y, jfloat pressure)
{
    (void)env;
    (void)cls;
    mel_input_android_handle_motion((i32)source, (i32)action, (i32)pointer_id, (f32)x, (f32)y, (f32)pressure);
}
