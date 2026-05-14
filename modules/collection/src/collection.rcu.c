#include <collection.rcu/collection.rcu.h>
#include <allocator/allocator.h>
#include <SDL3/SDL.h>
#include <stdatomic.h>

struct Mel__Rcu_Garbage {
    void* ptr;
    u32 bucket;
    Mel__Rcu_Garbage* next;
};

static void mel__rcu_collect(Mel_Rcu* rcu)
{
    Mel__Rcu_Garbage** pp = &rcu->garbage;
    while (*pp) {
        Mel__Rcu_Garbage* node = *pp;
        if (atomic_load_explicit(&rcu->readers[node->bucket], memory_order_acquire) == 0) {
            *pp = node->next;
            mel_dealloc(rcu->alloc, node->ptr);
            SDL_free(node);
        } else {
            pp = &node->next;
        }
    }
}

static void mel__rcu_retire(Mel_Rcu* rcu, void* old_ptr, u32 bucket)
{
    Mel__Rcu_Garbage* node = SDL_malloc(sizeof(Mel__Rcu_Garbage));
    node->ptr = old_ptr;
    node->bucket = bucket;
    node->next = rcu->garbage;
    rcu->garbage = node;
}

void mel_rcu_init(Mel_Rcu* rcu, const Mel_Alloc* alloc)
{
    assert(rcu != NULL);
    assert(alloc != NULL);
    atomic_store_explicit(&rcu->ptr, NULL, memory_order_relaxed);
    atomic_store_explicit(&rcu->epoch, 0, memory_order_relaxed);
    atomic_store_explicit(&rcu->readers[0], 0, memory_order_relaxed);
    atomic_store_explicit(&rcu->readers[1], 0, memory_order_relaxed);
    rcu->writer_lock = SDL_CreateMutex();
    assert(rcu->writer_lock != NULL);
    rcu->garbage = NULL;
    rcu->alloc = alloc;
}

void mel_rcu_destroy(Mel_Rcu* rcu)
{
    assert(rcu != NULL);
    assert(atomic_load_explicit(&rcu->readers[0], memory_order_acquire) == 0);
    assert(atomic_load_explicit(&rcu->readers[1], memory_order_acquire) == 0);

    mel__rcu_collect(rcu);
    assert(rcu->garbage == NULL);

    void* current = atomic_load_explicit(&rcu->ptr, memory_order_relaxed);
    if (current)
        mel_dealloc(rcu->alloc, current);

    SDL_DestroyMutex(rcu->writer_lock);
    rcu->writer_lock = NULL;
    rcu->alloc = NULL;
}

void* mel_rcu_read(Mel_Rcu* rcu, Mel_Rcu_Token* token)
{
    assert(rcu != NULL);
    assert(token != NULL);

    u32 e = atomic_load_explicit(&rcu->epoch, memory_order_acquire);
    token->_bucket = e & 1;
    atomic_fetch_add_explicit(&rcu->readers[token->_bucket], 1, memory_order_acq_rel);

    return atomic_load_explicit(&rcu->ptr, memory_order_acquire);
}

void mel_rcu_read_end(Mel_Rcu* rcu, Mel_Rcu_Token token)
{
    assert(rcu != NULL);
    atomic_fetch_sub_explicit(&rcu->readers[token._bucket], 1, memory_order_release);
}

void mel_rcu_writer_lock(Mel_Rcu* rcu)
{
    assert(rcu != NULL);
    SDL_LockMutex(rcu->writer_lock);
    mel__rcu_collect(rcu);
}

void mel_rcu_writer_unlock(Mel_Rcu* rcu)
{
    assert(rcu != NULL);
    SDL_UnlockMutex(rcu->writer_lock);
}

void* mel_rcu_writer_load(Mel_Rcu* rcu)
{
    assert(rcu != NULL);
    return atomic_load_explicit(&rcu->ptr, memory_order_relaxed);
}

void mel_rcu_writer_store(Mel_Rcu* rcu, void* new_ptr)
{
    assert(rcu != NULL);

    void* old = atomic_load_explicit(&rcu->ptr, memory_order_relaxed);
    atomic_store_explicit(&rcu->ptr, new_ptr, memory_order_release);

    if (old) {
        u32 prev = atomic_fetch_add_explicit(&rcu->epoch, 1, memory_order_acq_rel);
        u32 bucket = prev & 1;

        if (atomic_load_explicit(&rcu->readers[bucket], memory_order_acquire) == 0)
            mel_dealloc(rcu->alloc, old);
        else
            mel__rcu_retire(rcu, old, bucket);
    }
}
