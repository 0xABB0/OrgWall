#include <sensor.power/power.h>

#import <Foundation/Foundation.h>

Mel_Sensor_Low_Power_Mode mel_sensor_low_power_current(void)
{
    NSProcessInfo *info = [NSProcessInfo processInfo];
    if (![info respondsToSelector:@selector(isLowPowerModeEnabled)])
        return MEL_SENSOR_LOW_POWER_UNKNOWN;
    return info.lowPowerModeEnabled ? MEL_SENSOR_LOW_POWER_ON : MEL_SENSOR_LOW_POWER_OFF;
}
