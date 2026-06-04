#pragma once

#include <core/types.h>
#include <platform/platform.h>
#include <platform/android/jni.h>
#include <future/future.h>

#ifdef __cplusplus
extern "C"
{
#endif

void    mel_platform_android_set_activity(jobject activity);
jobject mel_platform_android_activity(void);

i32 mel_platform_android_sdk_version(void);

enum
{
    MEL_PLATFORM_PERMISSION_DENIED = 0u,
    MEL_PLATFORM_PERMISSION_GRANTED = 1u << 0,
};

typedef struct
{
    u32 result;
} Mel_Platform_Permission_Outcome;

typedef struct
{
    Mel_Executor* deliver;
} Mel_Platform_Permission_Opt;

Mel_Future* mel_platform_android_request_permission_opt(const char* permission, Mel_Platform_Permission_Opt opt);
#define mel_platform_android_request_permission(permission, ...) mel_platform_android_request_permission_opt((permission), (Mel_Platform_Permission_Opt){ __VA_ARGS__ })

const Mel_Platform_Permission_Outcome* mel_platform_android_permission_outcome(Mel_Future* f);

void mel_platform_android__permission_resolve(u64 token, bool granted);

enum
{
    MEL_PLATFORM_TOAST_SHORT = 0u,
    MEL_PLATFORM_TOAST_LONG = 1u << 0,
};

Mel_Platform_Status mel_platform_android_toast(const char* text, u32 flags);

const char* mel_platform_android_internal_storage_path(void);
const char* mel_platform_android_external_storage_path(void);
const char* mel_platform_android_cache_path(void);

#ifdef __cplusplus
}
#endif
