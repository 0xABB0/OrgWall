#pragma once

#include <core/types.h>
#include <core/compiler.h>

typedef enum
{
    MEL_POWER_SOURCE_UNKNOWN = 0,
    MEL_POWER_SOURCE_AC = 1,
    MEL_POWER_SOURCE_BATTERY = 2,
} Mel_Power_Source;

typedef enum
{
    MEL_POWER_LOW_POWER_UNKNOWN = 0,
    MEL_POWER_LOW_POWER_OFF = 1,
    MEL_POWER_LOW_POWER_ON = 2,
} Mel_Power_Low_Power_Mode;

typedef struct
{
    f32  bias;
    bool present;
} Mel_Power_Profile;

typedef struct
{
    bool present;
    bool charging;
    f32  level;
    f32  seconds_to_empty;
    f32  seconds_to_full;
} Mel_Power_Battery;

MEL_NODISCARD Mel_Power_Source         mel_power_source_current(void);
MEL_NODISCARD Mel_Power_Profile        mel_power_profile_current(void);
MEL_NODISCARD bool                     mel_power_profile_name(char* buf, usize cap);
MEL_NODISCARD Mel_Power_Low_Power_Mode mel_power_low_power_current(void);
MEL_NODISCARD Mel_Power_Battery        mel_power_battery_current(void);

typedef struct
{
    bool power_source_present;
    bool profile_present;
    bool battery_present;
} Mel_Power_Caps;

MEL_NODISCARD Mel_Power_Caps mel_power_caps(void);
