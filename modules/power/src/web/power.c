#include <power/power.h>

Mel_Power_Source mel_power_source_current(void) { return MEL_POWER_SOURCE_UNKNOWN; }

Mel_Power_Profile mel_power_profile_current(void) { return (Mel_Power_Profile){ 0 }; }

bool mel_power_profile_name(char* buf, usize cap)
{
    (void)buf;
    (void)cap;
    return false;
}

Mel_Power_Battery mel_power_battery_current(void) { return (Mel_Power_Battery){ false, false, 0.0f, -1.0f, -1.0f }; }

Mel_Power_Caps mel_power_caps(void) { return (Mel_Power_Caps){ 0 }; }
