#include <gui/gui.h>
#include <gui.platform/gui.platform.h>

#include <allocator/allocator.h>
#include <allocator.heap/heap.h>
#include <collection.array/array.h>
#include <collection.slotmap/slotmap.h>
#include <string/string.str8.h>
#include <string/table.h>

#include <core/platform.h>

#if MEL_PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    typedef DWORD mel__gui_thread_id;
    static inline mel__gui_thread_id mel__gui_self(void)     { return GetCurrentThreadId(); }
    static inline bool mel__gui_eq(mel__gui_thread_id a, mel__gui_thread_id b) { return a == b; }
#else
    #include <pthread.h>
    typedef pthread_t mel__gui_thread_id;
    static inline mel__gui_thread_id mel__gui_self(void)     { return pthread_self(); }
    static inline bool mel__gui_eq(mel__gui_thread_id a, mel__gui_thread_id b) { return pthread_equal(a, b) != 0; }
#endif

#include <stdio.h>
#include <stdlib.h>

typedef struct Mel_Gui_Class {
    Mel_Atom     atom;
    Mel_Atom     base;
    u32          style;
    Mel_Gui_Proc proc;
} Mel_Gui_Class;

typedef struct Mel_Gui_Window {
    Mel_Gui_Handle self;
    Mel_Atom       class_atom;
    Mel_Gui_Handle parent;
    u32            id;
    u32            style;
    void*          user;
    void*          native;
    Mel_Array(Mel_Gui_Proc) chain;
} Mel_Gui_Window;

#define MEL_GUI_FRAME_STACK_CAP 64u

typedef struct Mel_Gui_Frame {
    Mel_Gui_Handle h;
    i32            chain_idx;
} Mel_Gui_Frame;

static _Thread_local Mel_Gui_Frame mel__gui_frames[MEL_GUI_FRAME_STACK_CAP];
static _Thread_local i32           mel__gui_frame_top = -1;

static Mel_Atom_Table*           mel__gui_atoms;
static Mel_Array(Mel_Gui_Class)  mel__gui_classes;
static Mel_SlotMap               mel__gui_windows;
static bool                      mel__gui_initialized;
static mel__gui_thread_id        mel__gui_ui_thread;
static bool                      mel__gui_ui_thread_set;
static Mel_Gui_Handle            mel__gui_focus;

static const Mel_Alloc* mel__gui_alloc(void) { return mel_alloc_heap(); }

static Mel_Gui_Window* mel__gui_window_get(Mel_Gui_Handle h)
{
    if (!mel__gui_initialized) return NULL;
    return (Mel_Gui_Window*)mel_slotmap_get(&mel__gui_windows, h.handle);
}

static Mel_Gui_Class* mel__gui_class_get(Mel_Atom atom)
{
    if (atom == MEL_ATOM_NONE) return NULL;
    if ((usize)atom > mel__gui_classes.count) return NULL;
    Mel_Gui_Class* c = &mel__gui_classes.items[atom - 1u];
    return c->atom == atom ? c : NULL;
}

bool mel_gui_is_ui_thread(void)
{
    return mel__gui_ui_thread_set && mel__gui_eq(mel__gui_self(), mel__gui_ui_thread);
}

void mel_gui_assert_ui_thread(void)
{
    if (mel_gui_is_ui_thread()) return;
    fprintf(stderr, "[melody] gui call from non-UI thread; aborting\n");
    abort();
}

bool mel_gui_init(void)
{
    if (mel__gui_initialized) {
        mel_gui_assert_ui_thread();
        return true;
    }

    mel__gui_ui_thread     = mel__gui_self();
    mel__gui_ui_thread_set = true;

    const Mel_Alloc* alloc = mel__gui_alloc();

    mel__gui_atoms = mel_atom_table_create(alloc);
    if (mel__gui_atoms == NULL) return false;

    mel_array_init(&mel__gui_classes, alloc);
    mel_slotmap_init(&mel__gui_windows, alloc, .item_size = sizeof(Mel_Gui_Window), .initial_capacity = 32);

    mel__gui_initialized = true;

    if (!mel_gui_platform_init()) {
        mel_gui_shutdown();
        return false;
    }
    return true;
}

