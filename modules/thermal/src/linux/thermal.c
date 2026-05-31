#include <thermal/thermal.h>

#include "../thermal_sysfs.h"

Mel_Thermal_Pressure mel_thermal_current(void) { return mel_sysfs_thermal_tier(); }

Mel_Thermal_Temperature mel_thermal_temperature(Mel_Thermal_Temp_Domain domain) { return mel_sysfs_temperature(domain); }

Mel_Thermal_Caps mel_thermal_caps(void)
{
    return (Mel_Thermal_Caps){
        .present = mel_sysfs_thermal_present(),
        .temperature = mel_sysfs_temperature(MEL_THERMAL_TEMP_DOMAIN_PRIMARY).fidelity,
    };
}
