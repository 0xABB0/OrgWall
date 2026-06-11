#include <thermal/thermal.h>

Mel_Thermal_Pressure mel_thermal_current(void) { return MEL_THERMAL_UNKNOWN; }

Mel_Thermal_Temperature mel_thermal_temperature(Mel_Thermal_Temp_Domain domain)
{
    (void)domain;
    return (Mel_Thermal_Temperature){ .celsius = 0.0f, .fidelity = MEL_THERMAL_TEMP_NONE };
}

Mel_Thermal_Caps mel_thermal_caps(void) { return (Mel_Thermal_Caps){ .present = false, .temperature = MEL_THERMAL_TEMP_NONE }; }

Mel_Thermal_Sensor_List mel_thermal_sensor_enumerate(const Mel_Alloc* alloc)
{
    (void)alloc;
    return (Mel_Thermal_Sensor_List){ 0 };
}
