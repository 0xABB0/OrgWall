#pragma once

#include <core/types.h>
#include <collection/slotmap.h>

typedef enum
{
    MEL_GPU_OWNERSHIP_OWNED = 0,
    MEL_GPU_OWNERSHIP_BORROWED = 1,
} Mel_Gpu_Ownership;

typedef struct
{
    Mel_Gpu_Ownership ownership;
    bool              capture_replay;
    const char*       name;
} Mel_Gpu_Resource_Header;

#define MEL_GPU_HANDLE(Name)     \
    typedef struct               \
    {                            \
        Mel_SlotMap_Handle slot; \
    } Name

#define MEL_GPU_HANDLE_INDIRECT(Name)     \
    typedef struct                        \
    {                                     \
        Mel_SlotMap_Handle slot;          \
        u32                bindless_slot; \
    } Name

static inline bool mel_gpu_handle_eq(Mel_SlotMap_Handle a, Mel_SlotMap_Handle b) { return a.index == b.index && a.generation == b.generation; }

static inline Mel_SlotMap_Handle mel_gpu_handle_null(void) { return (Mel_SlotMap_Handle){ .index = 0, .generation = 0 }; }
