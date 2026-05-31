#include <power/power.h>

#import <Foundation/Foundation.h>

Mel_Power_Caps mel_power_caps(void)
{
    NSProcessInfo* info = [NSProcessInfo processInfo];
    return (Mel_Power_Caps) { .power_source_present = true, .low_power_present = [info respondsToSelector:@selector(isLowPowerModeEnabled)], };
}
