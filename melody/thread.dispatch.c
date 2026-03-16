#include "thread.dispatch.h"
#include "collection.mpmc.h"
#include "allocator.heap.h"

#include <SDL3/SDL.h>

typedef struct {
    Mel_Main_Dispatch_Fn fn;
    void* data;
    void* result;
    SDL_Semaphore* done;
} Mel__Dispatch_Item;

static SDL_ThreadID s_main_thread_id;
static Mel_Mpmc s_queue;
static bool s_initialized;

void mel__main_dispatch_init(void)
{
    assert(!s_initialized);
    s_main_thread_id = SDL_GetCurrentThreadID();
    mel_mpmc_init(&s_queue, 32, mel_alloc_heap());
    s_initialized = true;
}

void mel__main_dispatch_shutdown(void)
{
    if (!s_initialized) return;
    mel_mpmc_free(&s_queue);
    s_initialized = false;
}

void mel__main_dispatch_drain(void)
{
    void* raw;
    while (mel_mpmc_pop(&s_queue, &raw))
    {
        Mel__Dispatch_Item* item = raw;
        item->result = item->fn(item->data);
        SDL_SignalSemaphore(item->done);
    }
}

bool mel__is_main_thread(void)
{
    return SDL_GetCurrentThreadID() == s_main_thread_id;
}

void* mel__main_dispatch_sync(Mel_Main_Dispatch_Fn fn, void* data)
{
    if (mel__is_main_thread())
        return fn(data);

    assert(s_initialized);

    SDL_Semaphore* done = SDL_CreateSemaphore(0);
    Mel__Dispatch_Item item = {
        .fn = fn,
        .data = data,
        .done = done,
    };

    bool pushed = mel_mpmc_push(&s_queue, &item);
    assert(pushed);

    SDL_WaitSemaphore(done);
    SDL_DestroySemaphore(done);

    return item.result;
}
