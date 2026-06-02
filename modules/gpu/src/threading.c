#include <gpu/threading.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <thread/thread.h>
#include <thread/mutex.h>
#include <debug/assert.h>
#include <log/log.h>

typedef struct
{
    const void*         object;
    Mel_Thread_Id       owner;
    Mel_Gpu_Concurrency cls;
    u32                 depth;
} Mel_Gpu_Tracker_Entry;

struct Mel_Gpu_Thread_Tracker
{
    Mel_Mutex              lock;
    Mel_Gpu_Tracker_Entry* entries;
    u32                    count;
    u32                    capacity;
};

Mel_Gpu_Thread_Tracker* mel_gpu_thread_tracker_create(void)
{
    const Mel_Alloc*        a = mel_alloc_heap();
    Mel_Gpu_Thread_Tracker* t = mel_alloc_type(a, Mel_Gpu_Thread_Tracker);
    t->entries = NULL;
    t->count = 0;
    t->capacity = 0;
    mel_mutex_init(&t->lock, MEL_MUTEX_PLAIN);
    return t;
}

void mel_gpu_thread_tracker_destroy(Mel_Gpu_Thread_Tracker* t)
{
    if (!t)
        return;
    mel_mutex_destroy(&t->lock);
    const Mel_Alloc* a = mel_alloc_heap();
    if (t->entries)
        mel_dealloc(a, t->entries);
    mel_dealloc(a, t);
}

static Mel_Gpu_Tracker_Entry* mel_gpu__tracker_find(Mel_Gpu_Thread_Tracker* t, const void* object)
{
    for (u32 i = 0; i < t->count; i++)
        if (t->entries[i].object == object)
            return &t->entries[i];
    return NULL;
}

void mel_gpu_thread_tracker_enter(Mel_Gpu_Thread_Tracker* t, const void* object, Mel_Gpu_Concurrency cls)
{
    if (!t || cls == MEL_GPU_CONCURRENCY_CONCURRENT)
        return;

    mel_mutex_lock(&t->lock);

    Mel_Thread_Id          self = mel_thread_current_id();
    Mel_Gpu_Tracker_Entry* e = mel_gpu__tracker_find(t, object);

    if (e)
    {
        // §3.7 / U21: a SerializedPerObject object entered by a second thread without an intervening retirement
        // is a thread-safety contract violation — the single most useful porting-from-single-thread diagnostic.
        // BUG-2: it is REPORTED loudly (not asserted-as-control) so it can fire from wired public call paths
        // without taking the whole process (and the MEL_TEST_NOFORK runner) down — the foreign call is named,
        // the existing owner's depth is left untouched (we do not corrupt the ledger on the violating thread).
        // Same-thread re-entry is legal (recursive depth); only a foreign owner is the violation.
        if (!mel_thread_id_equal(e->owner, self))
        {
            mel_log_error("gpu", "thread-safety violation (§3.7): object %p is SerializedPerObject and owned by another thread; "
                                 "a second thread entered it without an intervening retirement (concurrent use of the same object)",
                          object);
            mel_mutex_unlock(&t->lock);
            return;
        }
        e->depth++;
        mel_mutex_unlock(&t->lock);
        return;
    }

    if (t->count == t->capacity)
    {
        u32              new_cap = t->capacity ? t->capacity * 2 : 16;
        const Mel_Alloc* a = mel_alloc_heap();
        t->entries = t->entries ? mel_realloc(a, t->entries, sizeof(Mel_Gpu_Tracker_Entry) * new_cap) : mel_alloc(a, sizeof(Mel_Gpu_Tracker_Entry) * new_cap);
        t->capacity = new_cap;
    }

    t->entries[t->count++] = (Mel_Gpu_Tracker_Entry){ .object = object, .owner = self, .cls = cls, .depth = 1 };
    mel_mutex_unlock(&t->lock);
}

void mel_gpu_thread_tracker_exit(Mel_Gpu_Thread_Tracker* t, const void* object)
{
    if (!t)
        return;

    mel_mutex_lock(&t->lock);
    Mel_Gpu_Tracker_Entry* e = mel_gpu__tracker_find(t, object);
    if (e)
    {
        if (--e->depth == 0)
        {
            *e = t->entries[--t->count];
        }
    }
    mel_mutex_unlock(&t->lock);
}