void mel_gui_shutdown(void)
{
    if (!mel__gui_initialized) return;
    mel_gui_assert_ui_thread();

    mel_gui_platform_shutdown();

    u32 count = mel_slotmap_count(&mel__gui_windows);
    Mel_Gui_Window* data = (Mel_Gui_Window*)mel_slotmap_data(&mel__gui_windows);
    for (u32 i = 0; i < count; i++) {
        mel_array_free(&data[i].chain);
    }

    mel_slotmap_free(&mel__gui_windows);
    mel_array_free(&mel__gui_classes);
    mel_atom_table_destroy(mel__gui_atoms);
    mel__gui_atoms = NULL;
    mel__gui_initialized = false;
}

bool mel_gui_is_initialized(void) { return mel__gui_initialized; }

Mel_Atom_Table* mel_gui_atom_table(void) { return mel__gui_atoms; }

Mel_Atom mel_gui_register_class(const Mel_Gui_Class_Desc* desc)
{
    if (desc == NULL || str8_is_empty(desc->name) || desc->proc == NULL) return MEL_ATOM_NONE;
    if (!mel_gui_init()) return MEL_ATOM_NONE;
    mel_gui_assert_ui_thread();

    Mel_Atom atom = mel_atom_intern(mel__gui_atoms, desc->name);
    if (atom == MEL_ATOM_NONE) return MEL_ATOM_NONE;

    while ((usize)atom > mel__gui_classes.count) {
        mel_array_push(&mel__gui_classes, ((Mel_Gui_Class){0}));
    }

    Mel_Gui_Class* c = &mel__gui_classes.items[atom - 1u];
    c->atom  = atom;
    c->base  = desc->base_class;
    c->style = desc->style;
    c->proc  = desc->proc;
    return atom;
}

Mel_Atom mel_gui_class_atom(str8 name)
{
    if (!mel__gui_initialized) return MEL_ATOM_NONE;
    mel_gui_assert_ui_thread();
    return mel_atom_lookup(mel__gui_atoms, name);
}

Mel_Atom mel_gui_class_base(Mel_Atom class_atom)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Class* c = mel__gui_class_get(class_atom);
    return c ? c->base : MEL_ATOM_NONE;
}

str8 mel_gui_class_name(Mel_Atom class_atom)
{
    mel_gui_assert_ui_thread();
    return mel_atom_get(mel__gui_atoms, class_atom);
}

#define MEL_GUI_CLASS_CHAIN_MAX 32u

static u32 mel__gui_class_chain(Mel_Atom class_atom, Mel_Atom out[MEL_GUI_CLASS_CHAIN_MAX])
{
    u32 n = 0;
    Mel_Atom cur = class_atom;
    while (cur != MEL_ATOM_NONE && n < MEL_GUI_CLASS_CHAIN_MAX) {
        Mel_Gui_Class* c = mel__gui_class_get(cur);
        if (c == NULL) break;
        out[n++] = cur;
        cur = c->base;
    }
    return n;
}

static Mel_Gui_Result mel__gui_dispatch(Mel_Gui_Window* w, Mel_Gui_Msg msg, Mel_Gui_WParam wp, Mel_Gui_LParam lp)
{
    if (mel__gui_frame_top + 1 >= (i32)MEL_GUI_FRAME_STACK_CAP) return MEL_GUI_ERR_PLATFORM;

    mel__gui_frame_top += 1;
    mel__gui_frames[mel__gui_frame_top].h         = w->self;
    mel__gui_frames[mel__gui_frame_top].chain_idx = (i32)w->chain.count - 1;

    Mel_Gui_Result r = MEL_GUI_OK;
    if (w->chain.count > 0) {
        Mel_Gui_Proc top_proc = w->chain.items[w->chain.count - 1u];
        r = top_proc(w->self, msg, wp, lp);
    } else {
        r = mel_gui_def_proc(w->self, msg, wp, lp);
    }

    mel__gui_frame_top -= 1;
    return r;
}

