#pragma once

#include "render.source.fwd.h"
#include "core.types.h"
#include "string.str8.fwd.h"
#include "render.list.fwd.h"
#include "render.target.fwd.h"

typedef enum {
    MEL_SOURCE_LIST = 0,
    MEL_SOURCE_RETAINED = 1,
    MEL_SOURCE_GPU_BUFFER = 2,
    MEL_SOURCE_TARGET = 3,
    MEL_SOURCE_PROCEDURAL = 4,
} Mel_Source_Kind;

typedef enum {
    MEL_SOURCE_ACCESS_CPU_WRITE = 1 << 0,
    MEL_SOURCE_ACCESS_GPU_WRITE = 1 << 1,
    MEL_SOURCE_ACCESS_CPU_READ  = 1 << 2,
    MEL_SOURCE_ACCESS_GPU_READ  = 1 << 3,
} Mel_Source_Access_Flags;

typedef enum {
    MEL_SOURCE_LIFETIME_FRAME = 0,
    MEL_SOURCE_LIFETIME_RETAINED = 1,
    MEL_SOURCE_LIFETIME_EXTERNAL = 2,
} Mel_Source_Lifetime;

typedef enum {
    MEL_SCHEMA_INVALID = 0,
    MEL_SCHEMA_SPRITE = 1,
    MEL_SCHEMA_TEXT = 2,
    MEL_SCHEMA_MESH_INSTANCE = 3,
    MEL_SCHEMA_LIGHT = 4,
    MEL_SCHEMA_MESHLET_DB = 5,
    MEL_SCHEMA_HDR_COLOR = 6,
} Mel_Source_Schema;

typedef struct {
    str8 name;
    Mel_Source_Kind kind;
    u32 schema;
    u32 access_flags;
    u32 lifetime;
    void* user;
} Mel_Source_Desc;

Mel_Source_Handle mel_source_create(const Mel_Source_Desc* desc);
void mel_source_destroy(Mel_Source_Handle source);

Mel_Source_Handle mel_source_from_render_list(Mel_Render_List* list, u32 schema);
Mel_Source_Handle mel_source_from_target(Mel_Render_Target* target, u32 schema);

str8 mel_source_name(Mel_Source_Handle source);
Mel_Source_Kind mel_source_kind(Mel_Source_Handle source);
u32 mel_source_schema(Mel_Source_Handle source);
u32 mel_source_access_flags(Mel_Source_Handle source);
u32 mel_source_lifetime(Mel_Source_Handle source);
void* mel_source_user(Mel_Source_Handle source);

Mel_Render_List* mel_source_render_list(Mel_Source_Handle source);
Mel_Render_Target* mel_source_target(Mel_Source_Handle source);
