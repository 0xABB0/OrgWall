#include <allocator/leak.h>
#include <allocator/tracking.h>
#include <allocator/heap.h>
#include <allocator/allocator.h>

#include <stdatomic.h>

typedef struct
{
    atomic_uint         init_state;
    Mel_Track_Allocator tracker;
    Mel_Alloc           iface;
} Mel_Leak_State;

static Mel_Leak_State s_leak = { .init_state = 0 };

static void mel__leak_init_once(void)
{
    u32 expected = 0;
    if (atomic_compare_exchange_strong_explicit(&s_leak.init_state, &expected, 1, memory_order_acq_rel, memory_order_acquire))
    {
        mel_track_init(&s_leak.tracker, (Mel_Track_Allocator_Opt){ .backing = mel_alloc_heap() });
        s_leak.iface = mel_track_allocator(&s_leak.tracker);
        atomic_store_explicit(&s_leak.init_state, 2, memory_order_release);
        return;
    }
    while (atomic_load_explicit(&s_leak.init_state, memory_order_acquire) != 2)
    {
    }
}

const Mel_Alloc* mel_alloc_leak_detect(void)
{
    mel__leak_init_once();
    return &s_leak.iface;
}

typedef struct
{
    Mel_Leak_Report_Cb cb;
    void*              user_data;
} Mel__Leak_Adapter;

static void mel__leak_record_cb(const Mel_Track_Record* rec, void* user_data)
{
    Mel__Leak_Adapter* a = (Mel__Leak_Adapter*)user_data;
    a->cb(rec->file, rec->func, rec->line, rec->size, a->user_data);
}

void mel_leak_dump(Mel_Leak_Report_Cb cb, void* user_data)
{
    mel__leak_init_once();
    Mel__Leak_Adapter adapter = { .cb = cb, .user_data = user_data };
    mel_track_dump_live(&s_leak.tracker, mel__leak_record_cb, &adapter);
}
