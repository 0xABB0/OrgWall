#pragma once

#include <core/types.h>
#include <core/compiler.h>

typedef enum {
    MEL_SENSOR_THERMAL_UNKNOWN  = 0,
    MEL_SENSOR_THERMAL_NOMINAL  = 1,
    MEL_SENSOR_THERMAL_FAIR     = 2,
    MEL_SENSOR_THERMAL_SERIOUS  = 3,
    MEL_SENSOR_THERMAL_CRITICAL = 4,
} Mel_Sensor_Thermal_Pressure;

MEL_NODISCARD Mel_Sensor_Thermal_Pressure mel_sensor_thermal_current(void);

typedef enum {
    MEL_SENSOR_TEMP_NONE     = 0,
    MEL_SENSOR_TEMP_DERIVED  = 1,
    MEL_SENSOR_TEMP_MEASURED = 2,
} Mel_Sensor_Temp_Fidelity;

typedef enum {
    MEL_SENSOR_TEMP_DOMAIN_PRIMARY = 0,
    MEL_SENSOR_TEMP_DOMAIN_CPU     = 1,
    MEL_SENSOR_TEMP_DOMAIN_GPU     = 2,
    MEL_SENSOR_TEMP_DOMAIN_AMBIENT = 3,
} Mel_Sensor_Temp_Domain;

typedef struct {
    f32                      celsius;
    Mel_Sensor_Temp_Fidelity fidelity;
} Mel_Sensor_Temperature;

MEL_NODISCARD Mel_Sensor_Temperature mel_sensor_thermal_temperature(Mel_Sensor_Temp_Domain domain);
