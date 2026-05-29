#pragma once

#include <core/compiler.h>

typedef enum {
    MEL_SENSOR_POWER_SOURCE_UNKNOWN = 0,
    MEL_SENSOR_POWER_SOURCE_AC      = 1,
    MEL_SENSOR_POWER_SOURCE_BATTERY = 2,
} Mel_Sensor_Power_Source;

typedef enum {
    MEL_SENSOR_LOW_POWER_UNKNOWN = 0,
    MEL_SENSOR_LOW_POWER_OFF     = 1,
    MEL_SENSOR_LOW_POWER_ON      = 2,
} Mel_Sensor_Low_Power_Mode;

MEL_NODISCARD Mel_Sensor_Power_Source   mel_sensor_power_source_current(void);
MEL_NODISCARD Mel_Sensor_Low_Power_Mode mel_sensor_low_power_current(void);
