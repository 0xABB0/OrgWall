#include <power/power.h>

#include "../../src/power_str.h"

#import <Foundation/Foundation.h>

static bool mel_apple_low_power(bool* on)
{
    NSProcessInfo* info = [NSProcessInfo processInfo];
    if (![info respondsToSelector:@selector(isLowPowerModeEnabled)])
        return false;
    *on = info.lowPowerModeEnabled;
    return true;
}

Mel_Power_Profile mel_power_profile_current(void)
{
    bool on = false;
    if (!mel_apple_low_power(&on))
        return (Mel_Power_Profile){ 0 };
    return (Mel_Power_Profile){ .bias = on ? -1.0f : 0.0f, .present = true };
}

bool mel_power_profile_name(char* buf, usize cap)
{
    bool on = false;
    if (!mel_apple_low_power(&on))
        return false;
    return mel_power_name_copy(buf, cap, on ? "Low Power" : "Automatic");
}
