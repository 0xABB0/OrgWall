#include <gui/gui.platform.h>

#include <allocator.heap/heap.h>
#include <collection.array/array.h>
#include <stdlib.h>

typedef struct Mel_Gui_Class_Storage {
    char name[64];
    char base_name[64];
    u32 style;
    Mel_Gui_Proc proc;
} Mel_Gui_Class_Storage;

static Mel_Array(Mel_Gui_Class_Storage) mel__gui_classes;
static bool mel__gui_initialized;

static void mel__gui_ensure_classes(void)
{
    if (mel__gui_classes.allocator == NULL) {
        mel_array_init(&mel__gui_classes, mel_alloc_heap());
    }
}

static void mel__gui_cstr_from_str8(str8 s, char* buf, size buf_size)
{
    if (buf_size == 0) return;
    size n = str8_to_buf(s, buf, buf_size - 1);
    buf[n] = 0;
}

static Mel_Gui_Class_Storage* mel__gui_class_storage_from_name(str8 name)
{
    if (mel__gui_classes.allocator == NULL) return NULL;

    for (usize i = 0; i < mel__gui_classes.count; i += 1) {
        if (str8_equals(str8_from_cstr(mel__gui_classes.items[i].name), name)) {
            return &mel__gui_classes.items[i];
        }
    }
    return NULL;
}

bool mel_gui_init(void)
{
    if (!mel__gui_initialized) {
        mel__gui_ensure_classes();
        if (!mel_gui_platform_init()) return false;
        mel__gui_initialized = true;
    }
    return true;
}

void mel_gui_shutdown(void)
{
    if (mel__gui_initialized) {
        mel_gui_platform_shutdown();
        mel__gui_initialized = false;
    }

    if (mel__gui_classes.allocator != NULL) {
        mel_array_free(&mel__gui_classes);
        mel__gui_classes.allocator = NULL;
    }
}

Mel_Gui_Atom mel_gui_register_class(const Mel_Gui_Class_Desc* desc)
{
    if (desc == NULL || str8_is_empty(desc->name)) return MEL_GUI_ATOM_NONE;

    mel__gui_ensure_classes();
    Mel_Gui_Class_Storage* cls = mel__gui_class_storage_from_name(desc->name);
    if (cls == NULL) {
        mel_array_push(&mel__gui_classes, ((Mel_Gui_Class_Storage){0}));
        cls = &mel_array_last(&mel__gui_classes);
    }

    mel__gui_cstr_from_str8(desc->name, cls->name, (size)sizeof(cls->name));
    mel__gui_cstr_from_str8(str8_is_empty(desc->base_name) ? desc->name : desc->base_name, cls->base_name, (size)sizeof(cls->base_name));
    cls->style = desc->style;
    cls->proc = desc->proc;

    return (Mel_Gui_Atom)(cls - mel__gui_classes.items + 1);
}

Mel_Gui_Handle mel_gui_create(const Mel_Gui_Create_Desc* desc)
{
    if (desc == NULL || str8_is_empty(desc->class_name)) return NULL;
    if (!mel_gui_init()) return NULL;

    Mel_Gui_Class_Storage* cls = mel__gui_class_storage_from_name(desc->class_name);
    str8 platform_class_name = desc->class_name;
    if (cls != NULL && cls->base_name[0] != 0) {
        platform_class_name = str8_from_cstr(cls->base_name);
    }

    Mel_Gui_Handle h = calloc(1, sizeof(*h));
    if (h == NULL) return NULL;

    h->parent = desc->parent;
    h->id = desc->id;
    h->class_atom = cls ? (Mel_Gui_Atom)(cls - mel__gui_classes.items + 1) : MEL_GUI_ATOM_NONE;
    h->proc = desc->proc ? desc->proc : (cls ? cls->proc : NULL);
    h->user = desc->user;

    if (!mel_gui_platform_realize(h, desc, platform_class_name)) {
        free(h);
        return NULL;
    }

    mel_gui_send_message(h, MEL_GUI_MSG_CREATE, 0, 0);
    return h;
}

void mel_gui_destroy(Mel_Gui_Handle h)
{
    if (h == NULL) return;

    mel_gui_send_message(h, MEL_GUI_MSG_DESTROY, 0, 0);
    mel_gui_platform_destroy(h);
    free(h);
}

