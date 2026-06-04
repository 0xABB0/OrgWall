#include "linux.h"

#include <log/log.h>

#include <dlfcn.h>
#include <string.h>

static Mel_Xcb_State g_xcb;

Mel_Xcb_State* mel_gui__xcb(void) { return &g_xcb; }

static void* mel_gui__xcb_sym(void* lib, const char* name)
{
    void* s = dlsym(lib, name);
    if (!s)
        mel_log_error("gui", "xcb backend: symbol '%s' missing from libxcb", name);
    return s;
}

static bool mel_gui__xcb_load(Mel_Xcb_State* x)
{
    x->lib = dlopen("libxcb.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!x->lib)
        x->lib = dlopen("libxcb.so", RTLD_NOW | RTLD_GLOBAL);
    if (!x->lib)
    {
        mel_log_error("gui", "xcb backend: cannot dlopen libxcb.so.1 (%s); no X11 connection possible", dlerror());
        return false;
    }

    mel_xcb_api* a = &x->api;
    a->connect = (mel_xcb_connection * (*)(const char*, int*)) mel_gui__xcb_sym(x->lib, "xcb_connect");
    a->connection_has_error = (int (*)(mel_xcb_connection*))mel_gui__xcb_sym(x->lib, "xcb_connection_has_error");
    a->disconnect = (void (*)(mel_xcb_connection*))mel_gui__xcb_sym(x->lib, "xcb_disconnect");
    a->get_file_descriptor = (int (*)(mel_xcb_connection*))mel_gui__xcb_sym(x->lib, "xcb_get_file_descriptor");
    a->flush = (int (*)(mel_xcb_connection*))mel_gui__xcb_sym(x->lib, "xcb_flush");
    a->generate_id = (u32 (*)(mel_xcb_connection*))mel_gui__xcb_sym(x->lib, "xcb_generate_id");
    a->get_setup = (const mel_xcb_setup* (*)(mel_xcb_connection*))mel_gui__xcb_sym(x->lib, "xcb_get_setup");
    a->setup_roots_iterator = (mel_xcb_screen_iterator (*)(const mel_xcb_setup*))mel_gui__xcb_sym(x->lib, "xcb_setup_roots_iterator");
    a->screen_next = (void (*)(mel_xcb_screen_iterator*))mel_gui__xcb_sym(x->lib, "xcb_screen_next");
    a->poll_for_event = (mel_xcb_generic_event * (*)(mel_xcb_connection*)) mel_gui__xcb_sym(x->lib, "xcb_poll_for_event");
    a->create_window = (mel_xcb_void_cookie (*)(mel_xcb_connection*, u8, mel_xcb_window, mel_xcb_window, i16, i16, u16, u16, u16, u16, mel_xcb_visualid, u32, const void*))mel_gui__xcb_sym(x->lib, "xcb_create_window");
    a->destroy_window = (mel_xcb_void_cookie (*)(mel_xcb_connection*, mel_xcb_window))mel_gui__xcb_sym(x->lib, "xcb_destroy_window");
    a->map_window = (mel_xcb_void_cookie (*)(mel_xcb_connection*, mel_xcb_window))mel_gui__xcb_sym(x->lib, "xcb_map_window");
    a->unmap_window = (mel_xcb_void_cookie (*)(mel_xcb_connection*, mel_xcb_window))mel_gui__xcb_sym(x->lib, "xcb_unmap_window");
    a->configure_window = (mel_xcb_void_cookie (*)(mel_xcb_connection*, mel_xcb_window, u16, const void*))mel_gui__xcb_sym(x->lib, "xcb_configure_window");
    a->change_property = (mel_xcb_void_cookie (*)(mel_xcb_connection*, u8, mel_xcb_window, mel_xcb_atom, mel_xcb_atom, u8, u32, const void*))mel_gui__xcb_sym(x->lib, "xcb_change_property");
    a->change_window_attributes = (mel_xcb_void_cookie (*)(mel_xcb_connection*, mel_xcb_window, u32, const void*))mel_gui__xcb_sym(x->lib, "xcb_change_window_attributes");
    a->set_input_focus = (mel_xcb_void_cookie (*)(mel_xcb_connection*, u8, mel_xcb_window, u32))mel_gui__xcb_sym(x->lib, "xcb_set_input_focus");
    a->intern_atom = (mel_xcb_intern_atom_cookie (*)(mel_xcb_connection*, u8, u16, const char*))mel_gui__xcb_sym(x->lib, "xcb_intern_atom");
    a->intern_atom_reply = (mel_xcb_intern_atom_reply * (*)(mel_xcb_connection*, mel_xcb_intern_atom_cookie, void*)) mel_gui__xcb_sym(x->lib, "xcb_intern_atom_reply");

    return a->connect && a->connection_has_error && a->disconnect && a->get_file_descriptor && a->flush && a->generate_id && a->get_setup && a->setup_roots_iterator && a->screen_next && a->poll_for_event && a->create_window &&
           a->destroy_window && a->map_window && a->unmap_window && a->configure_window && a->change_property && a->change_window_attributes && a->set_input_focus && a->intern_atom && a->intern_atom_reply;
}

static mel_xcb_atom mel_gui__xcb_atom(Mel_Xcb_State* x, const char* name)
{
    mel_xcb_intern_atom_cookie c = x->api.intern_atom(x->conn, 0, (u16)strlen(name), name);
    mel_xcb_intern_atom_reply* r = x->api.intern_atom_reply(x->conn, c, NULL);
    if (!r)
        return 0;
    mel_xcb_atom a = r->atom;
    free(r);
    return a;
}

Mel_Gui_Handle mel_gui__xcb_handle_of_window(mel_xcb_window w)
{
    u32           count = 0;
    Mel_Gui_Node* data = mel_gui__nodes(&count);
    for (u32 i = 0; i < count; i++)
        if (data[i].native && (mel_xcb_window)(uintptr_t)data[i].native == w)
            return data[i].self;
    return MEL_GUI_HANDLE_NONE;
}

static void mel_gui__xcb_dispatch_event(Mel_Xcb_State* x, mel_xcb_generic_event* ev)
{
    u8 type = ev->response_type & 0x7f;
    switch (type)
    {
    case MEL_XCB_CONFIGURE_NOTIFY:
    {
        mel_xcb_configure_notify_event* c = (mel_xcb_configure_notify_event*)ev;
        Mel_Gui_Handle                  h = mel_gui__xcb_handle_of_window(c->window);
        if (mel_gui_handle_is_none(h))
            break;
        Mel_Gui_Node* n = mel_gui__node(h);
        if (!n || (n->width == c->width && n->height == c->height))
            break;
        mel_gui__resized(h, c->width, c->height);
        mel_gui__xcb_view_resized(c->window, c->width, c->height);
        break;
    }
    case MEL_XCB_BUTTON_PRESS:
    case MEL_XCB_BUTTON_RELEASE:
    case MEL_XCB_MOTION_NOTIFY:
    {
        mel_xcb_input_event* in = (mel_xcb_input_event*)ev;
        mel_gui__xcb_view_pointer(in->event, type, in->event_x, in->event_y);
        break;
    }
    case MEL_XCB_CLIENT_MESSAGE:
    {
        mel_xcb_client_message_event* m = (mel_xcb_client_message_event*)ev;
        if (m->data32[0] != x->wm_delete_window)
            break;
        Mel_Gui_Handle h = mel_gui__xcb_handle_of_window(m->window);
        if (mel_gui_handle_is_none(h))
            break;
        mel_gui__frame_closed(h);
        Mel_Gui_Node* n = mel_gui__node(h);
        if (n)
            n->native = NULL;
        x->api.destroy_window(x->conn, m->window);
        mel_gui__destroy_tree(h);
        if (mel_gui__frames_dec() == 0)
            mel_reactor_quit(mel_reactor_source_reactor(x->source));
        break;
    }
    default:
        break;
    }
}

static bool xcb_source_prepare(Mel_Reactor_Source* source, i32* timeout)
{
    (void)source;
    *timeout = MEL_REACTOR_FOREVER;
    g_xcb.api.flush(g_xcb.conn);
    return false;
}

static bool xcb_source_check(Mel_Reactor_Source* source)
{
    if (source->poll_count == 0 || !source->polls[0])
        return false;
    return (source->polls[0]->revents & (MEL_REACTOR_POLL_IN | MEL_REACTOR_POLL_HUP | MEL_REACTOR_POLL_ERR)) != 0;
}

static bool xcb_source_dispatch(Mel_Reactor_Source* source, Mel_Reactor_Source_Proc callback, void* user)
{
    (void)source;
    (void)callback;
    (void)user;
    for (mel_xcb_generic_event* ev; (ev = g_xcb.api.poll_for_event(g_xcb.conn));)
    {
        mel_gui__xcb_dispatch_event(&g_xcb, ev);
        free(ev);
    }
    if (g_xcb.api.connection_has_error(g_xcb.conn))
    {
        mel_log_error("gui", "xcb backend: X connection lost");
        return false;
    }
    g_xcb.api.flush(g_xcb.conn);
    return true;
}

static const Mel_Reactor_Source_Callbacks g_xcb_source_cb = {
    .prepare = xcb_source_prepare,
    .check = xcb_source_check,
    .dispatch = xcb_source_dispatch,
    .finalize = NULL,
};

static Mel_Reactor_Poll g_xcb_poll;

bool mel_gui__backend_init(void)
{
    Mel_Xcb_State* x = &g_xcb;
    if (x->ok)
        return true;

    if (!mel_gui__xcb_load(x))
        return false;

    int screen_index = 0;
    x->conn = x->api.connect(NULL, &screen_index);
    if (!x->conn || x->api.connection_has_error(x->conn))
    {
        mel_log_error("gui", "xcb backend: xcb_connect failed (no DISPLAY or X server unreachable)");
        return false;
    }

    mel_xcb_screen_iterator it = x->api.setup_roots_iterator(x->api.get_setup(x->conn));
    for (int i = 0; i < screen_index && it.rem; i++)
        x->api.screen_next(&it);
    x->screen = it.rem ? it.data : NULL;
    if (!x->screen)
    {
        mel_log_error("gui", "xcb backend: no X screen found");
        return false;
    }
    x->root = x->screen->root;
    x->visual = x->screen->root_visual;
    x->depth = x->screen->root_depth;
    x->black_pixel = x->screen->black_pixel;

    x->wm_protocols = mel_gui__xcb_atom(x, "WM_PROTOCOLS");
    x->wm_delete_window = mel_gui__xcb_atom(x, "WM_DELETE_WINDOW");

    Mel_Reactor* reactor = mel_gui__reactor();
    if (!reactor)
    {
        mel_log_error("gui", "xcb backend: no reactor; event loop cannot be driven");
        return false;
    }

    x->source = mel_reactor_source_new(&g_xcb_source_cb, sizeof(Mel_Reactor_Source));
    g_xcb_poll = (Mel_Reactor_Poll){ .handle = x->api.get_file_descriptor(x->conn), .events = MEL_REACTOR_POLL_IN };
    mel_reactor_source_add_poll(x->source, &g_xcb_poll);
    mel_reactor_source_set_priority(x->source, MEL_REACTOR_PRIORITY_HIGH);
    mel_reactor_source_attach(reactor, x->source);

    x->ok = true;
    mel_log_info("gui", "xcb backend: connected to X server, screen %dx%d depth %u", x->screen->width_in_pixels, x->screen->height_in_pixels, x->depth);
    return true;
}

void mel_gui__backend_destroy(Mel_Gui_Node* n)
{
    if (!n || !n->native)
        return;
    Mel_Xcb_State* x = &g_xcb;
    if (!x->ok)
    {
        n->native = NULL;
        return;
    }
    mel_xcb_window w = (mel_xcb_window)(uintptr_t)n->native;
    mel_gui__xcb_view_drop(w);
    x->api.destroy_window(x->conn, w);
    x->api.flush(x->conn);
    n->native = NULL;
}

mel_xcb_window mel_gui__xcb_parent_window(Mel_Gui_Node* n)
{
    Mel_Gui_Node* p = mel_gui__node(n->parent);
    if (!p)
        return g_xcb.root;
    if (p->content)
        return (mel_xcb_window)(uintptr_t)p->content;
    if (p->native)
        return (mel_xcb_window)(uintptr_t)p->native;
    return g_xcb.root;
}

mel_xcb_window mel_gui__xcb_create_child(Mel_Gui_Node* n, u32 extra_event_mask)
{
    Mel_Xcb_State* x = &g_xcb;
    if (!x->ok)
        return 0;

    mel_xcb_window parent = mel_gui__xcb_parent_window(n);
    mel_xcb_window wid = x->api.generate_id(x->conn);

    u32 mask = MEL_XCB_CW_BACK_PIXEL | MEL_XCB_CW_EVENT_MASK;
    u32 values[2] = { x->black_pixel, MEL_XCB_EVENT_MASK_STRUCTURE_NOTIFY | extra_event_mask };

    u16 w = (u16)(n->width > 0 ? n->width : 1);
    u16 h = (u16)(n->height > 0 ? n->height : 1);

    x->api.create_window(x->conn, MEL_XCB_COPY_FROM_PARENT, wid, parent, (i16)n->x, (i16)n->y, w, h, 0, MEL_XCB_WINDOW_CLASS_INPUT_OUTPUT, x->visual, mask, values);
    if (!n->hidden)
        x->api.map_window(x->conn, wid);
    x->api.flush(x->conn);
    return wid;
}

bool mel_gui_supports_multi_root(void) { return true; }

void mel_gui_set_text(Mel_Gui_Handle h, str8 text)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
        return;

    if (n->is_screen)
    {
        n->screen_title = text;
        h = mel_gui__toplevel(h);
        n = mel_gui__node(h);
    }
    if (!n || !n->native || !g_xcb.ok)
        return;

    mel_xcb_window w = (mel_xcb_window)(uintptr_t)n->native;
    g_xcb.api.change_property(g_xcb.conn, MEL_XCB_PROP_MODE_REPLACE, w, MEL_XCB_ATOM_WM_NAME, MEL_XCB_ATOM_STRING, 8, (u32)(text.len > 0 ? text.len : 0), text.data);
    g_xcb.api.flush(g_xcb.conn);
}

