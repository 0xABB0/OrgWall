#include "editor.registry.h"
#include "collection.array.h"
#include "string.str8.h"

#include <cimgui/cimgui.h>
#include <stdio.h>
#include <SDL3/SDL.h>

void mel_ed_registry_init_opt(Mel_EdRegistry* reg, Mel_EdRegistry_Init_Opt opt)
{
    assert(reg != nullptr);
    assert(opt.alloc != nullptr);

    mel_array_init(&reg->entries, opt.alloc);
    reg->next_id = 1;
}

void mel_ed_registry_shutdown(Mel_EdRegistry* reg)
{
    assert(reg != nullptr);

    for (usize i = 0; i < reg->entries.count; i++)
    {
        Mel_EdEntry* entry = &reg->entries.items[i];
        if (entry->shutdown)
        {
            entry->shutdown(entry->data);
        }
    }

    mel_array_free(&reg->entries);
    reg->next_id = 0;
}

Mel_EdEntry* mel_ed_registry_add_opt(Mel_EdRegistry* reg, Mel_EdRegistry_Add_Opt opt)
{
    assert(reg != nullptr);
    assert(!str8_is_empty(opt.name));
    assert(opt.draw != nullptr);

    Mel_EdEntry entry = {
        .name = opt.name,
        .data = opt.data,
        .id = reg->next_id++,
        .open = true,
        .draw = opt.draw,
        .shutdown = opt.shutdown,
        .event = opt.event,
    };

    char name_buf[128];
    str8_to_buf(opt.name, name_buf, sizeof(name_buf));
    snprintf(entry.window_title, sizeof(entry.window_title), "%s##ed%u", name_buf, entry.id);

    mel_array_push(&reg->entries, entry);

    return &reg->entries.items[reg->entries.count - 1];
}

void mel_ed_registry_remove(Mel_EdRegistry* reg, Mel_EdEntry* entry)
{
    assert(reg != nullptr);
    assert(entry != nullptr);

    if (entry->shutdown)
    {
        entry->shutdown(entry->data);
    }

    usize idx = (usize)(entry - reg->entries.items);
    assert(idx < reg->entries.count);

    mel_array_remove_unordered(&reg->entries, idx);
}

void mel_ed_registry_draw(Mel_EdRegistry* reg, f32 dt)
{
    assert(reg != nullptr);

    for (usize i = 0; i < reg->entries.count; i++)
    {
        Mel_EdEntry* entry = &reg->entries.items[i];

        if (igBegin(entry->window_title, &entry->open, 0))
        {
            entry->draw(entry->data, dt);
        }
        igEnd();
    }

    for (usize i = reg->entries.count; i > 0; i--)
    {
        Mel_EdEntry* entry = &reg->entries.items[i - 1];
        if (!entry->open)
        {
            if (entry->shutdown)
            {
                entry->shutdown(entry->data);
            }
            mel_array_remove_unordered(&reg->entries, i - 1);
        }
    }
}

void mel_ed_registry_process_event(Mel_EdRegistry* reg, SDL_Event* event)
{
    assert(reg != nullptr);

    for (usize i = 0; i < reg->entries.count; i++)
    {
        Mel_EdEntry* entry = &reg->entries.items[i];
        if (entry->event)
        {
            entry->event(entry->data, event);
        }
    }
}

usize mel_ed_registry_count(Mel_EdRegistry* reg)
{
    assert(reg != nullptr);
    return reg->entries.count;
}

Mel_EdEntry* mel_ed_registry_at(Mel_EdRegistry* reg, usize idx)
{
    assert(reg != nullptr);
    assert(idx < reg->entries.count);
    return &reg->entries.items[idx];
}
