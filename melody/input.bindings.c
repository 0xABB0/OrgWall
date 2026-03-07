#include "input.bindings.h"
#include "input.stack.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <string.h>

void mel_input_bindings_init_opt(Mel_Input_Bindings* b, Mel_Input_Bindings_Opt opt)
{
    assert(b != nullptr);

    *b = (Mel_Input_Bindings){0};
    b->alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    if (opt.bindings && opt.binding_count > 0)
    {
        b->capacity = opt.binding_count;
        b->entries = mel_alloc(b->alloc, sizeof(Mel_Input_Binding) * b->capacity);
        memcpy(b->entries, opt.bindings, sizeof(Mel_Input_Binding) * opt.binding_count);
        b->count = opt.binding_count;
    }
}

void mel_input_bindings_shutdown(Mel_Input_Bindings* b)
{
    assert(b != nullptr);

    if (b->entries)
        mel_dealloc(b->alloc, b->entries);

    *b = (Mel_Input_Bindings){0};
}

void mel_input_bindings_add(Mel_Input_Bindings* b, SDL_Scancode key, Mel_Input_Action action)
{
    assert(b != nullptr);

    for (u32 i = 0; i < b->count; i++)
    {
        if (b->entries[i].key == key)
        {
            b->entries[i].action = action;
            return;
        }
    }

    if (b->count >= b->capacity)
    {
        u32 new_cap = b->capacity == 0 ? 8 : b->capacity * 2;
        Mel_Input_Binding* new_entries = mel_alloc(b->alloc, sizeof(Mel_Input_Binding) * new_cap);

        if (b->entries)
        {
            memcpy(new_entries, b->entries, sizeof(Mel_Input_Binding) * b->count);
            mel_dealloc(b->alloc, b->entries);
        }

        b->entries = new_entries;
        b->capacity = new_cap;
    }

    b->entries[b->count++] = (Mel_Input_Binding){ .key = key, .action = action };
}

void mel_input_bindings_remove_key(Mel_Input_Bindings* b, SDL_Scancode key)
{
    assert(b != nullptr);

    for (u32 i = 0; i < b->count; i++)
    {
        if (b->entries[i].key == key)
        {
            b->entries[i] = b->entries[b->count - 1];
            b->count--;
            return;
        }
    }
}

void mel_input_bindings_remove_action(Mel_Input_Bindings* b, Mel_Input_Action action)
{
    assert(b != nullptr);

    for (u32 i = b->count; i > 0; i--)
    {
        if (b->entries[i - 1].action == action)
        {
            b->entries[i - 1] = b->entries[b->count - 1];
            b->count--;
        }
    }
}

SDL_Scancode mel_input_bindings_get_key(Mel_Input_Bindings* b, Mel_Input_Action action)
{
    assert(b != nullptr);

    for (u32 i = 0; i < b->count; i++)
    {
        if (b->entries[i].action == action)
            return b->entries[i].key;
    }

    return SDL_SCANCODE_UNKNOWN;
}

Mel_Input_Map_Output mel_input_mapper_keyboard(SDL_Event* event, void* user)
{
    Mel_Input_Map_Output out = {0};
    Mel_Input_Bindings* b = (Mel_Input_Bindings*)user;

    if (event->type != SDL_EVENT_KEY_DOWN && event->type != SDL_EVENT_KEY_UP)
        return out;

    if (event->key.repeat)
        return out;

    SDL_Scancode scancode = event->key.scancode;
    f32 value = (event->type == SDL_EVENT_KEY_DOWN) ? 1.0f : 0.0f;

    for (u32 i = 0; i < b->count && out.count < MEL_INPUT_MAP_MAX; i++)
    {
        if (b->entries[i].key == scancode)
        {
            out.results[out.count++] = (Mel_Input_Mapped){
                .action = b->entries[i].action,
                .value = value,
            };
        }
    }

    return out;
}
