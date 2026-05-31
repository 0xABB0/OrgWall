#pragma once

#include <core/types.h>
#include <core/compiler.h>

#include <allocator/allocator.fwd.h>
#include <temperature/temperature.h>

typedef enum
{
    MEL_THERMAL_UNKNOWN = 0,
    MEL_THERMAL_NOMINAL = 1,
    MEL_THERMAL_FAIR = 2,
    MEL_THERMAL_SERIOUS = 3,
    MEL_THERMAL_CRITICAL = 4,
} Mel_Thermal_Pressure;

MEL_NODISCARD Mel_Thermal_Pressure mel_thermal_current(void);

typedef enum
{
    MEL_THERMAL_TEMP_NONE = 0,
    MEL_THERMAL_TEMP_DERIVED = 1,
    MEL_THERMAL_TEMP_MEASURED = 2,
} Mel_Thermal_Temp_Fidelity;

typedef enum
{
    MEL_THERMAL_TEMP_DOMAIN_PRIMARY = 0,
    MEL_THERMAL_TEMP_DOMAIN_CPU = 1,
    MEL_THERMAL_TEMP_DOMAIN_GPU = 2,
    MEL_THERMAL_TEMP_DOMAIN_AMBIENT = 3,
} Mel_Thermal_Temp_Domain;

typedef struct
{
    f32                       celsius;
    Mel_Thermal_Temp_Fidelity fidelity;
} Mel_Thermal_Temperature;

MEL_NODISCARD Mel_Thermal_Temperature mel_thermal_temperature(Mel_Thermal_Temp_Domain domain);

typedef struct
{
    bool                      present;
    Mel_Thermal_Temp_Fidelity temperature;
} Mel_Thermal_Caps;

MEL_NODISCARD Mel_Thermal_Caps mel_thermal_caps(void);

typedef struct
{
    Mel_Degrees               value;
    Mel_Thermal_Temp_Fidelity fidelity;
} Mel_Thermal_Reading;

typedef struct Mel_Thermal_Sensor Mel_Thermal_Sensor;

typedef Mel_Thermal_Reading (*Mel_Thermal_Sensor_Get)(Mel_Thermal_Sensor* self, void* user);

struct Mel_Thermal_Sensor
{
    const char*             name;
    Mel_Thermal_Temp_Domain domain;
    Mel_Thermal_Sensor_Get  get;
    u64                     handle;
};

typedef struct
{
    Mel_Thermal_Sensor* items;
    usize               count;
} Mel_Thermal_Sensor_List;

MEL_NODISCARD Mel_Thermal_Sensor_List mel_thermal_sensor_enumerate(const Mel_Alloc* alloc);

void mel_thermal_sensor_list_free(Mel_Thermal_Sensor_List* list, const Mel_Alloc* alloc);

MEL_NODISCARD static inline Mel_Thermal_Reading mel_thermal_sensor_read(Mel_Thermal_Sensor* self, void* user) { return self->get(self, user); }
