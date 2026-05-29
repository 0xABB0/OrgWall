#include <sensor.thermal/thermal.h>
#include <sensor.power/power.h>
#include <sensor/caps.h>

Mel_Sensor_Thermal_Pressure mel_sensor_thermal_current(void)
{
    return MEL_SENSOR_THERMAL_UNKNOWN;
}

Mel_Sensor_Power_Source mel_sensor_power_source_current(void)
{
    return MEL_SENSOR_POWER_SOURCE_UNKNOWN;
}

Mel_Sensor_Low_Power_Mode mel_sensor_low_power_current(void)
{
    return MEL_SENSOR_LOW_POWER_UNKNOWN;
}

Mel_Sensor_Temperature mel_sensor_thermal_temperature(Mel_Sensor_Temp_Domain domain)
{
    (void)domain;
    return (Mel_Sensor_Temperature){ .celsius = 0.0f, .fidelity = MEL_SENSOR_TEMP_NONE };
}

Mel_Sensor_Caps mel_sensor_caps(void)
{
    return (Mel_Sensor_Caps){0};
}
