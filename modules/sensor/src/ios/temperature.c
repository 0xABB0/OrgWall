#include <sensor.thermal/thermal.h>

Mel_Sensor_Temperature mel_sensor_thermal_temperature(Mel_Sensor_Temp_Domain domain)
{
    (void)domain;
    return (Mel_Sensor_Temperature){ .celsius = 0.0f, .fidelity = MEL_SENSOR_TEMP_NONE };
}
