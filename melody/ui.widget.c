#include "ui.widget.h"
#include <string.h>

static Mel_Vec2 widget_layoutable_preferred_size(Mel_Layoutable* l)
{
    Mel_Widget* w = (Mel_Widget*)l;
    if (w->measure)
        return w->measure(w);
    return w->size;
}

static void widget_layoutable_set_position(Mel_Layoutable* l, Mel_Vec2 pos)
{
    ((Mel_Widget*)l)->pos = pos;
}

static void widget_layoutable_set_size(Mel_Layoutable* l, Mel_Vec2 sz)
{
    ((Mel_Widget*)l)->size = sz;
}

static Mel_Vec2 widget_layoutable_get_position(Mel_Layoutable* l)
{
    return ((Mel_Widget*)l)->pos;
}

static Mel_Vec2 widget_layoutable_get_size(Mel_Layoutable* l)
{
    return ((Mel_Widget*)l)->size;
}

static Mel_Vec2 widget_layoutable_get_fixed_size(Mel_Layoutable* l)
{
    return ((Mel_Widget*)l)->fixed_size;
}

static bool widget_layoutable_is_visible(Mel_Layoutable* l)
{
    return ((Mel_Widget*)l)->visible;
}

static u32 widget_layoutable_get_flags(Mel_Layoutable* l)
{
    return ((Mel_Widget*)l)->flags;
}

static Mel_Layoutable* widget_layoutable_first_child(Mel_Layoutable* l)
{
    Mel_Widget* w = (Mel_Widget*)l;
    return w->first_child ? &w->first_child->layoutable : nullptr;
}

static Mel_Layoutable* widget_layoutable_next_sibling(Mel_Layoutable* l)
{
    Mel_Widget* w = (Mel_Widget*)l;
    return w->next_sibling ? &w->next_sibling->layoutable : nullptr;
}

static const Mel_Layoutable_VTable s_widget_layoutable_vtable = {
    .preferred_size = widget_layoutable_preferred_size,
    .set_position   = widget_layoutable_set_position,
    .set_size       = widget_layoutable_set_size,
    .get_position   = widget_layoutable_get_position,
    .get_size       = widget_layoutable_get_size,
    .get_fixed_size = widget_layoutable_get_fixed_size,
    .is_visible     = widget_layoutable_is_visible,
    .get_flags      = widget_layoutable_get_flags,
    .first_child    = widget_layoutable_first_child,
    .next_sibling   = widget_layoutable_next_sibling,
};

void mel_widget_init(Mel_Widget* widget)
{
    assert(widget != nullptr);
    memset(widget, 0, sizeof(*widget));
    widget->layoutable.vtable = &s_widget_layoutable_vtable;
    widget->visible = true;
    widget->enabled = true;
}

void mel_widget_destroy(Mel_Widget* widget)
{
    assert(widget != nullptr);
    Mel_Widget* child = widget->first_child;
    while (child) {
        Mel_Widget* next = child->next_sibling;
        mel_widget_destroy(child);
        child = next;
    }
    widget->first_child = nullptr;

    if (widget->on_destroy)
        widget->on_destroy(widget);
}

void mel_widget_add_child(Mel_Widget* parent, Mel_Widget* child)
{
    assert(parent != nullptr);
    assert(child != nullptr);
    assert(child->parent == nullptr);

    child->parent = parent;
    child->next_sibling = nullptr;

    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        Mel_Widget* last = parent->first_child;
        while (last->next_sibling)
            last = last->next_sibling;
        last->next_sibling = child;
    }
}

void mel_widget_remove_child(Mel_Widget* parent, Mel_Widget* child)
{
    assert(parent != nullptr);
    assert(child != nullptr);
    assert(child->parent == parent);

    if (parent->first_child == child) {
        parent->first_child = child->next_sibling;
    } else {
        Mel_Widget* prev = parent->first_child;
        while (prev && prev->next_sibling != child)
            prev = prev->next_sibling;
        if (prev)
            prev->next_sibling = child->next_sibling;
    }

    child->parent = nullptr;
    child->next_sibling = nullptr;
}

void mel_widget_set_layout(Mel_Widget* widget, Mel_Layout* layout)
{
    assert(widget != nullptr);
    widget->layout = layout;
}

void mel_widget_perform_layout(Mel_Widget* widget)
{
    assert(widget != nullptr);
    if (widget->layout)
        mel_layout_perform(widget->layout, &widget->layoutable);

    Mel_Widget* child = widget->first_child;
    while (child) {
        mel_widget_perform_layout(child);
        child = child->next_sibling;
    }
}

void mel_widget_set_visible(Mel_Widget* widget, bool visible)
{
    assert(widget != nullptr);
    widget->visible = visible;
}

void mel_widget_set_enabled(Mel_Widget* widget, bool enabled)
{
    assert(widget != nullptr);
    widget->enabled = enabled;
    if (enabled)
        widget->state &= ~MEL_WIDGET_STATE_DISABLED;
    else
        widget->state |= MEL_WIDGET_STATE_DISABLED;
}

void mel_widget_set_position(Mel_Widget* widget, Mel_Vec2 pos)
{
    assert(widget != nullptr);
    widget->pos = pos;
}

void mel_widget_set_size(Mel_Widget* widget, Mel_Vec2 sz)
{
    assert(widget != nullptr);
    widget->size = sz;
}

void mel_widget_draw(Mel_Widget* root, void* ctx)
{
    assert(root != nullptr);
    if (!root->visible) return;

    if (root->draw)
        root->draw(root, ctx);

    for (Mel_Widget* child = root->first_child; child; child = child->next_sibling)
        mel_widget_draw(child, ctx);
}

bool mel_widget_contains(Mel_Widget* w, Mel_Vec2 pos)
{
    return pos.x >= w->pos.x && pos.x < w->pos.x + w->size.x &&
           pos.y >= w->pos.y && pos.y < w->pos.y + w->size.y;
}

Mel_Widget* mel_widget_hit_test(Mel_Widget* root, Mel_Vec2 pos)
{
    if (!root->visible || !root->enabled) return nullptr;
    if (!mel_widget_contains(root, pos)) return nullptr;

    for (Mel_Widget* child = root->first_child; child; child = child->next_sibling) {
        Mel_Widget* hit = mel_widget_hit_test(child, pos);
        if (hit) return hit;
    }

    return root;
}

bool mel_widget_mouse_down(Mel_Widget* root, Mel_Vec2 pos, i32 button)
{
    if (!root->visible || !root->enabled) return false;

    for (Mel_Widget* child = root->first_child; child; child = child->next_sibling) {
        if (mel_widget_mouse_down(child, pos, button)) return true;
    }

    if (mel_widget_contains(root, pos) && root->on_mouse_down)
        return root->on_mouse_down(root, pos, button);

    return false;
}

bool mel_widget_mouse_up(Mel_Widget* root, Mel_Vec2 pos, i32 button)
{
    if (!root->visible || !root->enabled) return false;

    for (Mel_Widget* child = root->first_child; child; child = child->next_sibling) {
        if (mel_widget_mouse_up(child, pos, button)) return true;
    }

    if (root->on_mouse_up)
        return root->on_mouse_up(root, pos, button);

    return false;
}

bool mel_widget_mouse_move(Mel_Widget* root, Mel_Vec2 pos)
{
    if (!root->visible || !root->enabled) return false;

    for (Mel_Widget* child = root->first_child; child; child = child->next_sibling) {
        if (mel_widget_mouse_move(child, pos)) return true;
    }

    if (root->on_mouse_move)
        return root->on_mouse_move(root, pos);

    return false;
}
