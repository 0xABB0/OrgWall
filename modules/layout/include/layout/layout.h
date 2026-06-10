#pragma once

#include <core/types.h>
#include <string/str8.h>

typedef enum
{
    MEL_ALIGN_DEFAULT = 0,
    MEL_ALIGN_START = 1,
    MEL_ALIGN_CENTER = 2,
    MEL_ALIGN_END = 3,
    MEL_ALIGN_STRETCH = 4,
} Mel_Align;

typedef struct
{
    i32 preferred_w;
    i32 preferred_h;
    i32 fixed_w;
    i32 fixed_h;
    i32 weight;
    u8  cross_align;
    i32 margin_l;
    i32 margin_t;
    i32 margin_r;
    i32 margin_b;
} Mel_Layoutable;

/* One child as the solver sees it. The host fills the inputs from its own
 * tree, the solver writes the outputs, the host applies them; the solver
 * never walks the host's tree. natural_w/h stand in when the spec names no
 * size: the child's current or natively-measured extent. */
typedef struct
{
    Mel_Layoutable spec;
    i32            natural_w;
    i32            natural_h;
    i32            x, y, w, h;
} Mel_Layout_Item;

typedef struct Mel_Layout Mel_Layout;

/* A layout kind is its class pointer — an open set, never a tag. A host that
 * recognises a class may hand the arrangement to a richer engine (a gui
 * backend lowering to the OS layout system); every host can always run
 * measure/arrange portably, so an unrecognised class still lays out. */
typedef struct
{
    str8 name;
    void (*measure)(const Mel_Layout*, const Mel_Layout_Item* items, u32 count, i32 avail_w, i32 avail_h, i32* out_w, i32* out_h);
    void (*arrange)(const Mel_Layout*, Mel_Layout_Item* items, u32 count, i32 avail_w, i32 avail_h);
} Mel_Layout_Class;

struct Mel_Layout
{
    const Mel_Layout_Class* cls;
};

/* fixed > preferred > natural — the one sizing rule every solver shares. */
void mel_layout_item_preferred(const Mel_Layout_Item* item, i32* out_w, i32* out_h);
