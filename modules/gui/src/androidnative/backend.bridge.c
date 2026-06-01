#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include <jni.h>
#include <stdint.h>

#include "android.h"

static Mel_Gui_Handle unpack(jlong p) { return mel_gui_handle_unpack((u64)p); }

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeRegister(JNIEnv* env, jclass cls, jfloat density)
{
    mel_gui__android_set_density((float)density);
    mel_gui__android_register(env, cls);
}

JNIEXPORT jboolean JNICALL Java_orgwall_melody_platform_MelGui_nativeOsBack(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    return mel_gui__nav_os_back() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeFocus(JNIEnv* env, jclass cls, jlong h, jboolean in, jlong fnIn, jlong fnOut)
{
    (void)env;
    (void)cls;
    Mel_Gui_Handle handle = unpack(h);
    if (in)
    {
        mel_gui__set_focused(handle);
        if (fnIn)
            ((Mel_Cb_Void)(intptr_t)fnIn)(handle, mel_gui_user(handle));
    }
    else
    {
        if (mel_gui_handle_eq(mel_gui_focused(), handle))
        {
            mel_gui__set_focused(MEL_GUI_HANDLE_NONE);
        }
        if (fnOut)
            ((Mel_Cb_Void)(intptr_t)fnOut)(handle, mel_gui_user(handle));
    }
}
