#include <dialog/dialog.h>
#include <dialog/backend.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <collection.slotmap/slotmap.h>
#include <collection.list/list.h>
#include <future/future.h>
#include <executor/executor.h>
#include <reactor/reactor.h>
#include <log/log.h>

#include <string.h>

typedef struct
{
    char* label;
    Mel_Array(char*) patterns;
} Filter_Copy;

struct Mel_Dialog_Job
{
    Mel_Future         future;
    Mel_SlotMap_Handle self;
    const Mel_Alloc*   alloc;
    Mel_Executor*      deliver;

    u32        request;
    Mel_Window parent;
    char*      title;
    char*      default_path;
    char*      default_name;
    Mel_Array(Filter_Copy) filters;

    Mel_Array(char*) paths;
    Mel_Array(const char*) path_view;
    u32                  chosen_filter;
    Mel_Dialog_Selection selection;

    Mel_Dialog_Status status;
    bool              resolved;
};

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Reactor*     reactor;
    Mel_Executor*    exec;
    Mel_SlotMap      jobs;
} Dialog;

static Dialog g;

static char* dup_cstr(const Mel_Alloc* a, const char* s)
{
    if (!s)
        return NULL;
    usize n = strlen(s);
    char* d = (char*)mel_alloc(a, n + 1);
    if (!d)
        return NULL;
    memcpy(d, s, n + 1);
    return d;
}

void mel_dialog_init(const Mel_Alloc* alloc, Mel_Reactor* reactor)
{
    if (g.initialized)
        return;
    g.alloc = alloc ? alloc : mel_alloc_heap();
    g.reactor = reactor;
    g.exec = reactor ? mel_reactor_executor(reactor) : mel_executor_inline();
    mel_slotmap_init(&g.jobs, g.alloc, .item_size = sizeof(Mel_Dialog_Job*), .initial_capacity = 4);
    g.initialized = true;
}

static void job_storage_free(Mel_Dialog_Job* j)
{
    for (usize i = 0; i < j->filters.count; i++)
    {
        Filter_Copy* fc = &j->filters.items[i];
        if (fc->label)
            mel_dealloc(j->alloc, fc->label);
        for (usize p = 0; p < fc->patterns.count; p++)
            if (fc->patterns.items[p])
                mel_dealloc(j->alloc, fc->patterns.items[p]);
        mel_array_free(&fc->patterns);
    }
    mel_array_free(&j->filters);
    for (usize i = 0; i < j->paths.count; i++)
        if (j->paths.items[i])
            mel_dealloc(j->alloc, j->paths.items[i]);
    mel_array_free(&j->paths);
    mel_array_free(&j->path_view);
    if (j->title)
        mel_dealloc(j->alloc, j->title);
    if (j->default_path)
        mel_dealloc(j->alloc, j->default_path);
    if (j->default_name)
        mel_dealloc(j->alloc, j->default_name);
    mel_slotmap_remove(&g.jobs, j->self);
    mel_dealloc(g.alloc, j);
}

void mel_dialog_job_resolve(Mel_Dialog_Job* j, Mel_Dialog_Status s)
{
    if (!j || j->resolved)
        return;
    j->resolved = true;
    j->status |= s;

    mel_array_reserve(&j->path_view, j->paths.count);
    j->path_view.count = 0;
    for (usize i = 0; i < j->paths.count; i++)
        mel_array_push(&j->path_view, (const char*)j->paths.items[i]);

    j->selection.paths = j->path_view.items;
    j->selection.path_count = (u32)j->path_view.count;
    j->selection.chosen_filter = j->chosen_filter;
    j->selection.status = j->status;

    mel_future_resolve(&j->future, &j->selection, (Mel_Future_Status)(j->status & MEL_DIALOG_SEVERITY_MASK));
}

static Mel_Dialog_Job* job_new(const Mel_Alloc* alloc, Mel_Executor* deliver, u32 request)
{
    const Mel_Alloc* a = alloc ? alloc : g.alloc;
    Mel_Dialog_Job*  j = mel_alloc_type(a, Mel_Dialog_Job);
    if (!j)
        return NULL;
    memset(j, 0, sizeof *j);
    j->alloc = a;
    j->deliver = deliver ? deliver : g.exec;
    j->request = request;
    mel_future_init(&j->future, NULL, a);
    mel_array_init(&j->filters, a);
    mel_array_init(&j->paths, a);
    mel_array_init(&j->path_view, a);
    Mel_Dialog_Job* slot = j;
    j->self = mel_slotmap_insert(&g.jobs, &slot);
    return j;
}