Mel_Gui_Result mel_gui_send_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wp, Mel_Gui_LParam lp)
{
    if (!mel__gui_initialized) return MEL_GUI_ERR_NOT_INITIALIZED;
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    if (w == NULL) return MEL_GUI_ERR_INVALID_HANDLE;
    return mel__gui_dispatch(w, msg, wp, lp);
}

Mel_Gui_Result mel_gui_call_super(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wp, Mel_Gui_LParam lp)
{
    mel_gui_assert_ui_thread();
    if (mel__gui_frame_top < 0) return mel_gui_def_proc(h, msg, wp, lp);

    Mel_Gui_Frame* f = &mel__gui_frames[mel__gui_frame_top];
    if (!mel_gui_handle_eq(f->h, h)) return MEL_GUI_ERR_INVALID_HANDLE;

    f->chain_idx -= 1;
    if (f->chain_idx < 0) return mel_gui_def_proc(h, msg, wp, lp);

    Mel_Gui_Window* w = mel__gui_window_get(h);
    if (w == NULL) return MEL_GUI_ERR_INVALID_HANDLE;
    if ((u32)f->chain_idx >= w->chain.count) return mel_gui_def_proc(h, msg, wp, lp);

    Mel_Gui_Proc p = w->chain.items[f->chain_idx];
    return p(h, msg, wp, lp);
}

Mel_Gui_Result mel_gui_def_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wp, Mel_Gui_LParam lp)
{
    mel_gui_assert_ui_thread();
    switch (msg) {
        case MEL_GUI_MSG_CLOSE:
            mel_gui_destroy(h);
            return MEL_GUI_OK;
        case MEL_GUI_MSG_APP_BACK:
            mel_gui_platform_request_exit();
            return MEL_GUI_OK;
        case MEL_GUI_MSG_GET_TEXT:
            return (Mel_Gui_Result)mel_gui_platform_get_text(h, (char*)(intptr_t)lp, (size)wp);
        default:
            (void)wp;
            (void)lp;
            return MEL_GUI_OK;
    }
}

bool mel_gui_post_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wp, Mel_Gui_LParam lp)
{
    if (!mel__gui_initialized) return false;
    if (mel__gui_window_get(h) == NULL) return false;
    return mel_gui_platform_post_message(h, msg, wp, lp);
}

#define MEL_GUI_APP_DISPATCH_MAX 64u

void mel_gui_dispatch_app_message(Mel_Gui_Msg msg, Mel_Gui_WParam wp, Mel_Gui_LParam lp)
{
    if (!mel__gui_initialized) return;
    mel_gui_assert_ui_thread();

    Mel_Gui_Handle roots[MEL_GUI_APP_DISPATCH_MAX];
    u32 n = 0;
    u32 count = mel_slotmap_count(&mel__gui_windows);
    Mel_Gui_Window* all = (Mel_Gui_Window*)mel_slotmap_data(&mel__gui_windows);
    for (u32 i = 0; i < count && n < MEL_GUI_APP_DISPATCH_MAX; i++) {
        if (mel_gui_handle_is_none(all[i].parent)) {
            roots[n++] = all[i].self;
        }
    }

    for (u32 i = 0; i < n; i++) {
        if (mel_slotmap_alive(&mel__gui_windows, roots[i].handle)) {
            Mel_Gui_Window* w = mel__gui_window_get(roots[i]);
            if (w != NULL) mel__gui_dispatch(w, msg, wp, lp);
        }
    }
}

Mel_Gui_Handle mel_gui_focus(void)
{
    mel_gui_assert_ui_thread();
    if (mel_gui_handle_is_none(mel__gui_focus)) return MEL_GUI_HANDLE_NONE;
    if (!mel_slotmap_alive(&mel__gui_windows, mel__gui_focus.handle)) return MEL_GUI_HANDLE_NONE;
    return mel__gui_focus;
}

