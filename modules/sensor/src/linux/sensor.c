#include <sensor.thermal/thermal.h>
#include <sensor.power/power.h>
#include <sensor/caps.h>

#include "../sensor_sysfs.h"

Mel_Sensor_Thermal_Pressure mel_sensor_thermal_current(void)
{
    return mel_sysfs_thermal_tier();
}

Mel_Sensor_Temperature mel_sensor_thermal_temperature(Mel_Sensor_Temp_Domain domain)
{
    return mel_sysfs_temperature(domain);
}

Mel_Sensor_Power_Source mel_sensor_power_source_current(void)
{
    return mel_sysfs_power_source();
}

static bool mel_linux_platform_profile(Mel_Sensor_Low_Power_Mode *out)
{
    char buf[32];
    if (!mel_sysfs_read_str("/sys/firmware/acpi/platform_profile", buf, sizeof buf)) return false;
    if (strcmp(buf, "low-power") == 0 || strcmp(buf, "quiet") == 0)
        *out = MEL_SENSOR_LOW_POWER_ON;
    else
        *out = MEL_SENSOR_LOW_POWER_OFF;
    return true;
}

Mel_Sensor_Low_Power_Mode mel_sensor_low_power_current(void)
{
    Mel_Sensor_Low_Power_Mode mode;
    if (mel_linux_platform_profile(&mode)) return mode;
    return MEL_SENSOR_LOW_POWER_UNKNOWN;
}

Mel_Sensor_Caps mel_sensor_caps(void)
{
    char buf[32];
    return (Mel_Sensor_Caps){
        .thermal_present      = mel_sysfs_thermal_present(),
        .power_source_present = mel_sysfs_power_source_present(),
        .low_power_present    = mel_sysfs_read_str("/sys/firmware/acpi/platform_profile", buf, sizeof buf),
        .temperature          = mel_sysfs_temperature(MEL_SENSOR_TEMP_DOMAIN_PRIMARY).fidelity,
    };
}
