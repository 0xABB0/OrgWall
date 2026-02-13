#pragma once

#include "core.types.h"
#include "math.vec2.h"
#include "ui.layout.h"
#include "ui.native.ctrl.fwd.h"

struct Mel_NCtrl_VTable {
    void     (*create_backing)(Mel_NCtrl* ctrl);
    void     (*destroy_backing)(Mel_NCtrl* ctrl);
    void     (*set_frame)(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h);
    void     (*sync_frame)(Mel_NCtrl* ctrl);
    void     (*set_visible)(Mel_NCtrl* ctrl, bool visible);
    void     (*set_enabled)(Mel_NCtrl* ctrl, bool enabled);
    Mel_Vec2 (*preferred_size)(Mel_NCtrl* ctrl);
    void     (*add_child_backing)(Mel_NCtrl* parent, Mel_NCtrl* child);
    void     (*remove_child_backing)(Mel_NCtrl* parent, Mel_NCtrl* child);
};

struct Mel_NCtrl {
    Mel_Layoutable layoutable;

    const Mel_NCtrl_VTable* vtable;
    void* backing;

    Mel_NCtrl* parent;
    Mel_NCtrl* first_child;
    Mel_NCtrl* next_sibling;

    Mel_Layout* layout;

    Mel_Vec2 pos;
    Mel_Vec2 size;
    Mel_Vec2 fixed_size;

    u32 flags;
    bool visible;
    bool enabled;
};

void mel_nctrl_init(Mel_NCtrl* ctrl, const Mel_NCtrl_VTable* vtable);
void mel_nctrl_create_backing(Mel_NCtrl* ctrl);
void mel_nctrl_destroy(Mel_NCtrl* ctrl);
void mel_nctrl_add_child(Mel_NCtrl* parent, Mel_NCtrl* child);
void mel_nctrl_remove_child(Mel_NCtrl* parent, Mel_NCtrl* child);
void mel_nctrl_set_visible(Mel_NCtrl* ctrl, bool visible);
void mel_nctrl_set_enabled(Mel_NCtrl* ctrl, bool enabled);
void mel_nctrl_set_position(Mel_NCtrl* ctrl, Mel_Vec2 pos);
void mel_nctrl_set_size(Mel_NCtrl* ctrl, Mel_Vec2 size);
void mel_nctrl_set_layout(Mel_NCtrl* ctrl, Mel_Layout* layout);
void mel_nctrl_perform_layout(Mel_NCtrl* ctrl);
