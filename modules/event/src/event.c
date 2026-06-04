#include <event/event.h>

#include <executor/executor.h>
#include <allocator/allocator.h>
#include <collection/list.h>
#include <collection/slotmap.h>
#include <thread/spinlock.h>
#include <log/log.h>

#include <stdatomic.h>
#include <string.h>

typedef struct Mel_Event_Item Mel_Event_Item;
typedef struct Mel_Event_Node Mel_Event_Node;
typedef struct Mel_Event_Snap Mel_Event_Snap;

struct Mel_Event_Item
{
    Mel_Event_Item* next;
};

struct Mel_Event_Node
{
    Mel_Task           deliver;
    Mel_Event*         owner;
    Mel_SlotMap_Handle self;
    Mel_Executor*      exec;
    Mel_Event_Callback cb;
    void*              user;

    Mel_Spinlock    lock;
    Mel_Event_Item* fifo_head;
    Mel_Event_Item* fifo_tail;
    Mel_Event_Item* free_head;
    u8*             pool;
    u32             queued;
    _Atomic(u64)    total_lagged;
    _Atomic(i64)    refs;
    _Atomic(i32)    delivery_armed;
    bool            push;
};

struct Mel_Event_Snap
{
    _Atomic(i64)    refs;
    u32             count;
    Mel_Event_Node* nodes[];
};

struct Mel_Event
{
    const Mel_Alloc* alloc;
    usize            item_size;
    usize            item_stride;
    u32              ring_capacity;
    Mel_Event_Policy policy;

    _Atomic(i64)             refs;
    Mel_Spinlock             set_lock;
    Mel_SlotMap              subs;
    _Atomic(Mel_Event_Snap*) snapshot;
};

static void mel_event__latest(Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    info->drop_oldest = true;
    info->accepted = true;
}

static void mel_event__lossy(Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    info->drop_oldest = false;
    info->accepted = false;
}

static void mel_event__lossless(Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    info->drop_oldest = false;
    info->accepted = false;
    info->backpressured = true;
}

Mel_Event_Policy mel_event_policy_latest(Mel_Event_On_Overflow on_overflow, void* user) { return (Mel_Event_Policy){ .overflow = mel_event__latest, .on_overflow = on_overflow, .user = user }; }

Mel_Event_Policy mel_event_policy_lossy_lag(Mel_Event_On_Overflow on_overflow, void* user) { return (Mel_Event_Policy){ .overflow = mel_event__lossy, .on_overflow = on_overflow, .user = user }; }

Mel_Event_Policy mel_event_policy_lossless(Mel_Event_On_Overflow on_overflow, void* user) { return (Mel_Event_Policy){ .overflow = mel_event__lossless, .on_overflow = on_overflow, .user = user }; }

Mel_Event_Policy mel_event_policy_custom(Mel_Event_Overflow_Fn overflow, Mel_Event_On_Overflow on_overflow, void* user) { return (Mel_Event_Policy){ .overflow = overflow, .on_overflow = on_overflow, .user = user }; }

static usize mel_event__stride(usize item_size)
{
    usize header = sizeof(Mel_Event_Item);
    usize stride = header + item_size;
    usize align = sizeof(void*);
    return (stride + align - 1) & ~(align - 1);
}

static Mel_Event_Item* mel_event__item_at(Mel_Event_Node* node, usize stride, u32 i) { return (Mel_Event_Item*)(node->pool + (usize)i * stride); }

static void* mel_event__item_payload(Mel_Event_Item* item) { return (u8*)item + sizeof(Mel_Event_Item); }

Mel_Event* mel_event_create(const Mel_Alloc* alloc, usize item_size, u32 ring_capacity, Mel_Event_Policy policy)
{
    assert(alloc != nullptr);
    assert(item_size > 0);
    assert(ring_capacity > 0);
    assert(policy.overflow != nullptr);

    Mel_Event* ev = mel_alloc_type(alloc, Mel_Event);
    if (ev == nullptr)
        return nullptr;

    ev->alloc = alloc;
    ev->item_size = item_size;
    ev->item_stride = mel_event__stride(item_size);
    ev->ring_capacity = ring_capacity;
    ev->policy = policy;
    ev->set_lock = (Mel_Spinlock){ 0 };
    atomic_store_explicit(&ev->refs, 1, memory_order_relaxed);
    mel_slotmap_init(&ev->subs, alloc, .item_size = sizeof(Mel_Event_Node*), .initial_capacity = 8);
    atomic_store_explicit(&ev->snapshot, nullptr, memory_order_relaxed);
    return ev;
}

static void mel_event__node_free(Mel_Event* ev, Mel_Event_Node* node)
{
    if (node->pool != nullptr)
        mel_dealloc(ev->alloc, node->pool);
    mel_dealloc(ev->alloc, node);
}

