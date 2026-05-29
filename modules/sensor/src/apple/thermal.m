#include <sensor.thermal/thermal.h>

#import <Foundation/Foundation.h>

Mel_Sensor_Thermal_Pressure mel_sensor_thermal_current(void)
{
    switch ([[NSProcessInfo processInfo] thermalState]) {
        case NSProcessInfoThermalStateNominal:  return MEL_SENSOR_THERMAL_NOMINAL;
        case NSProcessInfoThermalStateFair:     return MEL_SENSOR_THERMAL_FAIR;
        case NSProcessInfoThermalStateSerious:  return MEL_SENSOR_THERMAL_SERIOUS;
        case NSProcessInfoThermalStateCritical: return MEL_SENSOR_THERMAL_CRITICAL;
    }
    return MEL_SENSOR_THERMAL_UNKNOWN;
}