static void copy_filters(Mel_Dialog_Job* j, const Mel_Dialog_Filter* filters, u32 n)
{
    for (u32 i = 0; i < n; i++)
    {
        Filter_Copy fc;
        fc.label = dup_cstr(j->alloc, filters[i].label);
        mel_array_init(&fc.patterns, j->alloc);
        for (u32 p = 0; p < filters[i].pattern_count; p++)
            mel_array_push(&fc.patterns, dup_cstr(j->alloc, filters[i].patterns[p]));
        mel_array_push(&j->filters, fc);
    }
}

static bool deliver_ok(Mel_Executor* deliver, Mel_Reactor* reactor, const char* op)
{
    if (!deliver)
        return true;
    Mel_Executor* expected = reactor ? mel_reactor_executor(reactor) : (g.reactor ? mel_reactor_executor(g.reactor) : mel_executor_inline());
    if (deliver == expected)
        return true;
    mel_log_error("dialog", "%s: deliver executor must match the reactor's executor; pass that or leave NULL", op);
    return false;
}

static bool backend_ready(void) { return g.initialized && mel_dialog__plat_available(); }

static Mel_Future* launch(Mel_Dialog_Job* j, const char* op)
{
    if (!j)
        return NULL;
    if (!backend_ready())
    {
        mel_log_error("dialog", "%s: no dialog backend on this platform", op);
        mel_dialog_job_resolve(j, MEL_DIALOG_ERROR | MEL_DIALOG_NO_BACKEND);
        return &j->future;
    }
    mel_dialog__plat_run(j);
    return &j->future;
}

Mel_Future* mel_dialog_open_file_opt(Mel_Dialog_Open_File_Opt opt)
{
    if (!g.initialized)
        return NULL;
    Mel_Dialog_Job* j = job_new(opt.alloc, opt.deliver, MEL_DIALOG_REQUEST_OPEN_FILE);
    if (!j)
        return NULL;
    if (!deliver_ok(opt.deliver, opt.reactor, "open_file"))
    {
        mel_dialog_job_resolve(j, MEL_DIALOG_ERROR | MEL_DIALOG_UNAVAILABLE);
        return &j->future;
    }
    j->parent = opt.parent;
    j->title = dup_cstr(j->alloc, opt.title);
    j->default_path = dup_cstr(j->alloc, opt.default_path);
    copy_filters(j, opt.filters, opt.filter_count);
    return launch(j, "open_file");
}

Mel_Future* mel_dialog_open_files_opt(Mel_Dialog_Open_File_Opt opt)
{
    if (!g.initialized)
        return NULL;
    Mel_Dialog_Job* j = job_new(opt.alloc, opt.deliver, MEL_DIALOG_REQUEST_OPEN_FILE | MEL_DIALOG_REQUEST_MULTI);
    if (!j)
        return NULL;
    if (!deliver_ok(opt.deliver, opt.reactor, "open_files"))
    {
        mel_dialog_job_resolve(j, MEL_DIALOG_ERROR | MEL_DIALOG_UNAVAILABLE);
        return &j->future;
    }
    j->parent = opt.parent;
    j->title = dup_cstr(j->alloc, opt.title);
    j->default_path = dup_cstr(j->alloc, opt.default_path);
    copy_filters(j, opt.filters, opt.filter_count);
    return launch(j, "open_files");
}

Mel_Future* mel_dialog_save_file_opt(Mel_Dialog_Save_File_Opt opt)
{
    if (!g.initialized)
        return NULL;
    Mel_Dialog_Job* j = job_new(opt.alloc, opt.deliver, MEL_DIALOG_REQUEST_SAVE_FILE);
    if (!j)
        return NULL;
    if (!deliver_ok(opt.deliver, opt.reactor, "save_file"))
    {
        mel_dialog_job_resolve(j, MEL_DIALOG_ERROR | MEL_DIALOG_UNAVAILABLE);
        return &j->future;
    }
    j->parent = opt.parent;
    j->title = dup_cstr(j->alloc, opt.title);
    j->default_path = dup_cstr(j->alloc, opt.default_path);
    j->default_name = dup_cstr(j->alloc, opt.default_name);
    copy_filters(j, opt.filters, opt.filter_count);
    return launch(j, "save_file");
}

Mel_Future* mel_dialog_open_folder_opt(Mel_Dialog_Open_Folder_Opt opt)
{
    if (!g.initialized)
        return NULL;
    Mel_Dialog_Job* j = job_new(opt.alloc, opt.deliver, MEL_DIALOG_REQUEST_OPEN_DIR);
    if (!j)
        return NULL;
    if (!deliver_ok(opt.deliver, opt.reactor, "open_folder"))
    {
        mel_dialog_job_resolve(j, MEL_DIALOG_ERROR | MEL_DIALOG_UNAVAILABLE);
        return &j->future;
    }
    j->parent = opt.parent;
    j->title = dup_cstr(j->alloc, opt.title);
    j->default_path = dup_cstr(j->alloc, opt.default_path);
    return launch(j, "open_folder");
}

