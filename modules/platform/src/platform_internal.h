#pragma once

#include <platform/platform.h>

typedef struct
{
    Mel_Platform_Status status;
    u64                 native;
} Mel_Platform_Inhibit_Native;

typedef struct
{
    const char* (*name)(void);
    u32 (*device_class)(void);
    Mel_Platform_Sandbox (*sandbox)(void);

    Mel_Platform_Inhibit_Native (*screensaver_inhibit)(const char* reason);
    Mel_Platform_Status (*screensaver_uninhibit)(u64 native);
} Mel_Platform_Backend;

const Mel_Platform_Backend* mel_platform__backend(void);

#include <allocator/allocator.fwd.h>

const Mel_Alloc* mel_platform__alloc(void);
