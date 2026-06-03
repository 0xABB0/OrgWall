#include <executor/executor.h>

#include <allocator/allocator.h>
#include <collection.list/list.h>
#include <core/compiler.h>

#include <assert.h>

static MEL_THREAD_LOCAL Mel_Mpsc_Node* s_inline_head;
static MEL_THREAD_LOCAL Mel_Mpsc_Node* s_inline_tail;
static MEL_THREAD_LOCAL bool           s_inline_draining;

static void mel_executor__inline_drain(void)
{
    s_inline_draining = true;
    while (s_inline_head != NULL)
    {
        Mel_Mpsc_Node* node = s_inline_head;
        Mel_Task*      task = mel_container_of(node, Mel_Task, link);
        s_inline_head = atomic_load_explicit(&node->next, memory_order_relaxed);
        if (s_inline_head == NULL)
            s_inline_tail = NULL;

        atomic_store_explicit(&node->next, NULL, memory_order_relaxed);
        atomic_store_explicit(&task->armed, 0, memory_order_release);
        task->run(task);
    }
    s_inline_draining = false;
}

static void mel_executor__inline_submit(Mel_Executor* self, Mel_Task* task)
{
    MEL_UNUSED(self);

    i32 expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&task->armed, &expected, 1, memory_order_acq_rel, memory_order_acquire))
        return;

    atomic_store_explicit(&task->link.next, NULL, memory_order_relaxed);
    if (s_inline_tail != NULL)
        atomic_store_explicit(&s_inline_tail->next, &task->link, memory_order_relaxed);
    else
        s_inline_head = &task->link;
    s_inline_tail = &task->link;

    if (s_inline_draining)
        return;

    mel_executor__inline_drain();
}

static Mel_Executor s_inline_executor = { mel_executor__inline_submit };

Mel_Executor* mel_executor_inline(void) { return &s_inline_executor; }

static void mel_resubmit__wake(void* user)
{
    Mel_Resubmit_Cell* cell = (Mel_Resubmit_Cell*)user;
    cell->exec->submit(cell->exec, cell->task);
}

Mel_Waker mel_resubmit_waker(Mel_Resubmit_Cell* cell) { return (Mel_Waker){ .wake = mel_resubmit__wake, .user = cell }; }

typedef struct
{
    Mel_Task task;
    void (*fn)(void* data);
    void*            data;
    const Mel_Alloc* alloc;
} Mel_Executor__Call_Node;

static void mel_executor__call_run(Mel_Task* self)
{
    Mel_Executor__Call_Node* node = (Mel_Executor__Call_Node*)self;
    void (*fn)(void* data) = node->fn;
    void*            data = node->data;
    const Mel_Alloc* alloc = node->alloc;
    fn(data);
    mel_dealloc(alloc, node);
}

void mel_executor_call(Mel_Executor* exec, void (*fn)(void* data), void* data, const Mel_Alloc* alloc)
{
    Mel_Executor__Call_Node* node = mel_alloc_type(alloc, Mel_Executor__Call_Node);
    assert(node != NULL);
    if (node == NULL)
        return;
    mel_task_init(&node->task, mel_executor__call_run);
    node->fn = fn;
    node->data = data;
    node->alloc = alloc;
    exec->submit(exec, &node->task);
}
