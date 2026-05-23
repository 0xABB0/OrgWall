#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
    #error "android-only translation unit"
#endif

#include <jni.h>

#include "../gui_internal.h"

extern bool mel_gui__android_register(JNIEnv* env, jclass cls);

static Mel_Gui_Handle unpack(jlong p) { return mel_gui_handle_unpack((u64)p); }

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGui_nativeRegister(JNIEnv* env, jclass cls)
{
    mel_gui__android_register(env, cls);
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGui_nativeFireFocus(JNIEnv* env, jclass cls, jlong h, jboolean in)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = unpack(h);
    if (in) {
        mel_gui__set_focused(handle);
        mel_gui__fire_focus_in(handle);
    } else {
        if (mel_gui_handle_eq(mel_gui_focused(), handle)) {
            mel_gui__set_focused(MEL_GUI_HANDLE_NONE);
        }
        mel_gui__fire_focus_out(handle);
    }
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGui_nativeFireResize(JNIEnv* env, jclass cls, jlong h, jint w, jint height)
{
    (void)env; (void)cls;
    mel_gui__fire_resize(unpack(h), w, height);
}
