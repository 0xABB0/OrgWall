#include "ui.layout.h"

Mel_Vec2 mel_layout_preferred_size(Mel_Layout* layout, Mel_Layoutable* container)
{
    assert(layout != nullptr);
    assert(layout->vtable != nullptr);
    assert(layout->vtable->preferred_size != nullptr);
    return layout->vtable->preferred_size(layout, container);
}

void mel_layout_perform(Mel_Layout* layout, Mel_Layoutable* container)
{
    assert(layout != nullptr);
    assert(layout->vtable != nullptr);
    assert(layout->vtable->perform_layout != nullptr);
    layout->vtable->perform_layout(layout, container);
}