Mel_Gui_Handle mel_gui_handle_from_native(void* native)
{
    mel_gui_assert_ui_thread();
    if (native == NULL || !mel__gui_initialized) return MEL_GUI_HANDLE_NONE;
    u32 count = mel_slotmap_count(&mel__gui_windows);
    Mel_Gui_Window* all = (Mel_Gui_Window*)mel_slotmap_data(&mel__gui_windows);
    for (u32 i = 0; i < count; i++) {
        if (all[i].native == native) return all[i].self;
    }
    return MEL_GUI_HANDLE_NONE;
}

bool mel_gui_set_focus(Mel_Gui_Handle h)
{
    mel_gui_assert_ui_thread();
    return mel_gui_platform_set_focus(h);
}

void mel_gui_dispatch_focus(Mel_Gui_Handle h)
{
    mel_gui_assert_ui_thread();
    if (mel_gui_handle_eq(mel__gui_focus, h)) return;

    Mel_Gui_Handle prev = mel__gui_focus;
    mel__gui_focus = h;

    if (!mel_gui_handle_is_none(prev) && mel_slotmap_alive(&mel__gui_windows, prev.handle)) {
        Mel_Gui_Window* w = mel__gui_window_get(prev);
        if (w != NULL) mel__gui_dispatch(w, MEL_GUI_MSG_FOCUS_LOST, 0, 0);
    }
    if (!mel_gui_handle_is_none(h) && mel_slotmap_alive(&mel__gui_windows, h.handle)) {
        Mel_Gui_Window* w = mel__gui_window_get(h);
        if (w != NULL) mel__gui_dispatch(w, MEL_GUI_MSG_FOCUS_GAINED, 0, 0);
    }
}

bool mel_gui_attach_proc(Mel_Gui_Handle h, Mel_Gui_Proc proc)
{
    mel_gui_assert_ui_thread();
    if (proc == NULL) return false;
    Mel_Gui_Window* w = mel__gui_window_get(h);
    if (w == NULL) return false;
    mel_array_push(&w->chain, proc);
    return true;
}

bool mel_gui_detach_proc(Mel_Gui_Handle h, Mel_Gui_Proc proc)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    if (w == NULL || proc == NULL) return false;
    for (usize i = w->chain.count; i > 0; i--) {
        if (w->chain.items[i - 1u] == proc) {
            for (usize j = i - 1u; j + 1u < w->chain.count; j++) {
                w->chain.items[j] = w->chain.items[j + 1u];
            }
            w->chain.count -= 1;
            return true;
        }
    }
    return false;
}

