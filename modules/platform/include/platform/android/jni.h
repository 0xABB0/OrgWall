#pragma once

#include <jni.h>

#include <allocator/allocator.h>

JavaVM* mel_platform_android_vm(void);
JNIEnv* mel_platform_android_env(void);

jclass mel_platform_android_find_class(JNIEnv* env, const char* name);

typedef void (*Mel_Platform_Android_Permission_Cb)(void* user, i32 request_code, bool granted);

void mel_platform_android_permission_listen(const Mel_Alloc* alloc, i32 request_code, Mel_Platform_Android_Permission_Cb cb, void* user);
void mel_platform_android_permission_unlisten(i32 request_code, Mel_Platform_Android_Permission_Cb cb, void* user);
void mel_platform_android_permission_dispatch(i32 request_code, bool granted);
