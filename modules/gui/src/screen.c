#include "gui_internal.h"

#include <collection.array/array.h>

static Mel_Array(Mel_Screen_Def) g_screens;

const Mel_Screen_Def* mel_gui__screen_find(str8 name)
{
    for (usize i = 0; i < g_screens.count; i++) {
        if (str8_equals(g_screens.items[i].name, name)) return &g_screens.items[i];
    }
    return NULL;
}

void mel_gui__screens_reset(void)
{
    mel_array_free(&g_screens);
}

void mel_app_register_screen_opt(str8 name, Mel_Screen_Opt opt)
{
    assert(opt.build && "screen builder must not be null");
    assert(!mel_gui__screen_find(name) && "screen name already registered");

    if (g_screens.allocator == NULL) mel_array_init(&g_screens, mel_gui__alloc());

    mel_array_push(&g_screens, ((Mel_Screen_Def){
        .name         = name,
        .build        = opt.build,
        .default_user = opt.user,
        .on_enter     = opt.on_enter,
        .on_leave     = opt.on_leave,
        .on_destroy   = opt.on_destroy,
    }));
}