const Mel_Dialog_Selection* mel_dialog_future_selection(const Mel_Future* f)
{
    return f ? (const Mel_Dialog_Selection*)mel_future_value((Mel_Future*)f) : NULL;
}

Mel_Dialog_Status mel_dialog_future_status(const Mel_Future* f)
{
    if (!f)
        return MEL_DIALOG_ERROR | MEL_DIALOG_CANCELLED;
    Mel_Future_Status s = mel_future_status((Mel_Future*)f);
    if (s & MEL_FUTURE_CANCELLED)
        return MEL_DIALOG_ERROR | MEL_DIALOG_CANCELLED;
    const Mel_Dialog_Job* j = mel_container_of(f, Mel_Dialog_Job, future);
    return j->status;
}

void mel_dialog_future_free(Mel_Future* f)
{
    if (!f || !g.initialized)
        return;
    Mel_Dialog_Job* j = mel_container_of(f, Mel_Dialog_Job, future);
    job_storage_free(j);
}

bool mel_dialog_available(void) { return backend_ready(); }

void mel_dialog_shutdown(void)
{
    if (!g.initialized)
        return;
    Mel_Array(Mel_Dialog_Job*) snap;
    mel_array_init(&snap, g.alloc);
    Mel_Dialog_Job** data = (Mel_Dialog_Job**)mel_slotmap_data(&g.jobs);
    u32              n = mel_slotmap_count(&g.jobs);
    for (u32 i = 0; i < n; i++)
        mel_array_push(&snap, data[i]);
    for (usize i = 0; i < snap.count; i++)
    {
        Mel_Dialog_Job* j = snap.items[i];
        bool            had_cont = atomic_load_explicit(&j->future.cont, memory_order_acquire) != NULL;
        if (!j->resolved)
            mel_future_cancel(&j->future);
        if (!had_cont)
            job_storage_free(j);
    }
    mel_array_free(&snap);
    mel_slotmap_free(&g.jobs);
    memset(&g, 0, sizeof g);
}

const Mel_Alloc* mel_dialog_job_alloc(const Mel_Dialog_Job* j) { return j ? j->alloc : NULL; }

u64 mel_dialog_job_token(const Mel_Dialog_Job* j) { return j ? mel_slotmap_handle_pack64(j->self) : 0; }

Mel_Dialog_Job* mel_dialog__job_from_token(u64 token)
{
    if (!g.initialized)
        return NULL;
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64(token);
    Mel_Dialog_Job**   pp = (Mel_Dialog_Job**)mel_slotmap_get(&g.jobs, h);
    return pp ? *pp : NULL;
}

u32 mel_dialog_job_request(const Mel_Dialog_Job* j) { return j ? j->request : 0; }

Mel_Window mel_dialog_job_parent(const Mel_Dialog_Job* j) { return j ? j->parent : MEL_WINDOW_NONE; }

const char* mel_dialog_job_title(const Mel_Dialog_Job* j) { return j ? j->title : NULL; }

const char* mel_dialog_job_default_path(const Mel_Dialog_Job* j) { return j ? j->default_path : NULL; }

const char* mel_dialog_job_default_name(const Mel_Dialog_Job* j) { return j ? j->default_name : NULL; }

u32 mel_dialog_job_filter_count(const Mel_Dialog_Job* j) { return j ? (u32)j->filters.count : 0; }

const char* mel_dialog_job_filter_label(const Mel_Dialog_Job* j, u32 filter)
{
    return (j && filter < j->filters.count) ? j->filters.items[filter].label : NULL;
}

u32 mel_dialog_job_filter_pattern_count(const Mel_Dialog_Job* j, u32 filter)
{
    return (j && filter < j->filters.count) ? (u32)j->filters.items[filter].patterns.count : 0;
}

const char* mel_dialog_job_filter_pattern(const Mel_Dialog_Job* j, u32 filter, u32 pattern)
{
    if (j && filter < j->filters.count && pattern < j->filters.items[filter].patterns.count)
        return j->filters.items[filter].patterns.items[pattern];
    return NULL;
}

void mel_dialog_job_emit_path(Mel_Dialog_Job* j, const char* path)
{
    if (!j || !path)
        return;
    mel_array_push(&j->paths, dup_cstr(j->alloc, path));
}

void mel_dialog_job_set_chosen_filter(Mel_Dialog_Job* j, u32 filter)
{
    if (j)
        j->chosen_filter = filter;
}

void mel_dialog_job_add_warning(Mel_Dialog_Job* j, Mel_Dialog_Status warn_bits)
{
    if (j)
        j->status |= warn_bits;
}
