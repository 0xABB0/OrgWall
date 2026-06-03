#include <future/future.h>

#include <allocator/allocator.h>
#include <collection.list/list.h>

#include <assert.h>

#define MEL_FUTURE__PENDING       0u
#define MEL_FUTURE__RESOLVED      1u
#define MEL_FUTURE__CANCELLED     2u
#define MEL_FUTURE__TERMINAL_MASK 3u
#define MEL_FUTURE__DELIVERED     4u

void mel_future_init(Mel_Future* f, Mel_Future_Free free_value, const Mel_Alloc* alloc)
{
    assert(f != NULL);
    atomic_store_explicit(&f->state, MEL_FUTURE__PENDING, memory_order_relaxed);
    f->status = MEL_FUTURE_OK;
    f->value = NULL;
    atomic_store_explicit(&f->cont, NULL, memory_order_relaxed);
    f->cont_exec = NULL;
    f->free_value = free_value;
    f->alloc = alloc;
}

static void mel_future__try_deliver(Mel_Future* f)
{
    u32 state = atomic_load_explicit(&f->state, memory_order_seq_cst);
    u32 terminal = state & MEL_FUTURE__TERMINAL_MASK;
    if (terminal == MEL_FUTURE__PENDING)
        return;

    Mel_Task* cont = atomic_load_explicit(&f->cont, memory_order_seq_cst);
    if (cont == NULL)
        return;

    u32 expected = terminal;
    if (!atomic_compare_exchange_strong_explicit(&f->state, &expected, terminal | MEL_FUTURE__DELIVERED, memory_order_acq_rel, memory_order_acquire))
        return;

    f->cont_exec->submit(f->cont_exec, cont);
}

bool mel_future_resolve(Mel_Future* f, void* value, Mel_Future_Status status)
{
    assert(f != NULL);
    assert((status & MEL_FUTURE_CANCELLED) == 0u);

    if ((atomic_load_explicit(&f->state, memory_order_relaxed) & MEL_FUTURE__TERMINAL_MASK) != MEL_FUTURE__PENDING)
    {
        if (f->free_value != NULL && value != NULL)
            f->free_value(value, f->alloc);
        return false;
    }

    f->value = value;
    f->status = status;

    u32 expected = MEL_FUTURE__PENDING;
    if (!atomic_compare_exchange_strong_explicit(&f->state, &expected, MEL_FUTURE__RESOLVED, memory_order_seq_cst, memory_order_acquire))
    {
        if (f->free_value != NULL && value != NULL)
            f->free_value(value, f->alloc);
        return false;
    }

    mel_future__try_deliver(f);
    return true;
}

bool mel_future_cancel(Mel_Future* f)
{
    assert(f != NULL);

    u32 expected = MEL_FUTURE__PENDING;
    if (!atomic_compare_exchange_strong_explicit(&f->state, &expected, MEL_FUTURE__CANCELLED, memory_order_seq_cst, memory_order_acquire))
        return false;

    mel_future__try_deliver(f);
    return true;
}

void mel_future_then(Mel_Future* f, Mel_Task* cont, Mel_Executor* target_executor)
{
    assert(f != NULL);
    assert(cont != NULL);
    assert(target_executor != NULL);

    Mel_Task* prev = atomic_load_explicit(&f->cont, memory_order_relaxed);
    assert(prev == NULL);

    f->cont_exec = target_executor;
    atomic_store_explicit(&f->cont, cont, memory_order_seq_cst);

    mel_future__try_deliver(f);
}

bool mel_future_resolved(const Mel_Future* f)
{
    assert(f != NULL);
    return (atomic_load_explicit(&f->state, memory_order_acquire) & MEL_FUTURE__TERMINAL_MASK) != MEL_FUTURE__PENDING;
}

void* mel_future_value(const Mel_Future* f)
{
    assert(f != NULL);
    u32 terminal = atomic_load_explicit(&f->state, memory_order_acquire) & MEL_FUTURE__TERMINAL_MASK;
    assert(terminal != MEL_FUTURE__PENDING);
    if (terminal == MEL_FUTURE__CANCELLED)
        return NULL;
    return f->value;
}

Mel_Future_Status mel_future_status(const Mel_Future* f)
{
    assert(f != NULL);
    u32 terminal = atomic_load_explicit(&f->state, memory_order_acquire) & MEL_FUTURE__TERMINAL_MASK;
    assert(terminal != MEL_FUTURE__PENDING);
    if (terminal == MEL_FUTURE__CANCELLED)
        return MEL_FUTURE_ERROR | MEL_FUTURE_CANCELLED;
    return f->status;
}

void mel_future_scope_init(Mel_Future_Scope* scope, const Mel_Alloc* alloc)
{
    assert(scope != NULL);
    assert(alloc != NULL);
    mel_array_init(&scope->children, alloc);
    scope->alloc = alloc;
}

void mel_future_scope_adopt(Mel_Future_Scope* scope, Mel_Future* f)
{
    assert(scope != NULL);
    assert(f != NULL);
    mel_array_push(&scope->children, f);
}

