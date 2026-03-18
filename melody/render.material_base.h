#pragma once

#include "render.material_base.fwd.h"
#include "string.str8.h"
#include "gpu.shader.h"
#include "gpu.buffer.h"
#include "gpu.pipeline.h"
#include "gpu.device.fwd.h"
#include "allocator.fwd.h"

#define MEL_COMPAT_FORWARD   (1u << 0)
#define MEL_COMPAT_DEFERRED  (1u << 1)

#define MEL_MATERIAL_BASE_MAX 32

struct Mel_Material_Base {
    str8 name;
    u32 param_size;
    Mel_Gpu_Shader shader;
    Mel_Gpu_Pipeline gpu_pipeline;
    u32 compat;
    bool shader_ready;
    bool pipeline_ready;

    u8* params;
    u32 instance_count;
    u32 instance_capacity;

    Mel_Gpu_Buffer param_buffer;
    bool param_buffer_dirty;

    const Mel_Alloc* alloc;
};

typedef struct {
    str8 name;
    u32 param_size;
    str8 shader_path;
    u32 compat;
} Mel_Material_Base_Desc;

Mel_Material_Base_Id mel_material_base_register(const Mel_Material_Base_Desc* desc);
Mel_Material_Base_Id mel_material_base_find(str8 name);
Mel_Material_Base* mel_material_base_get(Mel_Material_Base_Id id);
u32 mel_material_base_count(void);

Mel_Material_Instance_Id mel_material_base_alloc_instance(Mel_Material_Base_Id base_id, const void* initial_params);
void mel_material_base_free_instance(Mel_Material_Base_Id base_id, Mel_Material_Instance_Id instance_id);
void mel_material_base_set_params(Mel_Material_Base_Id base_id, Mel_Material_Instance_Id instance_id, const void* params);
const void* mel_material_base_get_params(Mel_Material_Base_Id base_id, Mel_Material_Instance_Id instance_id);

void mel_material_base_upload_dirty(Mel_Material_Base_Id base_id, Mel_Gpu_Device* dev);
Mel_Gpu_Buffer* mel_material_base_param_buffer(Mel_Material_Base_Id base_id);

void mel__material_base_init_gpu(Mel_Material_Base_Id base_id, Mel_Gpu_Device* dev);
