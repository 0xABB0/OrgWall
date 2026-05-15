#pragma once

#include <gui/gui.h>

#include <jni.h>

bool  mel_gui_android_attach(JavaVM* vm, JNIEnv* env, jobject host);

typedef jobject (*Mel_Gui_Android_Construct)(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc);

bool     mel_gui_android_register_constructor(Mel_Atom class_atom, Mel_Gui_Android_Construct cb);

JNIEnv*  mel_gui_android_env(void);
jobject  mel_gui_android_host(void);
jclass   mel_gui_android_host_class(void);

jstring  mel_gui_android_utf16(JNIEnv* env, str8 s);
str8     mel_gui_android_str8(JNIEnv* env, jstring s, const Mel_Alloc* alloc);
void     mel_gui_android_str8_free(JNIEnv* env, str8 s, const Mel_Alloc* alloc);