size mel_gui_get_text(Mel_Gui_Handle h, char* buf, size cap)
{
    (void)h;
    if (buf && cap > 0)
        buf[0] = 0;
    return 0;
}

void mel_gui_set_bounds(Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 height)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
        return;
    n->x = x;
    n->y = y;
    n->width = width;
    n->height = height;
    if (!n->native || !g_xcb.ok)
        return;

    mel_xcb_window w = (mel_xcb_window)(uintptr_t)n->native;
    u32            mask = MEL_XCB_CONFIG_WINDOW_X | MEL_XCB_CONFIG_WINDOW_Y | MEL_XCB_CONFIG_WINDOW_WIDTH | MEL_XCB_CONFIG_WINDOW_HEIGHT;
    u32            values[4] = { (u32)x, (u32)y, (u32)(width > 0 ? width : 1), (u32)(height > 0 ? height : 1) };
    g_xcb.api.configure_window(g_xcb.conn, w, (u16)mask, values);
    g_xcb.api.flush(g_xcb.conn);
}

void mel_gui_set_visible(Mel_Gui_Handle h, bool visible)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
        return;
    n->hidden = !visible;
    if (!n->native || !g_xcb.ok)
        return;

    mel_xcb_window w = (mel_xcb_window)(uintptr_t)n->native;
    if (visible)
        g_xcb.api.map_window(g_xcb.conn, w);
    else
        g_xcb.api.unmap_window(g_xcb.conn, w);
    g_xcb.api.flush(g_xcb.conn);
}

