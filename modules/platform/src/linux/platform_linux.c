#include <core/platform.h>

#if !MEL_PLATFORM_LINUX
#error "linux-only translation unit"
#endif

#include <platform/platform.h>
#include "../platform_internal.h"

#include <stdlib.h>
#include <unistd.h>

static const char* linux_name(void) { return "linux"; }

static u32 linux_device_class(void) { return MEL_PLATFORM_DEVICE_DESKTOP; }

static Mel_Platform_Sandbox linux_sandbox(void)
{
    if (access("/.flatpak-info", F_OK) == 0)
    {
        const char* id = getenv("FLATPAK_ID");
        return (Mel_Platform_Sandbox){ MEL_PLATFORM_SANDBOX_FLATPAK, id };
    }
    const char* snap = getenv("SNAP");
    if (snap && snap[0])
    {
        const char* name = getenv("SNAP_NAME");
        return (Mel_Platform_Sandbox){ MEL_PLATFORM_SANDBOX_SNAP, name };
    }
    return (Mel_Platform_Sandbox){ MEL_PLATFORM_SANDBOX_NONE, NULL };
}

static Mel_Platform_Inhibit_Native linux_inhibit(const char* reason)
{
    (void)reason;
    return (Mel_Platform_Inhibit_Native){ MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE, 0 };
}

static Mel_Platform_Status linux_uninhibit(u64 native)
{
    (void)native;
    return MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE;
}

const Mel_Platform_Backend* mel_platform__backend(void)
{
    static const Mel_Platform_Backend backend = {
        .name = linux_name,
        .device_class = linux_device_class,
        .sandbox = linux_sandbox,
        .screensaver_inhibit = linux_inhibit,
        .screensaver_uninhibit = linux_uninhibit,
    };
    return &backend;
}
