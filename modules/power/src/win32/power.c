#include <power/power.h>

#include <windows.h>

Mel_Power_Source mel_power_source_current(void)
{
    SYSTEM_POWER_STATUS s;
    if (!GetSystemPowerStatus(&s))
        return MEL_POWER_SOURCE_UNKNOWN;
    switch (s.ACLineStatus)
    {
    case 0:
        return MEL_POWER_SOURCE_BATTERY;
    case 1:
        return MEL_POWER_SOURCE_AC;
    default:
        return MEL_POWER_SOURCE_UNKNOWN;
    }
}

Mel_Power_Low_Power_Mode mel_power_low_power_current(void)
{
    SYSTEM_POWER_STATUS s;
    if (!GetSystemPowerStatus(&s))
        return MEL_POWER_LOW_POWER_UNKNOWN;
    return (s.SystemStatusFlag & 1) ? MEL_POWER_LOW_POWER_ON : MEL_POWER_LOW_POWER_OFF;
}

Mel_Power_Caps mel_power_caps(void)
{
    SYSTEM_POWER_STATUS s;
    bool                ok = GetSystemPowerStatus(&s);
    return (Mel_Power_Caps){
        .power_source_present = ok,
        .low_power_present = ok,
    };
}
