#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
    #error "android-only translation unit"
#endif

#include <jni.h>
#include <string.h>

#include "../gui_internal.h"

extern bool mel_gui__android_register(JNIEnv* env, jclass cls);

static Mel_Gui_Handle unpack(jlong p) { return mel_gui_handle_unpack((u64)p); }

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGui_nativeRegister(JNIEnv* env, jclass cls)
{
    mel_gui__android_register(env, cls);
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGui_nativeFireClick(JNIEnv* env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    mel_gui__fire_click(unpack(h));
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
Java_orgwall_melody_platform_MelGui_nativeFireToggled(JNIEnv* env, jclass cls, jlong h, jboolean checked)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = unpack(h);
    Mel_Gui_Widget* w = mel_gui__get(handle);
    if (!w) return;
    Mel_Gui_CheckBox_Impl* impl = (Mel_Gui_CheckBox_Impl*)w->impl;
    if (impl && impl->on_.on_toggled) {
        impl->on_.on_toggled(handle, (bool)checked, w->user);
    }
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGui_nativeFireSliderChanged(JNIEnv* env, jclass cls, jlong h, jint value)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = unpack(h);
    Mel_Gui_Widget* w = mel_gui__get(handle);
    if (!w) return;
    Mel_Gui_Slider_Impl* impl = (Mel_Gui_Slider_Impl*)w->impl;
    if (impl) impl->value = value;
    if (impl && impl->on_.on_value_changed) {
        impl->on_.on_value_changed(handle, value, w->user);
    }
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGui_nativeFireTextChanged(JNIEnv* env, jclass cls, jlong h, jstring text)
{
    (void)cls;
    Mel_Gui_Handle handle = unpack(h);
    Mel_Gui_Widget* w = mel_gui__get(handle);
    if (!w) return;
    Mel_Gui_TextField_Impl* impl = (Mel_Gui_TextField_Impl*)w->impl;
    if (!impl || !impl->on_.on_text_changed) return;

    const char* c = text ? (*env)->GetStringUTFChars(env, text, NULL) : NULL;
    str8 s = { (u8*)c, c ? (size)strlen(c) : 0 };
    impl->on_.on_text_changed(handle, s, w->user);
    if (c) (*env)->ReleaseStringUTFChars(env, text, c);
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGui_nativeFirePointer(JNIEnv* env, jclass cls, jlong h, jint kind, jint x, jint y)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = unpack(h);
    switch (kind) {
        case 0: mel_gui__fire_pointer_down(handle, x, y); break;
        case 1: mel_gui__fire_pointer_move(handle, x, y); break;
        case 2: mel_gui__fire_pointer_up  (handle, x, y); break;
        default: break;
    }
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGui_nativeFireKey(JNIEnv* env, jclass cls, jlong h, jint key, jboolean down)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = unpack(h);
    if (down) mel_gui__fire_key_down(handle, (Mel_Key)key);
    else      mel_gui__fire_key_up  (handle, (Mel_Key)key);
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGui_nativeFireCanvasPaint(JNIEnv* env, jclass cls, jlong h, jobject canvas, jint w, jint height)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = unpack(h);
    Mel_Gui_Widget* widget = mel_gui__get(handle);
    if (!widget) return;
    Mel_Gui_Canvas_Impl* impl = (Mel_Gui_Canvas_Impl*)widget->impl;
    if (impl && impl->on_.on_paint) {
        impl->on_.on_paint(handle, (void*)canvas, w, height, widget->user);
    }
}
