#include <power/power.h>

Mel_Power_Source mel_power_source_current(void) { return MEL_POWER_SOURCE_UNKNOWN; }

Mel_Power_Low_Power_Mode mel_power_low_power_current(void) { return MEL_POWER_LOW_POWER_UNKNOWN; }

Mel_Power_Caps mel_power_caps(void) { return (Mel_Power_Caps){ 0 }; }
