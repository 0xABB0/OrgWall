#include "android.h"

void mel_gui__backend_slider_create(Mel_Gui_Widget* w, str8 text)
{
    (void)text;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;

    Mel_Gui_Slider_Impl* impl = (Mel_Gui_Slider_Impl*)w->impl;
    i32 mn  = impl ? impl->min_value : 0;
    i32 mx  = impl ? impl->max_value : 100;
    i32 val = impl ? impl->value     : 0;

    Mel_Gui_Android* a = mel_gui__android();
    (*env)->CallStaticVoidMethod(env, a->cls, a->createSlider,
        mel_gui__android_pack(w->self),
        mel_gui__android_pack(w->parent),
        w->x, w->y, w->width, w->height,
        mn, mx, val);

    w->native = (void*)(uintptr_t)1;
}
