#pragma once

#include "render.source.type.h"
#include "render.manager.h"
#include "render.types.3d.h"
#include "allocator.fwd.h"

extern const Mel_Render_Source_Type mel_source_manual_type;

typedef struct {
    Mel_Geometry_Handle mesh;
    u32 material_binding_index;
    u32 flags;
    u32 _pad;
} Mel_Render_Mesh_Part;

Mel_Render_Source* mel_source_manual_create(const Mel_Alloc* alloc);

Mel_Render_Handle mel_source_manual_add(Mel_Render_Source* source,
                                         Mel_Mat4 transform,
                                         Mel_Render_Bounds bounds,
                                         Mel_Render_Info info);

void mel_source_manual_remove(Mel_Render_Source* source, Mel_Render_Handle h);

void mel_source_manual_set_transform(Mel_Render_Source* source,
                                      Mel_Render_Handle h, Mel_Mat4 transform);

void mel_source_manual_set_bounds(Mel_Render_Source* source,
                                   Mel_Render_Handle h, Mel_Render_Bounds bounds);

void mel_source_manual_set_info(Mel_Render_Source* source,
                                 Mel_Render_Handle h, Mel_Render_Info info);
void mel_source_manual_set_mesh_parts(Mel_Render_Source* source,
                                      Mel_Render_Handle h,
                                      const Mel_Render_Mesh_Part* parts,
                                      u32 part_count);
void mel_source_manual_set_material_bindings(Mel_Render_Source* source,
                                             Mel_Render_Handle h,
                                             const Mel_Render_Material_Binding* bindings,
                                             u32 binding_count);