static void mel_event__node_release(Mel_Event* ev, Mel_Event_Node* node)
{
    if (atomic_fetch_sub_explicit(&node->refs, 1, memory_order_acq_rel) == 1)
        mel_event__node_free(ev, node);
}

static void mel_event__snap_release(Mel_Event* ev, Mel_Event_Snap* snap)
{
    if (snap == nullptr)
        return;
    if (atomic_fetch_sub_explicit(&snap->refs, 1, memory_order_acq_rel) == 1)
    {
        for (u32 i = 0; i < snap->count; i++)
            mel_event__node_release(ev, snap->nodes[i]);
        mel_dealloc(ev->alloc, snap);
    }
}

static Mel_Event_Snap* mel_event__snap_acquire(Mel_Event* ev)
{
    mel_spinlock_lock(&ev->set_lock);
    Mel_Event_Snap* snap = atomic_load_explicit(&ev->snapshot, memory_order_relaxed);
    if (snap != nullptr)
        atomic_fetch_add_explicit(&snap->refs, 1, memory_order_relaxed);
    mel_spinlock_unlock(&ev->set_lock);
    return snap;
}

static Mel_Event_Snap* mel_event__snap_build(Mel_Event* ev)
{
    Mel_Event_Node** data = (Mel_Event_Node**)mel_slotmap_data(&ev->subs);
    u32              n = mel_slotmap_count(&ev->subs);

    Mel_Event_Snap* snap = (Mel_Event_Snap*)mel_alloc(ev->alloc, sizeof(Mel_Event_Snap) + (usize)n * sizeof(Mel_Event_Node*));
    if (snap == nullptr)
        return nullptr;

    atomic_store_explicit(&snap->refs, 1, memory_order_relaxed);
    snap->count = n;
    for (u32 i = 0; i < n; i++)
    {
        Mel_Event_Node* node = data[i];
        atomic_fetch_add_explicit(&node->refs, 1, memory_order_relaxed);
        snap->nodes[i] = node;
    }
    return snap;
}

static bool mel_event__republish(Mel_Event* ev)
{
    Mel_Event_Snap* fresh = mel_event__snap_build(ev);
    if (fresh == nullptr)
        return false;
    Mel_Event_Snap* old = atomic_exchange_explicit(&ev->snapshot, fresh, memory_order_acq_rel);
    mel_event__snap_release(ev, old);
    return true;
}

static void mel_event__channel_free(Mel_Event* ev)
{
    Mel_Event_Snap* snap = atomic_exchange_explicit(&ev->snapshot, nullptr, memory_order_acq_rel);
    mel_event__snap_release(ev, snap);

    Mel_Event_Node** data = (Mel_Event_Node**)mel_slotmap_data(&ev->subs);
    u32              n = mel_slotmap_count(&ev->subs);
    for (u32 i = 0; i < n; i++)
        mel_event__node_release(ev, data[i]);

    mel_slotmap_free(&ev->subs);
    const Mel_Alloc* alloc = ev->alloc;
    mel_dealloc(alloc, ev);
}

static void mel_event__channel_release(Mel_Event* ev)
{
    if (atomic_fetch_sub_explicit(&ev->refs, 1, memory_order_acq_rel) == 1)
        mel_event__channel_free(ev);
}

void mel_event_destroy(Mel_Event* ev)
{
    if (ev == nullptr)
        return;
    mel_event__channel_release(ev);
}

static Mel_Event_Node* mel_event__resolve_locked(Mel_Event* ev, Mel_SlotMap_Handle handle)
{
    Mel_Event_Node** pp = (Mel_Event_Node**)mel_slotmap_get(&ev->subs, handle);
    return pp != nullptr ? *pp : nullptr;
}

static Mel_Event_Node* mel_event__node_new(Mel_Event* ev, bool push, Mel_Executor* exec, Mel_Event_Callback cb, void* user)
{
    Mel_Event_Node* node = mel_alloc_type(ev->alloc, Mel_Event_Node);
    if (node == nullptr)
        return nullptr;

    memset(node, 0, sizeof *node);
    node->owner = ev;
    node->exec = exec;
    node->cb = cb;
    node->user = user;
    node->push = push;
    node->lock = (Mel_Spinlock){ 0 };

    node->pool = (u8*)mel_alloc(ev->alloc, ev->item_stride * ev->ring_capacity);
    if (node->pool == nullptr)
    {
        mel_dealloc(ev->alloc, node);
        return nullptr;
    }
    for (u32 i = 0; i < ev->ring_capacity; i++)
    {
        Mel_Event_Item* it = mel_event__item_at(node, ev->item_stride, i);
        it->next = node->free_head;
        node->free_head = it;
    }

    atomic_store_explicit(&node->refs, 1, memory_order_relaxed);
    atomic_store_explicit(&node->total_lagged, 0, memory_order_relaxed);
    atomic_store_explicit(&node->delivery_armed, 0, memory_order_relaxed);
    mel_task_init(&node->deliver, nullptr);
    return node;
}

