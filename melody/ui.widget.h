#pragma once

#include "core.types.h"
#include "math.vec2.h"
#include "math.geo.rect.h"
#include "ui.layout.h"
#include "ui.widget.fwd.h"
#include <SDL3/SDL_events.h>

#define MEL_WIDGET_STATE_HOVERED  (1 << 0)
#define MEL_WIDGET_STATE_PRESSED  (1 << 1)
#define MEL_WIDGET_STATE_FOCUSED  (1 << 2)
#define MEL_WIDGET_STATE_DISABLED (1 << 3)

typedef void     (*Mel_Widget_Draw_Fn)(Mel_Widget* w, void* ctx);
typedef Mel_Vec2 (*Mel_Widget_Measure_Fn)(Mel_Widget* w);
typedef bool     (*Mel_Widget_Mouse_Fn)(Mel_Widget* w, Mel_Vec2 pos, i32 button);
typedef bool     (*Mel_Widget_Move_Fn)(Mel_Widget* w, Mel_Vec2 pos);
typedef bool     (*Mel_Widget_Key_Fn)(Mel_Widget* w, const SDL_KeyboardEvent* event);

struct Mel_Widget {
    Mel_Layoutable layoutable;

    Mel_Widget* parent;
    Mel_Widget* first_child;
    Mel_Widget* next_sibling;

    Mel_Layout* layout;

    Mel_Vec2 pos;
    Mel_Vec2 size;
    Mel_Vec2 fixed_size;

    u32 flags;
    u32 state;

    bool visible;
    bool enabled;

    i32 type_tag;
    void* user_data;

    Mel_Widget_Draw_Fn    draw;
    Mel_Widget_Measure_Fn measure;
    void (*on_destroy)(Mel_Widget* w);
    Mel_Widget_Mouse_Fn   on_mouse_down;
    Mel_Widget_Mouse_Fn   on_mouse_up;
    Mel_Widget_Move_Fn    on_mouse_move;
    Mel_Widget_Key_Fn     on_key_down;
};

void mel_widget_init(Mel_Widget* widget);
void mel_widget_destroy(Mel_Widget* widget);
void mel_widget_add_child(Mel_Widget* parent, Mel_Widget* child);
void mel_widget_remove_child(Mel_Widget* parent, Mel_Widget* child);
void mel_widget_set_layout(Mel_Widget* widget, Mel_Layout* layout);
void mel_widget_perform_layout(Mel_Widget* widget);
void mel_widget_set_visible(Mel_Widget* widget, bool visible);
void mel_widget_set_enabled(Mel_Widget* widget, bool enabled);
void mel_widget_set_position(Mel_Widget* widget, Mel_Vec2 pos);
void mel_widget_set_size(Mel_Widget* widget, Mel_Vec2 size);

void mel_widget_draw(Mel_Widget* root, void* ctx);
bool mel_widget_contains(Mel_Widget* w, Mel_Vec2 pos);
Mel_Widget* mel_widget_hit_test(Mel_Widget* root, Mel_Vec2 pos);
bool mel_widget_mouse_down(Mel_Widget* root, Mel_Vec2 pos, i32 button);
bool mel_widget_mouse_up(Mel_Widget* root, Mel_Vec2 pos, i32 button);
bool mel_widget_mouse_move(Mel_Widget* root, Mel_Vec2 pos);
bool mel_widget_key_down(Mel_Widget* root, const SDL_KeyboardEvent* event);
