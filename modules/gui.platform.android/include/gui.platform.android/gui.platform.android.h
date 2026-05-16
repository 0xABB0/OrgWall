#pragma once

#include <gui/gui.h>

#include <jni.h>

bool  mel_gui_android_attach(JavaVM* vm, JNIEnv* env, jobject host);

typedef jobject (*Mel_Gui_Android_Construct)(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc);

bool     mel_gui_android_register_constructor(Mel_Atom class_atom, Mel_Gui_Android_Construct cb);

JNIEnv*  mel_gui_android_env(void);
jobject  mel_gui_android_host(void);
jclass   mel_gui_android_host_class(void);

jobject  mel_gui_android_activity(JNIEnv* env);
jlong    mel_gui_android_pack_handle(Mel_Gui_Handle h);
jobject  mel_gui_android_new_listener(JNIEnv* env, Mel_Gui_Handle h);
void     mel_gui_android_bind_edit_watcher(JNIEnv* env, jobject edit, jobject watcher);
void     mel_gui_android_install_focus_listener(JNIEnv* env, jobject view, jobject listener);

jstring  mel_gui_android_utf16(JNIEnv* env, str8 s);
str8     mel_gui_android_str8(JNIEnv* env, jstring s, const Mel_Alloc* alloc);
void     mel_gui_android_str8_free(JNIEnv* env, str8 s, const Mel_Alloc* alloc);
