#include "input.stack.h"
#include "allocator.h"
#include "allocator.heap.h"

void mel_input_stack_init_opt(Mel_Input_Stack* stack, Mel_Input_Stack_Opt opt)
{
    assert(stack != nullptr);

    *stack = (Mel_Input_Stack){0};
    stack->alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
}

void mel_input_stack_shutdown(Mel_Input_Stack* stack)
{
    assert(stack != nullptr);

    for (u32 i = 0; i < stack->count; i++)
        mel_dealloc(stack->alloc, stack->layers[i]);

    if (stack->layers)
        mel_dealloc(stack->alloc, stack->layers);

    *stack = (Mel_Input_Stack){0};
}

Mel_Input_Layer* mel_input_stack_push_opt(Mel_Input_Stack* stack, Mel_Input_Layer_Desc desc)
{
    assert(stack != nullptr);
    assert(desc.mapper != nullptr);
    assert(desc.on_action != nullptr);

    if (stack->count >= stack->capacity)
    {
        u32 new_cap = stack->capacity == 0 ? 4 : stack->capacity * 2;
        Mel_Input_Layer** new_layers = mel_alloc(stack->alloc, sizeof(Mel_Input_Layer*) * new_cap);

        if (stack->layers)
        {
            for (u32 i = 0; i < stack->count; i++)
                new_layers[i] = stack->layers[i];
            mel_dealloc(stack->alloc, stack->layers);
        }

        stack->layers = new_layers;
        stack->capacity = new_cap;
    }

    Mel_Input_Layer* layer = mel_alloc(stack->alloc, sizeof(Mel_Input_Layer));
    *layer = (Mel_Input_Layer){
        .mapper = desc.mapper,
        .mapper_user = desc.mapper_user,
        .on_action = desc.on_action,
        .user = desc.user,
        .opaque = desc.opaque,
    };

    stack->layers[stack->count++] = layer;
    return layer;
}

void mel_input_stack_remove(Mel_Input_Stack* stack, Mel_Input_Layer* layer)
{
    assert(stack != nullptr);
    assert(layer != nullptr);

    for (u32 i = 0; i < stack->count; i++)
    {
        if (stack->layers[i] == layer)
        {
            mel_dealloc(stack->alloc, layer);

            for (u32 j = i; j < stack->count - 1; j++)
                stack->layers[j] = stack->layers[j + 1];

            stack->count--;
            return;
        }
    }

    assert(false);
}

void mel_input_stack_dispatch(Mel_Input_Stack* stack, SDL_Event* event)
{
    assert(stack != nullptr);
    assert(event != nullptr);

    for (u32 i = stack->count; i > 0; i--)
    {
        Mel_Input_Layer* layer = stack->layers[i - 1];

        Mel_Input_Map_Output output = layer->mapper(event, layer->mapper_user);

        bool consumed = false;
        for (u32 j = 0; j < output.count; j++)
        {
            if (layer->on_action(output.results[j].action, output.results[j].value, layer->user))
                consumed = true;
        }

        if (consumed || layer->opaque)
            return;
    }
}
