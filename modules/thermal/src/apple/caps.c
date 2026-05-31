#include <thermal/thermal.h>

Mel_Thermal_Caps mel_thermal_caps(void)
{
    return (Mel_Thermal_Caps){
        .present = true,
        .temperature = mel_thermal_temperature(MEL_THERMAL_TEMP_DOMAIN_PRIMARY).fidelity,
    };
}