Mel_Gui_Result mel_gui_send_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam)
{
    if (h != NULL && h->proc != NULL) {
        return h->proc(h, msg, wparam, lparam);
    }
    return 0;
}

bool mel_gui_post_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam)
{
    mel_gui_send_message(h, msg, wparam, lparam);
    return true;
}

bool mel_gui_get_message(Mel_Gui_Message* out)
{
    MEL_UNUSED(out);
    return false;
}

Mel_Gui_Result mel_gui_dispatch_message(const Mel_Gui_Message* message)
{
    if (message == NULL) return 0;
    return mel_gui_send_message(message->h, message->msg, message->wparam, message->lparam);
}

void mel_gui_show(Mel_Gui_Handle h, bool visible)
{
    if (h == NULL) return;
    mel_gui_platform_show(h, visible);
    mel_gui_send_message(h, visible ? MEL_GUI_MSG_SHOW : MEL_GUI_MSG_HIDE, visible ? 1 : 0, 0);
}

void mel_gui_enable(Mel_Gui_Handle h, bool enabled)
{
    if (h == NULL) return;
    mel_gui_platform_enable(h, enabled);
    mel_gui_send_message(h, MEL_GUI_MSG_ENABLE, enabled ? 1 : 0, 0);
}

void mel_gui_set_text(Mel_Gui_Handle h, str8 text)
{
    if (h == NULL) return;
    mel_gui_platform_set_text(h, text);
    mel_gui_send_message(h, MEL_GUI_MSG_SET_TEXT, 0, (Mel_Gui_LParam)(intptr_t)&text);
}

void mel_gui_set_rect(Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 height)
{
    if (h == NULL) return;
    mel_gui_platform_set_rect(h, x, y, width, height);
    mel_gui_send_message(h, MEL_GUI_MSG_MOVE, 0, 0);
    mel_gui_send_message(h, MEL_GUI_MSG_SIZE, (Mel_Gui_WParam)(u32)width, (Mel_Gui_LParam)height);
}

void* mel_gui_native_handle(Mel_Gui_Handle h)
{
    return h ? h->platform_handle : NULL;
}

Mel_Gui_Handle mel_gui_parent(Mel_Gui_Handle h)
{
    return h ? h->parent : NULL;
}

u32 mel_gui_id(Mel_Gui_Handle h)
{
    return h ? h->id : MEL_GUI_ID_NONE;
}

void* mel_gui_user(Mel_Gui_Handle h)
{
    return h ? h->user : NULL;
}

void mel_gui_set_user(Mel_Gui_Handle h, void* user)
{
    if (h != NULL) h->user = user;
}

void mel_gui__set_platform_handle(Mel_Gui_Handle h, void* platform_handle)
{
    if (h != NULL) h->platform_handle = platform_handle;
}

void* mel_gui__platform_handle(Mel_Gui_Handle h)
{
    return h ? h->platform_handle : NULL;
}

#if defined(__GNUC__) || defined(__clang__)
#define MEL_GUI_WEAK __attribute__((weak))
#else
#define MEL_GUI_WEAK
#endif

MEL_GUI_WEAK bool mel_gui_platform_init(void) { return true; }
MEL_GUI_WEAK void mel_gui_platform_shutdown(void) {}
MEL_GUI_WEAK bool mel_gui_platform_realize(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc, str8 platform_class_name)
{
    MEL_UNUSED(h);
    MEL_UNUSED(desc);
    MEL_UNUSED(platform_class_name);
    return false;
}
MEL_GUI_WEAK void mel_gui_platform_destroy(Mel_Gui_Handle h) { MEL_UNUSED(h); }
MEL_GUI_WEAK void mel_gui_platform_show(Mel_Gui_Handle h, bool visible) { MEL_UNUSED(h); MEL_UNUSED(visible); }
MEL_GUI_WEAK void mel_gui_platform_enable(Mel_Gui_Handle h, bool enabled) { MEL_UNUSED(h); MEL_UNUSED(enabled); }
MEL_GUI_WEAK void mel_gui_platform_set_text(Mel_Gui_Handle h, str8 text) { MEL_UNUSED(h); MEL_UNUSED(text); }
MEL_GUI_WEAK void mel_gui_platform_set_rect(Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 height)
{
    MEL_UNUSED(h);
    MEL_UNUSED(x);
    MEL_UNUSED(y);
    MEL_UNUSED(width);
    MEL_UNUSED(height);
}
