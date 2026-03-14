#pragma once

#include "render.light.fwd.h"
#include "gpu.device.fwd.h"
#include "gpu.buffer.h"
#include "math.vec3.h"
#include "math.vec4.h"

struct Mel_Point_Light {
    Mel_Vec3 position;
    f32 radius;
    Mel_Vec4 color;
    f32 intensity;
};

struct Mel_Point_Light_Gpu_Record {
    Mel_Vec4 position_radius;
    Mel_Vec4 color_intensity;
};

struct Mel_Light_Table {
    Mel_Gpu_Device* dev;
    Mel_Gpu_Buffer buffer;
    Mel_Point_Light_Gpu_Record* records;
    u32 count;
    u32 capacity;
};

typedef struct {
    Mel_Gpu_Device* dev;
    u32 capacity;
} Mel_Light_Table_Init_Opt;

bool mel_light_table_init_opt(Mel_Light_Table* table, Mel_Light_Table_Init_Opt opt);
#define mel_light_table_init(table, ...) mel_light_table_init_opt((table), (Mel_Light_Table_Init_Opt){__VA_ARGS__})
void mel_light_table_shutdown(Mel_Light_Table* table);
void mel_light_table_clear(Mel_Light_Table* table);
u32 mel_light_table_push(Mel_Light_Table* table, Mel_Point_Light light);
u32 mel_light_table_push_record(Mel_Light_Table* table, Mel_Point_Light_Gpu_Record record);
void mel_light_table_upload(Mel_Light_Table* table);
Mel_Point_Light_Gpu_Record* mel_light_table_records(Mel_Light_Table* table);
u32 mel_light_table_count(Mel_Light_Table* table);

Mel_Point_Light_Gpu_Record mel_point_light_pack_gpu_record(Mel_Point_Light light);
