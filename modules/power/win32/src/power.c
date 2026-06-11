#include <power/power.h>

#include "../../src/power_str.h"

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

Mel_Power_Profile mel_power_profile_current(void)
{
    SYSTEM_POWER_STATUS s;
    if (!GetSystemPowerStatus(&s))
        return (Mel_Power_Profile){ 0 };
    bool saver = (s.SystemStatusFlag & 1) != 0;
    return (Mel_Power_Profile){ .bias = saver ? -1.0f : 0.0f, .present = true };
}

bool mel_power_profile_name(char* buf, usize cap)
{
    SYSTEM_POWER_STATUS s;
    if (!GetSystemPowerStatus(&s))
        return false;
    bool saver = (s.SystemStatusFlag & 1) != 0;
    return mel_power_name_copy(buf, cap, saver ? "Battery saver" : "Balanced");
}

Mel_Power_Battery mel_power_battery_current(void)
{
    Mel_Power_Battery out = { false, false, 0.0f, -1.0f, -1.0f };

    SYSTEM_POWER_STATUS s;
    if (!GetSystemPowerStatus(&s))
        return out;
    if (s.BatteryFlag & 128)
        return out;

    out.present = true;
    if (s.BatteryLifePercent <= 100)
        out.level = (f32)s.BatteryLifePercent / 100.0f;
    out.charging = (s.BatteryFlag & 8) != 0;
    if (s.BatteryLifeTime != (DWORD)-1)
        out.seconds_to_empty = (f32)s.BatteryLifeTime;
    return out;
}

Mel_Power_Caps mel_power_caps(void)
{
    SYSTEM_POWER_STATUS s;
    bool                ok = GetSystemPowerStatus(&s);
    return (Mel_Power_Caps){
        .power_source_present = ok,
        .profile_present = ok,
        .battery_present = ok && !(s.BatteryFlag & 128),
    };
}