static void mel_event__deliver_run(Mel_Task* self);

static Mel_Event_Sub mel_event__subscribe(Mel_Event* ev, bool push, Mel_Executor* exec, Mel_Event_Callback cb, void* user)
{
    Mel_Event_Node* node = mel_event__node_new(ev, push, exec, cb, user);
    if (node == nullptr)
        return MEL_EVENT_SUB_NULL;
    mel_task_init(&node->deliver, mel_event__deliver_run);

    mel_spinlock_lock(&ev->set_lock);
    Mel_Event_Node* slot = node;
    node->self = mel_slotmap_insert(&ev->subs, &slot);
    bool ok = mel_event__republish(ev);
    if (!ok)
        mel_slotmap_remove(&ev->subs, node->self);
    mel_spinlock_unlock(&ev->set_lock);

    if (!ok)
    {
        mel_event__node_free(ev, node);
        return MEL_EVENT_SUB_NULL;
    }
    return (Mel_Event_Sub){ node->self };
}

Mel_Event_Sub mel_event_subscribe_push(Mel_Event* ev, Mel_Executor* exec, Mel_Event_Callback cb, void* user)
{
    assert(ev != nullptr);
    assert(exec != nullptr);
    assert(cb != nullptr);
    return mel_event__subscribe(ev, true, exec, cb, user);
}

Mel_Event_Sub mel_event_subscribe_pull(Mel_Event* ev, void* user)
{
    assert(ev != nullptr);
    return mel_event__subscribe(ev, false, nullptr, nullptr, user);
}

void mel_event_unsubscribe(Mel_Event* ev, Mel_Event_Sub sub)
{
    assert(ev != nullptr);

    mel_spinlock_lock(&ev->set_lock);
    Mel_Event_Node* node = mel_event__resolve_locked(ev, sub.handle);
    if (node == nullptr)
    {
        mel_spinlock_unlock(&ev->set_lock);
        return;
    }
    mel_slotmap_remove(&ev->subs, sub.handle);
    mel_event__republish(ev);
    mel_spinlock_unlock(&ev->set_lock);

    mel_event__node_release(ev, node);
}

static bool mel_event__push_item(Mel_Event* ev, Mel_Event_Node* node, const void* item)
{
    mel_spinlock_lock(&node->lock);

    Mel_Event_Item* slot = node->free_head;
    if (slot != nullptr)
    {
        node->free_head = slot->next;
        memcpy(mel_event__item_payload(slot), item, ev->item_size);
        slot->next = nullptr;
        if (node->fifo_tail != nullptr)
            node->fifo_tail->next = slot;
        else
            node->fifo_head = slot;
        node->fifo_tail = slot;
        node->queued++;
        mel_spinlock_unlock(&node->lock);
        return node->push;
    }

    Mel_Event_Overflow_Info info = {
        .sub = (Mel_Event_Sub){ node->self },
        .ring_capacity = ev->ring_capacity,
        .ring_count = node->queued,
        .total_lagged = atomic_load_explicit(&node->total_lagged, memory_order_relaxed),
        .push = node->push,
    };
    ev->policy.overflow(&info, ev->policy.user);

    bool delivered = false;
    if (info.accepted && info.drop_oldest)
    {
        Mel_Event_Item* oldest = node->fifo_head;
        node->fifo_head = oldest->next;
        if (node->fifo_head == nullptr)
            node->fifo_tail = nullptr;
        node->queued--;

        memcpy(mel_event__item_payload(oldest), item, ev->item_size);
        oldest->next = nullptr;
        if (node->fifo_tail != nullptr)
            node->fifo_tail->next = oldest;
        else
            node->fifo_head = oldest;
        node->fifo_tail = oldest;
        node->queued++;
        delivered = node->push;
    }

    u64 lagged = atomic_fetch_add_explicit(&node->total_lagged, 1, memory_order_relaxed) + 1;
    u32 queued = node->queued;
    mel_spinlock_unlock(&node->lock);

    if (info.backpressured)
        mel_log_warn("event", "lossless subscriber ring full (capacity %u); item refused, backpressure reported", ev->ring_capacity);

    info.total_lagged = lagged;
    info.ring_count = queued;
    if (ev->policy.on_overflow != nullptr)
        ev->policy.on_overflow(&info, ev->policy.user);

    return delivered;
}

