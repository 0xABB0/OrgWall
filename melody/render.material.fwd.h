#pragma once

#include "collection.slotmap.fwd.h"

typedef struct Mel_Material_Family Mel_Material_Family;
typedef struct Mel_Material_Template Mel_Material_Template;
typedef struct Mel_Material_Instance Mel_Material_Instance;
typedef struct Mel_Material_Backend Mel_Material_Backend;
typedef struct Mel_Material_Table Mel_Material_Table;

typedef struct { Mel_SlotMap_Handle handle; } Mel_Material_Family_Handle;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Material_Template_Handle;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Material_Instance_Handle;

#define MEL_MATERIAL_FAMILY_HANDLE_NULL ((Mel_Material_Family_Handle){0})
#define MEL_MATERIAL_TEMPLATE_HANDLE_NULL ((Mel_Material_Template_Handle){0})
#define MEL_MATERIAL_INSTANCE_HANDLE_NULL ((Mel_Material_Instance_Handle){0})

static inline bool mel_material_family_handle_valid(Mel_Material_Family_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}

static inline bool mel_material_template_handle_valid(Mel_Material_Template_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}

static inline bool mel_material_instance_handle_valid(Mel_Material_Instance_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}
