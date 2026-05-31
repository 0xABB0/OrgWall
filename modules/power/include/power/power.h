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

MEL_NODISCARD Mel_Power_Source         mel_power_source_current(void);
MEL_NODISCARD Mel_Power_Low_Power_Mode mel_power_low_power_current(void);

typedef struct
{
    bool power_source_present;
    bool low_power_present;
} Mel_Power_Caps;

MEL_NODISCARD Mel_Power_Caps mel_power_caps(void);
