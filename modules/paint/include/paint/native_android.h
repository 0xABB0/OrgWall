#pragma once

#include <jni.h>

typedef struct
{
    JNIEnv* env;
    jobject canvas;
    jobject paint;
} Mel_Paint_Android_Native;
