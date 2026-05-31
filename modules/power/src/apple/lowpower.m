#include <power/power.h>

#import <Foundation/Foundation.h>

Mel_Power_Low_Power_Mode mel_power_low_power_current(void)
{
    NSProcessInfo* info = [NSProcessInfo processInfo];
    if (![info respondsToSelector:@selector(isLowPowerModeEnabled)])
        return MEL_POWER_LOW_POWER_UNKNOWN;
    return info.lowPowerModeEnabled ? MEL_POWER_LOW_POWER_ON : MEL_POWER_LOW_POWER_OFF;
}