Mel_Gui_Handle mel_gui_create(const Mel_Gui_Create_Desc* desc)
{
    if (desc == NULL) return MEL_GUI_HANDLE_NONE;
    if (!mel_gui_init()) return MEL_GUI_HANDLE_NONE;
    mel_gui_assert_ui_thread();

    Mel_Atom chain[MEL_GUI_CLASS_CHAIN_MAX];
    u32 chain_len = mel__gui_class_chain(desc->class_atom, chain);
    if (chain_len == 0) return MEL_GUI_HANDLE_NONE;

    Mel_Gui_Class* most_derived = mel__gui_class_get(chain[0]);

    Mel_Gui_Window w0 = {0};
    w0.class_atom = desc->class_atom;
    w0.parent     = desc->parent;
    w0.id         = desc->id;
    w0.style      = desc->style | most_derived->style;
    w0.user       = desc->user;
    mel_array_init(&w0.chain, mel__gui_alloc());
    for (u32 i = chain_len; i > 0; i--) {
        Mel_Gui_Class* c = mel__gui_class_get(chain[i - 1u]);
        if (c != NULL && c->proc != NULL) {
            mel_array_push(&w0.chain, c->proc);
        }
    }
    if (desc->proc != NULL && (w0.chain.count == 0 || mel_array_last(&w0.chain) != desc->proc)) {
        mel_array_push(&w0.chain, desc->proc);
    }

    Mel_SlotMap_Handle sh = mel_slotmap_insert(&mel__gui_windows, &w0);
    Mel_Gui_Handle h = (Mel_Gui_Handle){ .handle = sh };

    Mel_Gui_Window* w = (Mel_Gui_Window*)mel_slotmap_get(&mel__gui_windows, sh);
    if (w == NULL) {
        mel_array_free(&w0.chain);
        return MEL_GUI_HANDLE_NONE;
    }
    w->self = h;

    Mel_Gui_Result nc = mel__gui_dispatch(w, MEL_GUI_MSG_NCCREATE, 0, (Mel_Gui_LParam)(intptr_t)desc);
    if (nc < 0) {
        w = (Mel_Gui_Window*)mel_slotmap_get(&mel__gui_windows, sh);
        if (w != NULL) mel_array_free(&w->chain);
        mel_slotmap_remove(&mel__gui_windows, sh);
        return MEL_GUI_HANDLE_NONE;
    }

    Mel_Atom platform_class = chain[chain_len - 1u];
    void* native = mel_gui_platform_create(h, desc, platform_class);
    if (native == NULL) {
        w = (Mel_Gui_Window*)mel_slotmap_get(&mel__gui_windows, sh);
        if (w != NULL) {
            mel__gui_dispatch(w, MEL_GUI_MSG_NCDESTROY, 0, 0);
            mel_array_free(&w->chain);
        }
        mel_slotmap_remove(&mel__gui_windows, sh);
        return MEL_GUI_HANDLE_NONE;
    }
    w = (Mel_Gui_Window*)mel_slotmap_get(&mel__gui_windows, sh);
    w->native = native;

    mel__gui_dispatch(w, MEL_GUI_MSG_CREATE, 0, (Mel_Gui_LParam)(intptr_t)desc);
    return h;
}

bool mel_gui_destroy(Mel_Gui_Handle h)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    if (w == NULL) return false;

    if (mel_gui_handle_eq(mel__gui_focus, h)) mel__gui_focus = MEL_GUI_HANDLE_NONE;

    mel__gui_dispatch(w, MEL_GUI_MSG_DESTROY, 0, 0);

    u32 count = mel_slotmap_count(&mel__gui_windows);
    Mel_Gui_Window* all = (Mel_Gui_Window*)mel_slotmap_data(&mel__gui_windows);
    for (u32 i = 0; i < count; ) {
        if (mel_gui_handle_eq(all[i].parent, h)) {
            Mel_Gui_Handle child = all[i].self;
            mel_gui_destroy(child);
            count = mel_slotmap_count(&mel__gui_windows);
            all = (Mel_Gui_Window*)mel_slotmap_data(&mel__gui_windows);
            continue;
        }
        i += 1;
    }

    w = mel__gui_window_get(h);
    if (w == NULL) return true;

    mel__gui_dispatch(w, MEL_GUI_MSG_NCDESTROY, 0, 0);

    mel_gui_platform_destroy(h);

    w = mel__gui_window_get(h);
    if (w != NULL) {
        mel_array_free(&w->chain);
    }
    mel_slotmap_remove(&mel__gui_windows, h.handle);
    return true;
}

bool mel_gui_alive(Mel_Gui_Handle h)
{
    if (!mel__gui_initialized) return false;
    mel_gui_assert_ui_thread();
    return mel_slotmap_alive(&mel__gui_windows, h.handle);
}

void mel_gui_destroy_all_roots(void)
{
    if (!mel__gui_initialized) return;
    mel_gui_assert_ui_thread();

    for (;;) {
        Mel_Gui_Handle root = MEL_GUI_HANDLE_NONE;
        u32 count = mel_slotmap_count(&mel__gui_windows);
        Mel_Gui_Window* all = (Mel_Gui_Window*)mel_slotmap_data(&mel__gui_windows);
        for (u32 i = 0; i < count; i++) {
            if (mel_gui_handle_is_none(all[i].parent)) {
                root = all[i].self;
                break;
            }
        }
        if (mel_gui_handle_is_none(root)) break;
        mel_gui_destroy(root);
    }
}