void mel_gui_set_enabled(Mel_Gui_Handle h, bool enabled)
{
    (void)h;
    (void)enabled;
}

void mel_gui_set_focus(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native || !g_xcb.ok)
        return;
    mel_xcb_window w = (mel_xcb_window)(uintptr_t)n->native;
    g_xcb.api.set_input_focus(g_xcb.conn, MEL_XCB_INPUT_FOCUS_POINTER_ROOT, w, 0);
    g_xcb.api.flush(g_xcb.conn);
}

void mel_gui_invalidate(Mel_Gui_Handle h) { (void)h; }

void mel_gui__nav_replace(Mel_Gui_Handle next, Mel_Gui_Handle prev)
{
    mel_gui_set_visible(next, true);
    mel_gui_set_focus(next);
    if (!mel_gui_handle_is_none(prev))
        mel_gui_set_visible(prev, false);
}

void mel_gui__nav_back(Mel_Gui_Handle prev, Mel_Gui_Handle cur)
{
    mel_gui_set_visible(prev, true);
    mel_gui_set_focus(prev);
    if (!mel_gui_handle_is_none(cur))
        mel_gui_set_visible(cur, false);
}

void mel_gui__backend_set_content_size(Mel_Gui_Node* n, i32 w, i32 h)
{
    if (!n || !n->content || !g_xcb.ok)
        return;
    mel_xcb_window inner = (mel_xcb_window)(uintptr_t)n->content;
    u32            mask = MEL_XCB_CONFIG_WINDOW_WIDTH | MEL_XCB_CONFIG_WINDOW_HEIGHT;
    u32            values[2] = { (u32)(w > 0 ? w : 1), (u32)(h > 0 ? h : 1) };
    g_xcb.api.configure_window(g_xcb.conn, inner, (u16)mask, values);
    g_xcb.api.flush(g_xcb.conn);
}
