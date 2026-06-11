#include <core/platform.h>

#if !MEL_PLATFORM_WEB
#error "web-only translation unit"
#endif

#include <platform/platform.h>
#include "../../src/platform_internal.h"

static const char* web_name(void) { return "web"; }

static u32 web_device_class(void) { return MEL_PLATFORM_DEVICE_UNKNOWN; }

static Mel_Platform_Sandbox web_sandbox(void) { return (Mel_Platform_Sandbox){ MEL_PLATFORM_SANDBOX_NONE, NULL }; }

static Mel_Platform_Inhibit_Native web_inhibit(const char* reason)
{
    (void)reason;
    return (Mel_Platform_Inhibit_Native){ MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE, 0 };
}

static Mel_Platform_Status web_uninhibit(u64 native)
{
    (void)native;
    return MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE;
}

const Mel_Platform_Backend* mel_platform__backend(void)
{
    static const Mel_Platform_Backend backend = {
        .name = web_name,
        .device_class = web_device_class,
        .sandbox = web_sandbox,
        .screensaver_inhibit = web_inhibit,
        .screensaver_uninhibit = web_uninhibit,
    };
    return &backend;
}
