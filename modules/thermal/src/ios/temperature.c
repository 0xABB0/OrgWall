#include <thermal/thermal.h>

Mel_Thermal_Temperature mel_thermal_temperature(Mel_Thermal_Temp_Domain domain)
{
    (void)domain;
    return (Mel_Thermal_Temperature){ .celsius = 0.0f, .fidelity = MEL_THERMAL_TEMP_NONE };
}
