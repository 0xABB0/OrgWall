#pragma once

#include "core.types.h"
#include "math.vec2.h"

#define MEL_LAYOUTABLE_FLAG_GROUP_HEADER (1 << 0)

#define MEL_ORIENTATION_HORIZONTAL  0
#define MEL_ORIENTATION_VERTICAL    1

#define MEL_ALIGN_MIN     0
#define MEL_ALIGN_MIDDLE  1
#define MEL_ALIGN_MAX     2
#define MEL_ALIGN_FILL    3

typedef struct Mel_Layoutable Mel_Layoutable;
typedef struct Mel_Layout Mel_Layout;

typedef struct Mel_Layoutable_VTable {
    Mel_Vec2        (*preferred_size)(Mel_Layoutable* l);
    void            (*set_position)(Mel_Layoutable* l, Mel_Vec2 pos);
    void            (*set_size)(Mel_Layoutable* l, Mel_Vec2 size);
    Mel_Vec2        (*get_position)(Mel_Layoutable* l);
    Mel_Vec2        (*get_size)(Mel_Layoutable* l);
    Mel_Vec2        (*get_fixed_size)(Mel_Layoutable* l);
    bool            (*is_visible)(Mel_Layoutable* l);
    u32             (*get_flags)(Mel_Layoutable* l);
    Mel_Layoutable* (*first_child)(Mel_Layoutable* l);
    Mel_Layoutable* (*next_sibling)(Mel_Layoutable* l);
} Mel_Layoutable_VTable;

typedef struct Mel_Layoutable {
    const Mel_Layoutable_VTable* vtable;
} Mel_Layoutable;

typedef struct Mel_Layout_VTable {
    Mel_Vec2 (*preferred_size)(Mel_Layout* layout, Mel_Layoutable* container);
    void     (*perform_layout)(Mel_Layout* layout, Mel_Layoutable* container);
} Mel_Layout_VTable;

typedef struct Mel_Layout {
    const Mel_Layout_VTable* vtable;
} Mel_Layout;

Mel_Vec2 mel_layout_preferred_size(Mel_Layout* layout, Mel_Layoutable* container);
void     mel_layout_perform(Mel_Layout* layout, Mel_Layoutable* container);

[[nodiscard]] static inline Mel_Vec2 mel_layoutable_preferred_size(Mel_Layoutable* l);
[[nodiscard]] static inline Mel_Vec2 mel_layoutable_get_position(Mel_Layoutable* l);
[[nodiscard]] static inline Mel_Vec2 mel_layoutable_get_size(Mel_Layoutable* l);
[[nodiscard]] static inline Mel_Vec2 mel_layoutable_get_fixed_size(Mel_Layoutable* l);
[[nodiscard]] static inline bool     mel_layoutable_is_visible(Mel_Layoutable* l);
[[nodiscard]] static inline u32      mel_layoutable_get_flags(Mel_Layoutable* l);
[[nodiscard]] static inline Mel_Layoutable* mel_layoutable_first_child(Mel_Layoutable* l);
[[nodiscard]] static inline Mel_Layoutable* mel_layoutable_next_sibling(Mel_Layoutable* l);
[[nodiscard]] static inline Mel_Vec2 mel_layoutable_resolved_size(Mel_Layoutable* l);

static inline void mel_layoutable_set_position(Mel_Layoutable* l, Mel_Vec2 pos);
static inline void mel_layoutable_set_size(Mel_Layoutable* l, Mel_Vec2 size);

#include "ui.layout.inl"
