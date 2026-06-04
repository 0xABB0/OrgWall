#include <core/platform.h>

#if !MEL_PLATFORM_WINDOWS
#error "win32-only translation unit"
#endif

#include <platform/platform.h>
#include "../platform_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const char* win32_name(void) { return "win32"; }

static u32 win32_device_class(void) { return MEL_PLATFORM_DEVICE_DESKTOP; }

static Mel_Platform_Sandbox win32_sandbox(void) { return (Mel_Platform_Sandbox){ MEL_PLATFORM_SANDBOX_NONE, NULL }; }

static Mel_Platform_Inhibit_Native win32_inhibit(const char* reason)
{
    (void)reason;
    EXECUTION_STATE prev = SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    if (prev == 0)
        return (Mel_Platform_Inhibit_Native){ MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE, 0 };
    return (Mel_Platform_Inhibit_Native){ MEL_PLATFORM_OK, 1 };
}

static Mel_Platform_Status win32_uninhibit(u64 native)
{
    (void)native;
    if (SetThreadExecutionState(ES_CONTINUOUS) == 0)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE;
    return MEL_PLATFORM_OK;
}

const Mel_Platform_Backend* mel_platform__backend(void)
{
    static const Mel_Platform_Backend backend = {
        .name = win32_name,
        .device_class = win32_device_class,
        .sandbox = win32_sandbox,
        .screensaver_inhibit = win32_inhibit,
        .screensaver_uninhibit = win32_uninhibit,
    };
    return &backend;
}
