#include <shell/shell.h>
#include <shell/backend.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <collection.list/list.h>
#include <collection.slotmap/slotmap.h>
#include <future/future.h>
#include <executor/executor.h>
#include <reactor/reactor.h>
#include <string/str8.h>
#include <log/log.h>

#include <string.h>

struct Mel_Shell_Job
{
    Mel_Future         future;
    Mel_SlotMap_Handle self;
    const Mel_Alloc*   alloc;

    str8 target;

    Mel_Shell_Status status;
    bool             resolved;
};

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Reactor*     reactor;
    Mel_SlotMap      jobs;
} Shell;

static Shell g;

void mel_shell_init(const Mel_Alloc* alloc, Mel_Reactor* reactor)
{
    if (g.initialized)
        return;
    g.alloc = alloc ? alloc : mel_alloc_heap();
    g.reactor = reactor;
    mel_slotmap_init(&g.jobs, g.alloc, .item_size = sizeof(Mel_Shell_Job*), .initial_capacity = 8);
    g.initialized = true;
}

static void job_storage_free(Mel_Shell_Job* j)
{
    const Mel_Alloc* a = j->alloc;
    if (g.initialized)
        mel_slotmap_remove(&g.jobs, j->self);
    if (j->target.data)
        mel_dealloc(a, j->target.data);
    mel_dealloc(a, j);
}

void mel_shell_job_resolve(Mel_Shell_Job* j, Mel_Shell_Status s)
{
    if (!j || j->resolved)
        return;
    j->resolved = true;
    j->status |= s;
    mel_future_resolve(&j->future, NULL, (Mel_Future_Status)(j->status & MEL_SHELL_SEVERITY_MASK));
}

void mel_shell_job_add_warning(Mel_Shell_Job* j, Mel_Shell_Status warn_bits)
{
    if (j)
        j->status |= warn_bits;
}

static Mel_Shell_Job* job_new(str8 target, Mel_Shell_Opt opt)
{
    const Mel_Alloc* a = opt.alloc ? opt.alloc : g.alloc;
    Mel_Shell_Job*   j = mel_alloc_type(a, Mel_Shell_Job);
    if (!j)
        return NULL;
    memset(j, 0, sizeof *j);
    j->alloc = a;
    j->target = str8_is_empty(target) ? STR8_EMPTY : str8_dup(target, j->alloc);
    mel_future_init(&j->future, NULL, j->alloc);
    Mel_Shell_Job* slot = j;
    j->self = mel_slotmap_insert(&g.jobs, &slot);
    if (opt.out_op)
        *opt.out_op = (Mel_Shell_Op){ j->self.index, j->self.generation };
    return j;
}

static bool backend_ready(void) { return g.initialized && mel_shell__plat_available(); }

static Mel_Future* dispatch(str8 target, Mel_Shell_Opt opt, void (*plat)(Mel_Shell_Job*), const char* what)
{
    if (!g.initialized)
    {
        mel_log_error("shell", "%s: called before mel_shell_init", what);
        return NULL;
    }
    Mel_Shell_Job* j = job_new(target, opt);
    if (!j)
        return NULL;
    if (str8_is_empty(target))
    {
        mel_log_error("shell", "%s: empty target", what);
        mel_shell_job_resolve(j, MEL_SHELL_ERROR | MEL_SHELL_RESULT_BAD_TARGET);
        return &j->future;
    }
    if (!backend_ready())
    {
        mel_log_error("shell", "%s: no backend", what);
        mel_shell_job_resolve(j, MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_BACKEND);
        return &j->future;
    }
    plat(j);
    return &j->future;
}

Mel_Future* mel_shell_open_url_opt(str8 url, Mel_Shell_Opt opt) { return dispatch(url, opt, mel_shell__plat_open_url, "open_url"); }

Mel_Future* mel_shell_reveal_path_opt(str8 path, Mel_Shell_Opt opt) { return dispatch(path, opt, mel_shell__plat_reveal_path, "reveal_path"); }

bool mel_shell_cancel(Mel_Shell_Op op)
{
    if (!g.initialized || !mel_shell_op_valid(op))
        return false;
    Mel_SlotMap_Handle h = { op.index, op.generation };
    Mel_Shell_Job**    pp = (Mel_Shell_Job**)mel_slotmap_get(&g.jobs, h);
    if (!pp || !*pp)
        return false;
    Mel_Shell_Job* j = *pp;
    if (j->resolved)
        return false;
    return mel_future_cancel(&j->future);
}

Mel_Shell_Status mel_shell_future_status(const Mel_Future* f)
{
    if (!f)
        return MEL_SHELL_ERROR | MEL_SHELL_RESULT_CANCELLED;
    if (!mel_future_resolved(f))
    {
        mel_log_error("shell", "mel_shell_future_status: future not yet resolved");
        return MEL_SHELL_ERROR;
    }
    Mel_Future_Status s = mel_future_status((Mel_Future*)f);
    if (s & MEL_FUTURE_CANCELLED)
        return MEL_SHELL_ERROR | MEL_SHELL_RESULT_CANCELLED;
    const Mel_Shell_Job* j = mel_container_of(f, Mel_Shell_Job, future);
    return j->status;
}

void mel_shell_future_free(Mel_Future* f)
{
    if (!f)
        return;
    Mel_Shell_Job* j = mel_container_of(f, Mel_Shell_Job, future);
    job_storage_free(j);
}

bool mel_shell_available(void) { return backend_ready(); }

void* mel_shell_native(void) { return g.initialized ? mel_shell__plat_native() : NULL; }

void mel_shell_shutdown(void)
{
    if (!g.initialized)
        return;

    Mel_Array(Mel_Shell_Job*) snap;
    mel_array_init(&snap, g.alloc);
    Mel_Shell_Job** data = (Mel_Shell_Job**)mel_slotmap_data(&g.jobs);
    u32             n = mel_slotmap_count(&g.jobs);
    for (u32 i = 0; i < n; i++)
        mel_array_push(&snap, data[i]);
    for (usize i = 0; i < snap.count; i++)
    {
        Mel_Shell_Job* j = snap.items[i];
        bool           had_cont = atomic_load_explicit(&j->future.cont, memory_order_acquire) != NULL;
        if (!j->resolved)
            mel_future_cancel(&j->future);
        if (!had_cont)
            job_storage_free(j);
    }
    mel_array_free(&snap);

    mel_slotmap_free(&g.jobs);
    memset(&g, 0, sizeof g);
}

const Mel_Alloc* mel_shell_job_alloc(const Mel_Shell_Job* j) { return j ? j->alloc : NULL; }

u64 mel_shell_job_token(const Mel_Shell_Job* j) { return j ? mel_slotmap_handle_pack64(j->self) : 0; }

Mel_Shell_Job* mel_shell__job_from_token(u64 token)
{
    if (!g.initialized)
        return NULL;
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64(token);
    Mel_Shell_Job**    pp = (Mel_Shell_Job**)mel_slotmap_get(&g.jobs, h);
    return pp ? *pp : NULL;
}

str8 mel_shell_job_target(const Mel_Shell_Job* j) { return j ? j->target : STR8_EMPTY; }