void mel_future_scope_cancel(Mel_Future_Scope* scope)
{
    assert(scope != NULL);
    for (usize i = 0; i < scope->children.count; i++)
        mel_future_cancel(scope->children.items[i]);
}

void mel_future_scope_teardown(Mel_Future_Scope* scope)
{
    assert(scope != NULL);
    mel_future_scope_cancel(scope);
    mel_array_free(&scope->children);
}

typedef struct
{
    Mel_Task         task;
    Mel_Future_When* owner;
    Mel_Future*      input;
} Mel_Future__Join_Node;

struct Mel_Future_When
{
    Mel_Future result;

    Mel_Future__Join_Node* nodes;
    usize                  count;

    _Atomic(i64) remaining;
    _Atomic(i32) any_gate;

    const Mel_Alloc* alloc;
};

static Mel_Future_Status mel_future__merge_status(Mel_Future_Status acc, Mel_Future_Status s)
{
    u32 acc_sev = acc & MEL_FUTURE_SEVERITY_MASK;
    u32 s_sev = s & MEL_FUTURE_SEVERITY_MASK;
    u32 sev = acc_sev > s_sev ? acc_sev : s_sev;
    return sev | ((acc | s) & ~MEL_FUTURE_SEVERITY_MASK);
}

static void mel_future__join_all_run(Mel_Task* self)
{
    Mel_Future__Join_Node* node = mel_container_of(self, Mel_Future__Join_Node, task);
    Mel_Future_When*       w = node->owner;

    i64 left = atomic_fetch_sub_explicit(&w->remaining, 1, memory_order_acq_rel) - 1;
    if (left != 0)
        return;

    Mel_Future_Status agg = MEL_FUTURE_OK;
    for (usize i = 0; i < w->count; i++)
    {
        Mel_Future_Status s = mel_future_status(w->nodes[i].input);
        agg = mel_future__merge_status(agg, s);
    }
    if (agg != MEL_FUTURE_OK)
        agg |= MEL_FUTURE_PARTIAL;

    mel_future_resolve(&w->result, NULL, agg & ~MEL_FUTURE_CANCELLED);
}

static void mel_future__join_any_run(Mel_Task* self)
{
    Mel_Future__Join_Node* node = mel_container_of(self, Mel_Future__Join_Node, task);
    Mel_Future_When*       w = node->owner;

    i32 expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&w->any_gate, &expected, 1, memory_order_acq_rel, memory_order_acquire))
        return;

    Mel_Future_Status s = mel_future_status(node->input);
    void*             v = mel_future_value(node->input);
    if (s & MEL_FUTURE_CANCELLED)
        mel_future_cancel(&w->result);
    else
        mel_future_resolve(&w->result, v, s & ~MEL_FUTURE_CANCELLED);
}

static Mel_Future_When* mel_future__build_when(Mel_Future* const* inputs, usize n, void (*run)(Mel_Task*), const Mel_Alloc* alloc)
{
    assert(alloc != NULL);
    assert(n == 0 || inputs != NULL);

    Mel_Future_When* w = mel_alloc_type(alloc, Mel_Future_When);
    assert(w != NULL);

    mel_future_init(&w->result, NULL, alloc);
    w->nodes = n == 0 ? NULL : mel_alloc_array(alloc, Mel_Future__Join_Node, n);
    assert(n == 0 || w->nodes != NULL);
    w->count = n;
    w->alloc = alloc;
    atomic_store_explicit(&w->remaining, (i64)n, memory_order_relaxed);
    atomic_store_explicit(&w->any_gate, 0, memory_order_relaxed);

    for (usize i = 0; i < n; i++)
    {
        w->nodes[i].owner = w;
        w->nodes[i].input = inputs[i];
        mel_task_init(&w->nodes[i].task, run);
        mel_future_then(inputs[i], &w->nodes[i].task, mel_executor_inline());
    }

    return w;
}

Mel_Future_When* mel_future_when_all(Mel_Future* const* inputs, usize n, const Mel_Alloc* alloc)
{
    Mel_Future_When* w = mel_future__build_when(inputs, n, mel_future__join_all_run, alloc);
    if (n == 0)
        mel_future_resolve(&w->result, NULL, MEL_FUTURE_OK);
    return w;
}

Mel_Future_When* mel_future_when_any(Mel_Future* const* inputs, usize n, const Mel_Alloc* alloc)
{
    Mel_Future_When* w = mel_future__build_when(inputs, n, mel_future__join_any_run, alloc);
    if (n == 0)
        mel_future_cancel(&w->result);
    return w;
}

Mel_Future* mel_future_when_future(Mel_Future_When* w)
{
    assert(w != NULL);
    return &w->result;
}

void mel_future_when_free(Mel_Future_When* w)
{
    assert(w != NULL);
    const Mel_Alloc* alloc = w->alloc;
    if (w->nodes != NULL)
        mel_dealloc(alloc, w->nodes);
    mel_dealloc(alloc, w);
}
