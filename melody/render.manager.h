#pragma once

#include "render.manager.fwd.h"
#include "collection.bitset.h"
#include "render.types.3d.h"
#include "math.geo.rect.h"
#include "math.mat4.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "allocator.fwd.h"

#define MEL_MGR_FREE_END UINT32_MAX

#define MEL_RENDER_OBJECT_NONE      0
#define MEL_RENDER_OBJECT_SPRITE_2D 1
#define MEL_RENDER_OBJECT_MESH_3D   2

typedef struct {
    Mel_Vec2 pos;
    Mel_Vec2 scale;
    f32 rotation;
    f32 depth;
    u32 flags;
    u32 _pad[3];
} Mel_Render_Object_Sprite2D;

typedef struct {
    Mel_Mat4 model;
    Mel_Mat4 model_inverse;
} Mel_Render_Object_Mesh3D;

typedef struct Mel_Render_Object {
    u32 kind;
    u32 material_base_id;
    u32 material_idx;
    u32 flags;
    u32 layer_mask;
    u32 texture_idx;
    u32 _pad0[2];

    Mel_Geometry_Handle mesh;
    u32 _pad1[2];

    Mel_Rect uv;
    Mel_Vec4 color;
    Mel_Render_Bounds bounds;

    union {
        Mel_Render_Object_Sprite2D sprite2d;
        Mel_Render_Object_Mesh3D mesh3d;
    };
} Mel_Render_Object;

struct Mel_Render_Manager {
    u32* sparse;
    u32* generations;
    u32* packed_to_sparse;
    u32 sparse_capacity;
    u32 packed_capacity;
    u32 packed_count;
    u32 free_head;

    Mel_Render_Object* objects;
    Mel_BitSet dirty;
    u64 mutation_serial;

    const Mel_Alloc* alloc;
};

typedef struct {
    const Mel_Alloc* alloc;
    u32 initial_capacity;
} Mel_Render_Manager_Opt;

void mel_mgr_init_opt(Mel_Render_Manager* mgr, Mel_Render_Manager_Opt opt);
#define mel_mgr_init(mgr, ...) mel_mgr_init_opt((mgr), (Mel_Render_Manager_Opt){__VA_ARGS__})

void mel_mgr_shutdown(Mel_Render_Manager* mgr);

Mel_Render_Handle mel_mgr_alloc(Mel_Render_Manager* mgr);
void              mel_mgr_free(Mel_Render_Manager* mgr, Mel_Render_Handle h);
bool              mel_mgr_alive(Mel_Render_Manager* mgr, Mel_Render_Handle h);

void                mel_mgr_set_object(Mel_Render_Manager* mgr, Mel_Render_Handle h, const Mel_Render_Object* object);
Mel_Render_Object*  mel_mgr_get_object(Mel_Render_Manager* mgr, Mel_Render_Handle h);
void                mel_mgr_mark_dirty(Mel_Render_Manager* mgr, Mel_Render_Handle h);

u32                       mel_mgr_count(Mel_Render_Manager* mgr);
const Mel_Render_Object*  mel_mgr_objects(Mel_Render_Manager* mgr);
u64                       mel_mgr_mutation_serial(Mel_Render_Manager* mgr);
