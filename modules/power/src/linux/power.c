#include <power/power.h>

#include "../power_sysfs.h"

#include <string.h>

Mel_Power_Source mel_power_source_current(void) { return mel_sysfs_power_source(); }

static bool mel_linux_platform_profile(Mel_Power_Low_Power_Mode* out)
{
    char buf[32];
    if (!mel_sysfs_read_str("/sys/firmware/acpi/platform_profile", buf, sizeof buf))
        return false;
    if (strcmp(buf, "low-power") == 0 || strcmp(buf, "quiet") == 0)
        *out = MEL_POWER_LOW_POWER_ON;
    else
        *out = MEL_POWER_LOW_POWER_OFF;
    return true;
}

Mel_Power_Low_Power_Mode mel_power_low_power_current(void)
{
    Mel_Power_Low_Power_Mode mode;
    if (mel_linux_platform_profile(&mode))
        return mode;
    return MEL_POWER_LOW_POWER_UNKNOWN;
}

Mel_Power_Caps mel_power_caps(void)
{
    char buf[32];
    return (Mel_Power_Caps){
        .power_source_present = mel_sysfs_power_source_present(),
        .low_power_present = mel_sysfs_read_str("/sys/firmware/acpi/platform_profile", buf, sizeof buf),
    };
}
