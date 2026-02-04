#ifndef MEL_UI_WIDGET_H
#define MEL_UI_WIDGET_H

#include "types.h"
#include "vec2.h"
#include "vec4.h"
#include "sprite_batch.h"

#define MEL_WIDGET_TYPE_BASE    0
#define MEL_WIDGET_TYPE_PANEL   1
#define MEL_WIDGET_TYPE_LABEL   2
#define MEL_WIDGET_TYPE_BUTTON  3

typedef struct Mel_Widget Mel_Widget;
typedef struct Mel_Widget_VTable Mel_Widget_VTable;

struct Mel_Widget_VTable
{
    void (*destroy)(Mel_Widget* w);
    Mel_Vec2 (*preferred_size)(Mel_Widget* w);
    void (*layout)(Mel_Widget* w);
    void (*draw)(Mel_Widget* w, Mel_SpriteBatch* batch);
    bool (*on_mouse_down)(Mel_Widget* w, Mel_Vec2 pos, i32 button);
    bool (*on_mouse_up)(Mel_Widget* w, Mel_Vec2 pos, i32 button);
    bool (*on_mouse_move)(Mel_Widget* w, Mel_Vec2 pos);
};

struct Mel_Widget
{
    const Mel_Widget_VTable* vtable;
    i32 type;

    Mel_Widget* parent;
    Mel_Widget* first_child;
    Mel_Widget* next_sibling;

    Mel_Vec2 pos;
    Mel_Vec2 size;
    Mel_Vec4 color;

    bool visible;
    bool enabled;
};

void mel_widget_init(Mel_Widget* w, const Mel_Widget_VTable* vtable, i32 type);
void mel_widget_destroy(Mel_Widget* w);

void mel_widget_add_child(Mel_Widget* parent, Mel_Widget* child);
void mel_widget_remove_child(Mel_Widget* parent, Mel_Widget* child);

void mel_widget_layout(Mel_Widget* w);
void mel_widget_draw(Mel_Widget* w, Mel_SpriteBatch* batch);

bool mel_widget_contains(Mel_Widget* w, Mel_Vec2 pos);
Mel_Widget* mel_widget_hit_test(Mel_Widget* w, Mel_Vec2 pos);

bool mel_widget_mouse_down(Mel_Widget* w, Mel_Vec2 pos, i32 button);
bool mel_widget_mouse_up(Mel_Widget* w, Mel_Vec2 pos, i32 button);
bool mel_widget_mouse_move(Mel_Widget* w, Mel_Vec2 pos);

#endif
