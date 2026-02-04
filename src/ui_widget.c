#include "ui_widget.h"

void mel_widget_init(Mel_Widget* w, const Mel_Widget_VTable* vtable, i32 type)
{
    assert(w != nullptr);

    *w = (Mel_Widget){0};
    w->vtable = vtable;
    w->type = type;
    w->visible = true;
    w->enabled = true;
    w->color = mel_vec4(1, 1, 1, 1);
}

void mel_widget_destroy(Mel_Widget* w)
{
    assert(w != nullptr);

    Mel_Widget* child = w->first_child;
    while (child)
    {
        Mel_Widget* next = child->next_sibling;
        mel_widget_destroy(child);
        child = next;
    }

    if (w->vtable && w->vtable->destroy)
    {
        w->vtable->destroy(w);
    }
}

void mel_widget_add_child(Mel_Widget* parent, Mel_Widget* child)
{
    assert(parent != nullptr);
    assert(child != nullptr);
    assert(child->parent == nullptr);

    child->parent = parent;
    child->next_sibling = nullptr;

    if (!parent->first_child)
    {
        parent->first_child = child;
    }
    else
    {
        Mel_Widget* last = parent->first_child;
        while (last->next_sibling)
        {
            last = last->next_sibling;
        }
        last->next_sibling = child;
    }
}

void mel_widget_remove_child(Mel_Widget* parent, Mel_Widget* child)
{
    assert(parent != nullptr);
    assert(child != nullptr);
    assert(child->parent == parent);

    if (parent->first_child == child)
    {
        parent->first_child = child->next_sibling;
    }
    else
    {
        Mel_Widget* prev = parent->first_child;
        while (prev && prev->next_sibling != child)
        {
            prev = prev->next_sibling;
        }
        if (prev)
        {
            prev->next_sibling = child->next_sibling;
        }
    }

    child->parent = nullptr;
    child->next_sibling = nullptr;
}

void mel_widget_layout(Mel_Widget* w)
{
    assert(w != nullptr);

    if (w->vtable && w->vtable->layout)
    {
        w->vtable->layout(w);
    }

    for (Mel_Widget* child = w->first_child; child; child = child->next_sibling)
    {
        mel_widget_layout(child);
    }
}

void mel_widget_draw(Mel_Widget* w, Mel_SpriteBatch* batch)
{
    assert(w != nullptr);

    if (!w->visible) return;

    if (w->vtable && w->vtable->draw)
    {
        w->vtable->draw(w, batch);
    }

    for (Mel_Widget* child = w->first_child; child; child = child->next_sibling)
    {
        mel_widget_draw(child, batch);
    }
}

bool mel_widget_contains(Mel_Widget* w, Mel_Vec2 pos)
{
    f32 x = w->pos.x;
    f32 y = w->pos.y;
    return pos.x >= x && pos.x < x + w->size.x &&
           pos.y >= y && pos.y < y + w->size.y;
}

Mel_Widget* mel_widget_hit_test(Mel_Widget* w, Mel_Vec2 pos)
{
    if (!w->visible || !w->enabled) return nullptr;
    if (!mel_widget_contains(w, pos)) return nullptr;

    for (Mel_Widget* child = w->first_child; child; child = child->next_sibling)
    {
        Mel_Widget* hit = mel_widget_hit_test(child, pos);
        if (hit) return hit;
    }

    return w;
}

bool mel_widget_mouse_down(Mel_Widget* w, Mel_Vec2 pos, i32 button)
{
    if (!w->visible || !w->enabled) return false;

    for (Mel_Widget* child = w->first_child; child; child = child->next_sibling)
    {
        if (mel_widget_mouse_down(child, pos, button)) return true;
    }

    if (mel_widget_contains(w, pos) && w->vtable && w->vtable->on_mouse_down)
    {
        return w->vtable->on_mouse_down(w, pos, button);
    }

    return false;
}

bool mel_widget_mouse_up(Mel_Widget* w, Mel_Vec2 pos, i32 button)
{
    if (!w->visible || !w->enabled) return false;

    for (Mel_Widget* child = w->first_child; child; child = child->next_sibling)
    {
        if (mel_widget_mouse_up(child, pos, button)) return true;
    }

    if (mel_widget_contains(w, pos) && w->vtable && w->vtable->on_mouse_up)
    {
        return w->vtable->on_mouse_up(w, pos, button);
    }

    return false;
}

bool mel_widget_mouse_move(Mel_Widget* w, Mel_Vec2 pos)
{
    if (!w->visible || !w->enabled) return false;

    for (Mel_Widget* child = w->first_child; child; child = child->next_sibling)
    {
        if (mel_widget_mouse_move(child, pos)) return true;
    }

    if (w->vtable && w->vtable->on_mouse_move)
    {
        return w->vtable->on_mouse_move(w, pos);
    }

    return false;
}
