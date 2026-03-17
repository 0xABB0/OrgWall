#include "render.view.registry.h"
#include "render.viewport.h"
#include "collection.array.h"
#include "allocator.heap.h"

#include <assert.h>

typedef Mel_Array(Mel_Render_View*) View_Array;

static View_Array s_views;

__attribute__((constructor))
static void mel__view_registry_init(void)
{
    mel_array_init(&s_views, mel_alloc_heap());
}

__attribute__((destructor))
static void mel__view_registry_shutdown(void)
{
    mel_array_free(&s_views);
}

void mel__view_registry_add(Mel_Render_View* view)
{
    assert(view != nullptr);
    mel_array_push(&s_views, view);
}

void mel__view_registry_remove(Mel_Render_View* view)
{
    assert(view != nullptr);

    for (usize i = 0; i < s_views.count; i++)
    {
        if (s_views.items[i] == view)
        {
            mel_array_remove_unordered(&s_views, i);
            return;
        }
    }

    assert(false);
}

u32 mel__view_registry_count(void)
{
    return (u32)s_views.count;
}

Mel_Render_View* mel__view_registry_at(u32 index)
{
    assert(index < (u32)s_views.count);
    return s_views.items[index];
}
