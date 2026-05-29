#include <sensor.thermal/thermal.h>
#include <sensor.power/power.h>
#include <sensor/caps.h>

#include <windows.h>

Mel_Sensor_Thermal_Pressure mel_sensor_thermal_current(void)
{
    return MEL_SENSOR_THERMAL_UNKNOWN;
}

Mel_Sensor_Temperature mel_sensor_thermal_temperature(Mel_Sensor_Temp_Domain domain)
{
    (void)domain;
    return (Mel_Sensor_Temperature){ .celsius = 0.0f, .fidelity = MEL_SENSOR_TEMP_NONE };
}

Mel_Sensor_Power_Source mel_sensor_power_source_current(void)
{
    SYSTEM_POWER_STATUS s;
    if (!GetSystemPowerStatus(&s)) return MEL_SENSOR_POWER_SOURCE_UNKNOWN;
    switch (s.ACLineStatus) {
        case 0:  return MEL_SENSOR_POWER_SOURCE_BATTERY;
        case 1:  return MEL_SENSOR_POWER_SOURCE_AC;
        default: return MEL_SENSOR_POWER_SOURCE_UNKNOWN;
    }
}

Mel_Sensor_Low_Power_Mode mel_sensor_low_power_current(void)
{
    SYSTEM_POWER_STATUS s;
    if (!GetSystemPowerStatus(&s)) return MEL_SENSOR_LOW_POWER_UNKNOWN;
    return (s.SystemStatusFlag & 1) ? MEL_SENSOR_LOW_POWER_ON : MEL_SENSOR_LOW_POWER_OFF;
}

Mel_Sensor_Caps mel_sensor_caps(void)
{
    SYSTEM_POWER_STATUS s;
    bool ok = GetSystemPowerStatus(&s);
    return (Mel_Sensor_Caps){
        .thermal_present      = false,
        .power_source_present = ok,
        .low_power_present    = ok,
        .temperature          = MEL_SENSOR_TEMP_NONE,
    };
}
