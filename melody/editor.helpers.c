#include "editor.helpers.h"

#include <cimgui/cimgui.h>

void mel_ed_draw_vec2(const char* label, Mel_Vec2* v, bool* changed)
{
    f32 values[2] = {v->x, v->y};
    if (igDragFloat2(label, values, 0.1f, 0, 0, "%.2f", 0))
    {
        v->x = values[0];
        v->y = values[1];
        *changed = true;
    }
}

void mel_ed_draw_vec4(const char* label, Mel_Vec4* v, bool* changed)
{
    f32 values[4] = {v->x, v->y, v->z, v->w};
    if (igColorEdit4(label, values, 0))
    {
        v->x = values[0];
        v->y = values[1];
        v->z = values[2];
        v->w = values[3];
        *changed = true;
    }
}
