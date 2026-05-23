#pragma once

#include "../gui_internal.h"

#include <jni.h>

typedef struct {
    jclass    cls;
    jmethodID createFrame;
    jmethodID createLabel;
    jmethodID createButton;
    jmethodID createCheckBox;
    jmethodID createTextField;
    jmethodID createSlider;
    jmethodID createCanvas;
    jmethodID destroyView;
    jmethodID setText;
    jmethodID getText;
    jmethodID setBounds;
    jmethodID setVisible;
    jmethodID setEnabled;
    jmethodID setFocus;
    jmethodID invalidate;
    jmethodID sliderValue;
    jmethodID setSliderValue;
    jmethodID checkBoxChecked;
    jmethodID presentFrame;
} Mel_Gui_Android;

Mel_Gui_Android* mel_gui__android(void);
JNIEnv*          mel_gui__android_env(void);
jstring          mel_gui__android_jstring(JNIEnv* env, str8 s);
jlong            mel_gui__android_pack(Mel_Gui_Handle h);
Mel_Gui_Handle   mel_gui__android_unpack(jlong p);