bool mel_gui_set_window_pos(Mel_Gui_Handle h, i32 x, i32 y, i32 w, i32 hgt, u32 flags)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* win = mel__gui_window_get(h);
    if (win == NULL) return false;
    if (!mel_gui_platform_set_window_pos(h, x, y, w, hgt, flags)) return false;

    if (!(flags & MEL_GUI_SWP_NOMOVE)) {
        mel__gui_dispatch(win, MEL_GUI_MSG_MOVE, 0, mel_gui_pack_xy(x, y));
    }
    if (!(flags & MEL_GUI_SWP_NOSIZE)) {
        mel__gui_dispatch(win, MEL_GUI_MSG_SIZE, 0, mel_gui_pack_wh(w, hgt));
    }
    if (flags & MEL_GUI_SWP_SHOW)    mel__gui_dispatch(win, MEL_GUI_MSG_SHOW, 1, 0);
    if (flags & MEL_GUI_SWP_HIDE)    mel__gui_dispatch(win, MEL_GUI_MSG_HIDE, 0, 0);
    if (flags & MEL_GUI_SWP_ENABLE)  mel__gui_dispatch(win, MEL_GUI_MSG_ENABLE, 1, 0);
    if (flags & MEL_GUI_SWP_DISABLE) mel__gui_dispatch(win, MEL_GUI_MSG_ENABLE, 0, 0);
    return true;
}

size mel_gui_get_text(Mel_Gui_Handle h, char* buf, size cap)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Result r = mel_gui_send_message(h, MEL_GUI_MSG_GET_TEXT, (Mel_Gui_WParam)cap, (Mel_Gui_LParam)(intptr_t)buf);
    return r < 0 ? 0 : (size)r;
}

bool mel_gui_invalidate(Mel_Gui_Handle h)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    if (w == NULL) return false;
    return mel_gui_platform_invalidate(h);
}

bool mel_gui_invalidate_rect(Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 hgt)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    if (w == NULL) return false;
    return mel_gui_platform_invalidate_rect(h, x, y, width, hgt);
}

bool mel_gui_set_text(Mel_Gui_Handle h, str8 text)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    if (w == NULL) return false;
    if (!mel_gui_platform_set_text(h, text)) return false;
    mel__gui_dispatch(w, MEL_GUI_MSG_SET_TEXT, (Mel_Gui_WParam)(usize)text.len, (Mel_Gui_LParam)(intptr_t)text.data);
    return true;
}

Mel_Gui_Handle mel_gui_parent(Mel_Gui_Handle h)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    return w ? w->parent : MEL_GUI_HANDLE_NONE;
}

u32 mel_gui_id(Mel_Gui_Handle h)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    return w ? w->id : MEL_GUI_ID_NONE;
}

u32 mel_gui_style(Mel_Gui_Handle h)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    return w ? w->style : 0u;
}

void* mel_gui_user(Mel_Gui_Handle h)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    return w ? w->user : NULL;
}

void mel_gui_set_user(Mel_Gui_Handle h, void* user)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    if (w != NULL) w->user = user;
}

void* mel_gui_native(Mel_Gui_Handle h)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    return w ? w->native : NULL;
}

Mel_Atom mel_gui_class_of(Mel_Gui_Handle h)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    return w ? w->class_atom : MEL_ATOM_NONE;
}

void* mel_gui_platform_native(Mel_Gui_Handle h)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    return w ? w->native : NULL;
}

void mel_gui_platform_bind_native(Mel_Gui_Handle h, void* native)
{
    mel_gui_assert_ui_thread();
    Mel_Gui_Window* w = mel__gui_window_get(h);
    if (w != NULL) w->native = native;
}
