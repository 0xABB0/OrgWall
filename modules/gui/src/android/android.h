#pragma once

#include "../gui_internal.h"

#include <jni.h>

typedef struct {
    jclass    cls;
    jmethodID destroyView;
    jmethodID setText;
    jmethodID getText;
    jmethodID setBounds;
    jmethodID setVisible;
    jmethodID setEnabled;
    jmethodID setFocus;
    jmethodID invalidate;
    jmethodID presentFrame;
    jmethodID setActivityTitle;
} Mel_Gui_Android;

Mel_Gui_Android* mel_gui__android(void);
JNIEnv*          mel_gui__android_env(void);
jstring          mel_gui__android_jstring(JNIEnv* env, str8 s);
jlong            mel_gui__android_pack(Mel_Gui_Handle h);
Mel_Gui_Handle   mel_gui__android_unpack(jlong p);

bool mel_gui__android_frame_register_jni    (JNIEnv* env);
bool mel_gui__android_label_register_jni    (JNIEnv* env);
bool mel_gui__android_button_register_jni   (JNIEnv* env);
bool mel_gui__android_checkbox_register_jni (JNIEnv* env);
bool mel_gui__android_textfield_register_jni(JNIEnv* env);
bool mel_gui__android_slider_register_jni   (JNIEnv* env);
bool mel_gui__android_canvas_register_jni   (JNIEnv* env);
