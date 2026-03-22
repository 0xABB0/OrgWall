#pragma once

#include "render.manager.h"
#include "render.source.manual.h"
#include "render.types.3d.h"
#include "gpu.geometry_pool.h"
#include "render.material_base.h"
#include "math.vec3.h"
#include "math.mat4.h"
#include "allocator.fwd.h"

#define SPONZA_WIN_W 1600
#define SPONZA_WIN_H 900

typedef struct {
    Mel_Render_Mesh_Part* parts;
    u32 part_count;
    Mel_Render_Material_Binding* bindings;
    u32 binding_count;
    Mel_Render_Bounds bounds;
    Mel_Mat4 model;
    Mel_Vec3 world_center;
    Mel_Vec3 world_extents;
} Sponza_Load_Result;

typedef struct {
    u32 material_count;
    u32 primitive_count;
    u32 vertex_count;
    u32 index_count;
} Sponza_Scan_Result;

usize sponza_vertex_stride(void);
Mel_Material_Base_Id sponza_ensure_forward_lit_material_base(void);
bool sponza_scan(Sponza_Scan_Result* out, const Mel_Alloc* alloc);
bool sponza_load(Sponza_Load_Result* out,
                 Mel_Geometry_Pool* pool,
                 Mel_Material_Base_Id forward_lit_id,
                 const Mel_Alloc* alloc);
void sponza_load_result_free(Sponza_Load_Result* result, const Mel_Alloc* alloc);
