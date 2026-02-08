#include "ui.native.ctrl.h"
#include <string.h>

static Mel_Vec2 nctrl_layoutable_preferred_size(Mel_Layoutable* l)
{
    Mel_NCtrl* ctrl = (Mel_NCtrl*)l;
    if (ctrl->vtable && ctrl->vtable->preferred_size)
        return ctrl->vtable->preferred_size(ctrl);
    return ctrl->size;
}

static void nctrl_layoutable_set_position(Mel_Layoutable* l, Mel_Vec2 pos)
{
    Mel_NCtrl* ctrl = (Mel_NCtrl*)l;
    ctrl->pos = pos;
    if (ctrl->vtable && ctrl->vtable->set_frame)
        ctrl->vtable->set_frame(ctrl, pos.x, pos.y, ctrl->size.x, ctrl->size.y);
}

static void nctrl_layoutable_set_size(Mel_Layoutable* l, Mel_Vec2 sz)
{
    Mel_NCtrl* ctrl = (Mel_NCtrl*)l;
    ctrl->size = sz;
    if (ctrl->vtable && ctrl->vtable->set_frame)
        ctrl->vtable->set_frame(ctrl, ctrl->pos.x, ctrl->pos.y, sz.x, sz.y);
}

static Mel_Vec2 nctrl_layoutable_get_position(Mel_Layoutable* l)
{
    return ((Mel_NCtrl*)l)->pos;
}

static Mel_Vec2 nctrl_layoutable_get_size(Mel_Layoutable* l)
{
    return ((Mel_NCtrl*)l)->size;
}

static Mel_Vec2 nctrl_layoutable_get_fixed_size(Mel_Layoutable* l)
{
    return ((Mel_NCtrl*)l)->fixed_size;
}

static bool nctrl_layoutable_is_visible(Mel_Layoutable* l)
{
    return ((Mel_NCtrl*)l)->visible;
}

static u32 nctrl_layoutable_get_flags(Mel_Layoutable* l)
{
    return ((Mel_NCtrl*)l)->flags;
}

static Mel_Layoutable* nctrl_layoutable_first_child(Mel_Layoutable* l)
{
    Mel_NCtrl* ctrl = (Mel_NCtrl*)l;
    return ctrl->first_child ? &ctrl->first_child->layoutable : nullptr;
}

static Mel_Layoutable* nctrl_layoutable_next_sibling(Mel_Layoutable* l)
{
    Mel_NCtrl* ctrl = (Mel_NCtrl*)l;
    return ctrl->next_sibling ? &ctrl->next_sibling->layoutable : nullptr;
}

static const Mel_Layoutable_VTable s_nctrl_layoutable_vtable = {
    .preferred_size = nctrl_layoutable_preferred_size,
    .set_position   = nctrl_layoutable_set_position,
    .set_size       = nctrl_layoutable_set_size,
    .get_position   = nctrl_layoutable_get_position,
    .get_size       = nctrl_layoutable_get_size,
    .get_fixed_size = nctrl_layoutable_get_fixed_size,
    .is_visible     = nctrl_layoutable_is_visible,
    .get_flags      = nctrl_layoutable_get_flags,
    .first_child    = nctrl_layoutable_first_child,
    .next_sibling   = nctrl_layoutable_next_sibling,
};

void mel_nctrl_init(Mel_NCtrl* ctrl, const Mel_NCtrl_VTable* vtable)
{
    assert(ctrl != nullptr);
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->layoutable.vtable = &s_nctrl_layoutable_vtable;
    ctrl->vtable = vtable;
    ctrl->visible = true;
    ctrl->enabled = true;
}

void mel_nctrl_create_backing(Mel_NCtrl* ctrl)
{
    assert(ctrl != nullptr);
    if (ctrl->vtable && ctrl->vtable->create_backing)
        ctrl->vtable->create_backing(ctrl);
}

void mel_nctrl_destroy(Mel_NCtrl* ctrl)
{
    assert(ctrl != nullptr);
    Mel_NCtrl* child = ctrl->first_child;
    while (child) {
        Mel_NCtrl* next = child->next_sibling;
        mel_nctrl_destroy(child);
        child = next;
    }
    ctrl->first_child = nullptr;
    if (ctrl->vtable && ctrl->vtable->destroy_backing)
        ctrl->vtable->destroy_backing(ctrl);
    ctrl->backing = nullptr;
}

void mel_nctrl_add_child(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    assert(parent != nullptr);
    assert(child != nullptr);
    assert(child->parent == nullptr);

    child->parent = parent;
    child->next_sibling = nullptr;

    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        Mel_NCtrl* last = parent->first_child;
        while (last->next_sibling)
            last = last->next_sibling;
        last->next_sibling = child;
    }

    if (parent->vtable && parent->vtable->add_child_backing)
        parent->vtable->add_child_backing(parent, child);
}

void mel_nctrl_remove_child(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    assert(parent != nullptr);
    assert(child != nullptr);
    assert(child->parent == parent);

    if (parent->first_child == child) {
        parent->first_child = child->next_sibling;
    } else {
        Mel_NCtrl* prev = parent->first_child;
        while (prev && prev->next_sibling != child)
            prev = prev->next_sibling;
        if (prev)
            prev->next_sibling = child->next_sibling;
    }

    if (parent->vtable && parent->vtable->remove_child_backing)
        parent->vtable->remove_child_backing(parent, child);

    child->parent = nullptr;
    child->next_sibling = nullptr;
}

void mel_nctrl_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    assert(ctrl != nullptr);
    ctrl->visible = visible;
    if (ctrl->vtable && ctrl->vtable->set_visible)
        ctrl->vtable->set_visible(ctrl, visible);
}

void mel_nctrl_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    assert(ctrl != nullptr);
    ctrl->enabled = enabled;
    if (ctrl->vtable && ctrl->vtable->set_enabled)
        ctrl->vtable->set_enabled(ctrl, enabled);
}

void mel_nctrl_set_position(Mel_NCtrl* ctrl, Mel_Vec2 pos)
{
    assert(ctrl != nullptr);
    ctrl->pos = pos;
    if (ctrl->vtable && ctrl->vtable->set_frame)
        ctrl->vtable->set_frame(ctrl, pos.x, pos.y, ctrl->size.x, ctrl->size.y);
}

void mel_nctrl_set_size(Mel_NCtrl* ctrl, Mel_Vec2 sz)
{
    assert(ctrl != nullptr);
    ctrl->size = sz;
    if (ctrl->vtable && ctrl->vtable->set_frame)
        ctrl->vtable->set_frame(ctrl, ctrl->pos.x, ctrl->pos.y, sz.x, sz.y);
}

void mel_nctrl_set_layout(Mel_NCtrl* ctrl, Mel_Layout* layout)
{
    assert(ctrl != nullptr);
    ctrl->layout = layout;
}

void mel_nctrl_perform_layout(Mel_NCtrl* ctrl)
{
    assert(ctrl != nullptr);
    if (ctrl->layout)
        mel_layout_perform(ctrl->layout, &ctrl->layoutable);

    Mel_NCtrl* child = ctrl->first_child;
    while (child) {
        if (child->vtable && child->vtable->sync_frame)
            child->vtable->sync_frame(child);
        mel_nctrl_perform_layout(child);
        child = child->next_sibling;
    }
}
