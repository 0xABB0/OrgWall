#include <thermal/thermal.h>

#import <Foundation/Foundation.h>

Mel_Thermal_Pressure mel_thermal_current(void)
{
    switch ([[NSProcessInfo processInfo] thermalState])
    {
    case NSProcessInfoThermalStateNominal:
        return MEL_THERMAL_NOMINAL;
    case NSProcessInfoThermalStateFair:
        return MEL_THERMAL_FAIR;
    case NSProcessInfoThermalStateSerious:
        return MEL_THERMAL_SERIOUS;
    case NSProcessInfoThermalStateCritical:
        return MEL_THERMAL_CRITICAL;
    }
    return MEL_THERMAL_UNKNOWN;
}
