#include <core/platform.h>

#if !MEL_PLATFORM_OSX
#error "macos-only translation unit"
#endif

#include <platform/platform.h>
#include "../../src/platform_internal.h"

#include <IOKit/pwr_mgt/IOPMLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdlib.h>

static const char* macos_name(void) { return "macos"; }

static u32 macos_device_class(void) { return MEL_PLATFORM_DEVICE_DESKTOP; }

static Mel_Platform_Sandbox macos_sandbox(void)
{
    const char* container = getenv("APP_SANDBOX_CONTAINER_ID");
    if (container && container[0])
        return (Mel_Platform_Sandbox){ MEL_PLATFORM_SANDBOX_APPLE, container };
    return (Mel_Platform_Sandbox){ MEL_PLATFORM_SANDBOX_NONE, NULL };
}

static Mel_Platform_Inhibit_Native macos_inhibit(const char* reason)
{
    CFStringRef     name = CFStringCreateWithCString(kCFAllocatorDefault, reason ? reason : "melody", kCFStringEncodingUTF8);
    IOPMAssertionID id = 0;
    IOReturn        rc = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleDisplaySleep, kIOPMAssertionLevelOn, name, &id);
    if (name)
        CFRelease(name);
    if (rc != kIOReturnSuccess)
        return (Mel_Platform_Inhibit_Native){ MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE, 0 };
    return (Mel_Platform_Inhibit_Native){ MEL_PLATFORM_OK, (u64)id };
}

static Mel_Platform_Status macos_uninhibit(u64 native)
{
    if (IOPMAssertionRelease((IOPMAssertionID)native) != kIOReturnSuccess)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_INVALID;
    return MEL_PLATFORM_OK;
}

const Mel_Platform_Backend* mel_platform__backend(void)
{
    static const Mel_Platform_Backend backend = {
        .name = macos_name,
        .device_class = macos_device_class,
        .sandbox = macos_sandbox,
        .screensaver_inhibit = macos_inhibit,
        .screensaver_uninhibit = macos_uninhibit,
    };
    return &backend;
}
