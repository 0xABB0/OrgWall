#include <power/power.h>

Mel_Power_Low_Power_Mode mel_power_low_power_current(void)
{
    Mel_Power_Profile p = mel_power_profile_current();
    if (!p.present)
        return MEL_POWER_LOW_POWER_UNKNOWN;
    return p.bias < 0.0f ? MEL_POWER_LOW_POWER_ON : MEL_POWER_LOW_POWER_OFF;
}
