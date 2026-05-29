#include <sensor/caps.h>

#import <Foundation/Foundation.h>

Mel_Sensor_Caps mel_sensor_caps(void)
{
    NSProcessInfo *info = [NSProcessInfo processInfo];
    return (Mel_Sensor_Caps){
        .thermal_present      = true,
        .power_source_present = true,
        .low_power_present    = [info respondsToSelector:@selector(isLowPowerModeEnabled)],
        .temperature          = mel_sensor_thermal_temperature(MEL_SENSOR_TEMP_DOMAIN_PRIMARY).fidelity,
    };
}
