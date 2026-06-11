#pragma once

#include <core/types.h>
#include <platform/platform.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef void (*Mel_Platform_iOS_Animation_Cb)(f64 timestamp_s, void* user);

Mel_Platform_Status mel_platform_ios_set_animation_callback(u32 interval_frames, Mel_Platform_iOS_Animation_Cb cb, void* user);
Mel_Platform_Status mel_platform_ios_clear_animation_callback(void);

Mel_Platform_Status mel_platform_ios_set_event_pump(bool enabled);
bool                mel_platform_ios_event_pump_enabled(void);

#ifdef __cplusplus
}
#endif
