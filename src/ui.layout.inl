#pragma once

#ifdef _CLANGD
#include "ui.layout.h"
#endif

static inline Mel_Vec2 mel_layoutable_preferred_size(Mel_Layoutable* l)
{
    assert(l != nullptr);
    assert(l->vtable != nullptr);
    assert(l->vtable->preferred_size != nullptr);
    return l->vtable->preferred_size(l);
}

static inline void mel_layoutable_set_position(Mel_Layoutable* l, Mel_Vec2 pos)
{
    assert(l != nullptr);
    assert(l->vtable != nullptr);
    assert(l->vtable->set_position != nullptr);
    l->vtable->set_position(l, pos);
}

static inline void mel_layoutable_set_size(Mel_Layoutable* l, Mel_Vec2 size)
{
    assert(l != nullptr);
    assert(l->vtable != nullptr);
    assert(l->vtable->set_size != nullptr);
    l->vtable->set_size(l, size);
}

static inline Mel_Vec2 mel_layoutable_get_position(Mel_Layoutable* l)
{
    assert(l != nullptr);
    assert(l->vtable != nullptr);
    assert(l->vtable->get_position != nullptr);
    return l->vtable->get_position(l);
}

static inline Mel_Vec2 mel_layoutable_get_size(Mel_Layoutable* l)
{
    assert(l != nullptr);
    assert(l->vtable != nullptr);
    assert(l->vtable->get_size != nullptr);
    return l->vtable->get_size(l);
}

static inline Mel_Vec2 mel_layoutable_get_fixed_size(Mel_Layoutable* l)
{
    assert(l != nullptr);
    assert(l->vtable != nullptr);
    assert(l->vtable->get_fixed_size != nullptr);
    return l->vtable->get_fixed_size(l);
}

static inline bool mel_layoutable_is_visible(Mel_Layoutable* l)
{
    assert(l != nullptr);
    assert(l->vtable != nullptr);
    assert(l->vtable->is_visible != nullptr);
    return l->vtable->is_visible(l);
}

static inline u32 mel_layoutable_get_flags(Mel_Layoutable* l)
{
    assert(l != nullptr);
    assert(l->vtable != nullptr);
    assert(l->vtable->get_flags != nullptr);
    return l->vtable->get_flags(l);
}

static inline Mel_Layoutable* mel_layoutable_first_child(Mel_Layoutable* l)
{
    assert(l != nullptr);
    assert(l->vtable != nullptr);
    assert(l->vtable->first_child != nullptr);
    return l->vtable->first_child(l);
}

static inline Mel_Layoutable* mel_layoutable_next_sibling(Mel_Layoutable* l)
{
    assert(l != nullptr);
    assert(l->vtable != nullptr);
    assert(l->vtable->next_sibling != nullptr);
    return l->vtable->next_sibling(l);
}

static inline Mel_Vec2 mel_layoutable_resolved_size(Mel_Layoutable* l)
{
    Mel_Vec2 fixed = mel_layoutable_get_fixed_size(l);
    Mel_Vec2 pref = mel_layoutable_preferred_size(l);
    return mel_vec2(
        fixed.x > 0 ? fixed.x : pref.x,
        fixed.y > 0 ? fixed.y : pref.y
    );
}
