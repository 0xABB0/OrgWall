#include <power/power.h>

#import <UIKit/UIKit.h>

Mel_Power_Source mel_power_source_current(void)
{
    UIDevice* device = [UIDevice currentDevice];
    BOOL      was_enabled = device.batteryMonitoringEnabled;
    if (!was_enabled)
        device.batteryMonitoringEnabled = YES;

    UIDeviceBatteryState state = device.batteryState;

    if (!was_enabled)
        device.batteryMonitoringEnabled = NO;

    switch (state)
    {
    case UIDeviceBatteryStateCharging:
    case UIDeviceBatteryStateFull:
        return MEL_POWER_SOURCE_AC;
    case UIDeviceBatteryStateUnplugged:
        return MEL_POWER_SOURCE_BATTERY;
    default:
        return MEL_POWER_SOURCE_UNKNOWN;
    }
}

Mel_Power_Battery mel_power_battery_current(void)
{
    UIDevice* device = [UIDevice currentDevice];
    BOOL      was_enabled = device.batteryMonitoringEnabled;
    if (!was_enabled)
        device.batteryMonitoringEnabled = YES;

    float                level = device.batteryLevel;
    UIDeviceBatteryState state = device.batteryState;

    if (!was_enabled)
        device.batteryMonitoringEnabled = NO;

    Mel_Power_Battery out = { false, false, 0.0f, -1.0f, -1.0f };
    if (level >= 0.0f)
    {
        out.present = true;
        out.level = level;
    }
    out.charging = (state == UIDeviceBatteryStateCharging);
    return out;
}

Mel_Power_Caps mel_power_caps(void)
{
    NSProcessInfo*    info = [NSProcessInfo processInfo];
    Mel_Power_Battery b = mel_power_battery_current();
    return (Mel_Power_Caps){
        .power_source_present = true,
        .profile_present = [info respondsToSelector:@selector(isLowPowerModeEnabled)],
        .battery_present = b.present,
    };
}