void mel_event_fire(Mel_Event* ev, const void* item)
{
    assert(ev != nullptr);
    assert(item != nullptr);

    Mel_Event_Snap* snap = mel_event__snap_acquire(ev);
    if (snap == nullptr)
        return;

    for (u32 i = 0; i < snap->count; i++)
    {
        Mel_Event_Node* node = snap->nodes[i];
        bool            wake = mel_event__push_item(ev, node, item);
        if (wake && atomic_exchange_explicit(&node->delivery_armed, 1, memory_order_acq_rel) == 0)
        {
            atomic_fetch_add_explicit(&ev->refs, 1, memory_order_relaxed);
            atomic_fetch_add_explicit(&node->refs, 1, memory_order_relaxed);
            mel_executor_submit(node->exec, &node->deliver);
        }
    }

    mel_event__snap_release(ev, snap);
}

static Mel_Event_Item* mel_event__detach_head(Mel_Event_Node* node)
{
    mel_spinlock_lock(&node->lock);
    Mel_Event_Item* it = node->fifo_head;
    if (it != nullptr)
    {
        node->fifo_head = it->next;
        if (node->fifo_head == nullptr)
            node->fifo_tail = nullptr;
        node->queued--;
    }
    mel_spinlock_unlock(&node->lock);
    return it;
}

static void mel_event__recycle(Mel_Event_Node* node, Mel_Event_Item* it)
{
    mel_spinlock_lock(&node->lock);
    it->next = node->free_head;
    node->free_head = it;
    mel_spinlock_unlock(&node->lock);
}

static bool mel_event__pop_item(Mel_Event* ev, Mel_Event_Node* node, void* out)
{
    Mel_Event_Item* it = mel_event__detach_head(node);
    if (it == nullptr)
        return false;
    memcpy(out, mel_event__item_payload(it), ev->item_size);
    mel_event__recycle(node, it);
    return true;
}

static void mel_event__deliver_run(Mel_Task* self)
{
    Mel_Event_Node* node = mel_container_of(self, Mel_Event_Node, deliver);
    Mel_Event*      ev = node->owner;

    for (;;)
    {
        for (Mel_Event_Item* it = mel_event__detach_head(node); it != nullptr; it = mel_event__detach_head(node))
        {
            node->cb(mel_event__item_payload(it), node->user);
            mel_event__recycle(node, it);
        }

        atomic_store_explicit(&node->delivery_armed, 0, memory_order_release);

        mel_spinlock_lock(&node->lock);
        bool more = node->fifo_head != nullptr;
        mel_spinlock_unlock(&node->lock);
        if (!more)
            break;

        i32 expected = 0;
        if (!atomic_compare_exchange_strong_explicit(&node->delivery_armed, &expected, 1, memory_order_acq_rel, memory_order_relaxed))
            break;
    }

    mel_event__node_release(ev, node);
    mel_event__channel_release(ev);
}

bool mel_event_pull(Mel_Event* ev, Mel_Event_Sub sub, void* item_out)
{
    assert(ev != nullptr);
    assert(item_out != nullptr);

    mel_spinlock_lock(&ev->set_lock);
    Mel_Event_Node* node = mel_event__resolve_locked(ev, sub.handle);
    if (node != nullptr)
        atomic_fetch_add_explicit(&node->refs, 1, memory_order_relaxed);
    mel_spinlock_unlock(&ev->set_lock);

    if (node == nullptr)
        return false;

    bool ok = mel_event__pop_item(ev, node, item_out);
    mel_event__node_release(ev, node);
    return ok;
}

u32 mel_event_pull_pending(Mel_Event* ev, Mel_Event_Sub sub)
{
    assert(ev != nullptr);

    mel_spinlock_lock(&ev->set_lock);
    Mel_Event_Node* node = mel_event__resolve_locked(ev, sub.handle);
    if (node != nullptr)
        atomic_fetch_add_explicit(&node->refs, 1, memory_order_relaxed);
    mel_spinlock_unlock(&ev->set_lock);

    if (node == nullptr)
        return 0;

    mel_spinlock_lock(&node->lock);
    u32 queued = node->queued;
    mel_spinlock_unlock(&node->lock);

    mel_event__node_release(ev, node);
    return queued;
}

u64 mel_event_lag(Mel_Event* ev, Mel_Event_Sub sub)
{
    assert(ev != nullptr);

    mel_spinlock_lock(&ev->set_lock);
    Mel_Event_Node* node = mel_event__resolve_locked(ev, sub.handle);
    u64             lag = node != nullptr ? atomic_load_explicit(&node->total_lagged, memory_order_relaxed) : 0;
    mel_spinlock_unlock(&ev->set_lock);
    return lag;
}

u32 mel_event_subscriber_count(const Mel_Event* ev)
{
    assert(ev != nullptr);
    Mel_Event* m = (Mel_Event*)ev;
    mel_spinlock_lock(&m->set_lock);
    u32 n = mel_slotmap_count(&m->subs);
    mel_spinlock_unlock(&m->set_lock);
    return n;
}
