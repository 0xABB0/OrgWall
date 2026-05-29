#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <sensor.thermal/thermal.h>

typedef struct {
    bool                     thermal_present;
    bool                     power_source_present;
    bool                     low_power_present;
    Mel_Sensor_Temp_Fidelity temperature;
} Mel_Sensor_Caps;

MEL_NODISCARD Mel_Sensor_Caps mel_sensor_caps(void);
