#pragma once

#include <gui/gui.h>

#include <jni.h>

bool mel_gui_android_attach(JavaVM* vm, JNIEnv* env, jobject host);
void mel_gui_android_dispatch_click(Mel_Gui_Handle h);
void mel_gui_android_dispatch_value_changed(Mel_Gui_Handle h, i32 value);
void mel_gui_android_dispatch_text_changed(Mel_Gui_Handle h, str8 text);
