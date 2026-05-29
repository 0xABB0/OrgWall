#include <sensor.power/power.h>

#import <UIKit/UIKit.h>

Mel_Sensor_Power_Source mel_sensor_power_source_current(void)
{
    UIDevice *device = [UIDevice currentDevice];
    BOOL was_enabled = device.batteryMonitoringEnabled;
    if (!was_enabled) device.batteryMonitoringEnabled = YES;

    UIDeviceBatteryState state = device.batteryState;

    if (!was_enabled) device.batteryMonitoringEnabled = NO;

    switch (state) {
        case UIDeviceBatteryStateCharging:
        case UIDeviceBatteryStateFull:     return MEL_SENSOR_POWER_SOURCE_AC;
        case UIDeviceBatteryStateUnplugged: return MEL_SENSOR_POWER_SOURCE_BATTERY;
        default:                            return MEL_SENSOR_POWER_SOURCE_UNKNOWN;
    }
}
