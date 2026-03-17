#include "ui.widget.panel.h"

static void wpanel_draw(Mel_Widget* w, void* ctx)
{
    Mel_WPanel* panel = (Mel_WPanel*)w;
    Mel_Render_List* list = (Mel_Render_List*)ctx;

    Mel_Sprite_Entry* e = mel_render_list_push(list, mel_sort_key_sprite(0, 0.0f, 0, 0));
    *e = (Mel_Sprite_Entry){
        .pos = w->pos,
        .size = w->size,
        .uv = MEL_UV_FULL,
        .color = panel->color,
        .tex = MEL_TEXTURE_HANDLE_NULL,
    };
}

void mel_wpanel_init(Mel_WPanel* panel)
{
    assert(panel != nullptr);
    mel_widget_init(&panel->base);
    panel->base.draw = wpanel_draw;
    panel->color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
}
