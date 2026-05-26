#include "web.h"

// =============================================================================
// JS-side element registry + DOM ops
// =============================================================================
//
// globalThis.MelWeb.els is a dense array of DOM elements; the index is the id
// stored in node->native. MelWeb.css turns a packed RGBA word into a CSS color.

EM_JS(void, mel_web__js_init, (void), {
    globalThis.MelWeb = {
        els: [null],
        css: (v) => 'rgba(' + ((v >>> 24) & 255) + ',' + ((v >>> 16) & 255) + ','
                  + ((v >>> 8) & 255) + ',' + (((v) & 255) / 255) + ')',
    };
    const style = document.createElement('style');
    style.textContent =
        '.mel-frame{background:#2b2b2b;color:#e0e0e0;overflow:hidden;font-size:14px}' +
        '.mel-label{display:flex;align-items:center;white-space:pre-wrap;color:#e0e0e0;font-size:14px}' +
        '.mel-button{font-size:14px;cursor:pointer}' +
        '.mel-textfield{font-size:14px}' +
        '.mel-checkbox{display:flex;align-items:center;color:#e0e0e0;font-size:14px;gap:6px}' +
        '.mel-panel{background:rgba(255,255,255,0.04);border-radius:4px}' +
        '.mel-groupbox{border:1px solid #555;border-radius:4px;color:#cfcfcf;padding:0}' +
        '.mel-groupbox > legend{padding:0 6px;font-size:13px}' +
        '.mel-groupbox-body{position:absolute;left:8px;top:24px;right:8px;bottom:8px}' +
        '.mel-scroll{overflow:auto;background:#252525;border:1px solid #444}' +
        '.mel-canvas{display:block;background:#26333f}' +
        '.mel-tabview{display:flex;flex-direction:column}' +
        '.mel-splitter{display:flex}';
    document.head.appendChild(style);
});

EM_JS(int, mel_web__el_create, (const char* tag), {
    if (!globalThis.MelWeb) return 0;
    const el = document.createElement(UTF8ToString(tag));
    el.style.position = 'absolute';
    el.style.boxSizing = 'border-box';
    el.style.margin = '0';
    const id = MelWeb.els.length;
    MelWeb.els.push(el);
    return id;
});

EM_JS(void, mel_web__el_append, (int parent, int child), {
    const c = MelWeb.els[child];
    const p = parent === 0 ? (document.getElementById('mel-root') || document.body)
                           : MelWeb.els[parent];
    if (p && c) p.appendChild(c);
});

EM_JS(void, mel_web__el_class, (int id, const char* cls), {
    const el = MelWeb.els[id]; if (el) el.className = UTF8ToString(cls);
});

EM_JS(void, mel_web__el_bounds, (int id, int x, int y, int w, int h), {
    const el = MelWeb.els[id]; if (!el) return;
    el.style.left = x + 'px'; el.style.top = y + 'px';
    el.style.width = w + 'px'; el.style.height = h + 'px';
});

EM_JS(void, mel_web__el_text, (int id, const char* s), {
    const el = MelWeb.els[id]; if (el) el.textContent = UTF8ToString(s);
});

EM_JS(int, mel_web__el_get_text, (int id, char* buf, int cap), {
    const el = MelWeb.els[id];
    const s = el ? (el.textContent || '') : '';
    stringToUTF8(s, buf, cap);
    const n = lengthBytesUTF8(s);
    return n < cap ? n : cap - 1;
});

EM_JS(void, mel_web__el_set_value, (int id, const char* s), {
    const el = MelWeb.els[id]; if (el) el.value = UTF8ToString(s);
});

EM_JS(int, mel_web__el_get_value, (int id, char* buf, int cap), {
    const el = MelWeb.els[id];
    const s = el ? (el.value || '') : '';
    stringToUTF8(s, buf, cap);
    const n = lengthBytesUTF8(s);
    return n < cap ? n : cap - 1;
});

EM_JS(void, mel_web__el_title, (const char* s), { document.title = UTF8ToString(s); });

EM_JS(void, mel_web__el_visible, (int id, int v), {
    const el = MelWeb.els[id]; if (el) el.style.display = v ? 'block' : 'none';
});

EM_JS(void, mel_web__el_enabled, (int id, int v), {
    const el = MelWeb.els[id]; if (!el) return;
    if (v) el.removeAttribute('disabled'); else el.setAttribute('disabled', '');
    el.style.opacity = v ? '1' : '0.5';
});

EM_JS(void, mel_web__el_focus, (int id), {
    const el = MelWeb.els[id]; if (el && el.focus) el.focus();
});

EM_JS(void, mel_web__el_destroy, (int id), {
    const el = MelWeb.els[id];
    if (el && el.parentNode) el.parentNode.removeChild(el);
    MelWeb.els[id] = null;
});

// --- event-listener wiring; each forwards into an exported dispatcher below ---

EM_JS(void, mel_web__on_click, (int id), {
    const el = MelWeb.els[id]; if (el) el.addEventListener('click', () => _mel_web__ev_click(id));
});
EM_JS(void, mel_web__on_input, (int id), {
    const el = MelWeb.els[id]; if (el) el.addEventListener('input', () => _mel_web__ev_input(id));
});
EM_JS(void, mel_web__on_check, (int id), {
    const el = MelWeb.els[id]; const i = el && el.querySelector('input');
    if (i) i.addEventListener('change', () => _mel_web__ev_check(id));
});
EM_JS(void, mel_web__on_slider, (int id), {
    const el = MelWeb.els[id]; if (el) el.addEventListener('input', () => _mel_web__ev_slider(id));
});
EM_JS(void, mel_web__on_focus, (int id), {
    const el = MelWeb.els[id]; if (!el) return;
    el.addEventListener('focus', () => _mel_web__ev_focus(id, 1));
    el.addEventListener('blur',  () => _mel_web__ev_focus(id, 0));
});
EM_JS(void, mel_web__on_pointer, (int id), {
    const el = MelWeb.els[id]; if (!el) return;
    const xy = (e) => {
        const r = el.getBoundingClientRect();
        return [Math.round(e.clientX - r.left), Math.round(e.clientY - r.top)];
    };
    el.addEventListener('pointerdown', (e) => {
        const p = xy(e);
        if (el.setPointerCapture) el.setPointerCapture(e.pointerId);
        _mel_web__ev_pointer(id, 0, p[0], p[1]);
    });
    el.addEventListener('pointermove', (e) => { const p = xy(e); _mel_web__ev_pointer(id, 1, p[0], p[1]); });
    el.addEventListener('pointerup',   (e) => { const p = xy(e); _mel_web__ev_pointer(id, 2, p[0], p[1]); });
});
EM_JS(void, mel_web__on_key, (int id), {
    const el = MelWeb.els[id]; if (!el) return;
    el.addEventListener('keydown', (e) => _mel_web__ev_key(id, 1, e.keyCode | 0, 0));
    el.addEventListener('keyup',   (e) => _mel_web__ev_key(id, 0, e.keyCode | 0, 0));
});

// =============================================================================
// C-side control registry, indexed by element id
// =============================================================================

static Mel_Web_Ctl* g_ctls;
static int          g_ctl_cap;

int mel_web__id_of(Mel_Gui_Node* n) { return n ? (int)(intptr_t)n->native : 0; }

int mel_web__parent_id(Mel_Gui_Node* n) {
    Mel_Gui_Node* p = mel_gui__node(n->parent);
    if (!p) return 0;
    return (int)(intptr_t)(p->content ? p->content : p->native);
}

Mel_Web_Ctl* mel_web__ctl(int id) {
    if (id <= 0 || id >= g_ctl_cap || !g_ctls[id].used) return NULL;
    return &g_ctls[id];
}

Mel_Web_Ctl* mel_web__ctl_new(int id, Mel_Gui_Handle h) {
    if (id <= 0) return NULL;
    if (id >= g_ctl_cap) {
        int ncap = g_ctl_cap ? g_ctl_cap * 2 : 64;
        while (ncap <= id) ncap *= 2;
        Mel_Web_Ctl* grown = realloc(g_ctls, (size_t)ncap * sizeof *grown);
        if (!grown) return NULL;
        memset(grown + g_ctl_cap, 0, (size_t)(ncap - g_ctl_cap) * sizeof *grown);
        g_ctls = grown;
        g_ctl_cap = ncap;
    }
    Mel_Web_Ctl* c = &g_ctls[id];
    memset(c, 0, sizeof *c);
    c->handle = h;
    c->used = true;
    return c;
}

// =============================================================================
// Event dispatchers (called from JS listeners)
// =============================================================================

static Mel_Key web_key(int code) {
    if ((code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z')) return (Mel_Key)code;
    switch (code) {
        case 8:  return MEL_KEY_BACKSPACE;
        case 9:  return MEL_KEY_TAB;
        case 13: return MEL_KEY_ENTER;
        case 27: return MEL_KEY_ESCAPE;
        case 32: return MEL_KEY_SPACE;
        case 37: return MEL_KEY_LEFT;
        case 38: return MEL_KEY_UP;
        case 39: return MEL_KEY_RIGHT;
        case 40: return MEL_KEY_DOWN;
        case 36: return MEL_KEY_HOME;
        case 35: return MEL_KEY_END;
        case 33: return MEL_KEY_PAGE_UP;
        case 34: return MEL_KEY_PAGE_DOWN;
        case 45: return MEL_KEY_INSERT;
        case 46: return MEL_KEY_DELETE;
        default: return MEL_KEY_NONE;
    }
}

EMSCRIPTEN_KEEPALIVE void mel_web__ev_click(int id) {
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (c && c->pointer.on_click) c->pointer.on_click(c->handle, mel_gui_user(c->handle));
}

EMSCRIPTEN_KEEPALIVE void mel_web__ev_input(int id) {
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (!c || !c->textfield.on_text_changed) return;
    char buf[1024];
    int n = mel_web__el_get_value(id, buf, sizeof buf);
    c->textfield.on_text_changed(c->handle, str8_from_range((u8*)buf, (u8*)buf + n),
                                 mel_gui_user(c->handle));
}

EMSCRIPTEN_KEEPALIVE void mel_web__ev_check(int id) {
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (c && c->checkbox.on_toggled)
        c->checkbox.on_toggled(c->handle, mel_web__checkbox_get(id) != 0, mel_gui_user(c->handle));
}

EMSCRIPTEN_KEEPALIVE void mel_web__ev_slider(int id) {
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (c && c->slider.on_value_changed)
        c->slider.on_value_changed(c->handle, mel_web__slider_value(id), mel_gui_user(c->handle));
}

EMSCRIPTEN_KEEPALIVE void mel_web__ev_focus(int id, int in) {
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (!c) return;
    if (in) {
        mel_gui__set_focused(c->handle);
        if (c->focus.on_focus_in) c->focus.on_focus_in(c->handle, mel_gui_user(c->handle));
    } else {
        if (mel_gui_handle_eq(mel_gui_focused(), c->handle)) mel_gui__set_focused(MEL_GUI_HANDLE_NONE);
        if (c->focus.on_focus_out) c->focus.on_focus_out(c->handle, mel_gui_user(c->handle));
    }
}

EMSCRIPTEN_KEEPALIVE void mel_web__ev_pointer(int id, int type, int x, int y) {
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (!c) return;
    void* u = mel_gui_user(c->handle);
    switch (type) {
        case 0: if (c->pointer.on_pointer_down) c->pointer.on_pointer_down(c->handle, x, y, u); break;
        case 1: if (c->pointer.on_pointer_move) c->pointer.on_pointer_move(c->handle, x, y, u); break;
        case 2: if (c->pointer.on_pointer_up)   c->pointer.on_pointer_up(c->handle, x, y, u);   break;
    }
}

EMSCRIPTEN_KEEPALIVE void mel_web__ev_key(int id, int down, int code, int codepoint) {
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (!c) return;
    void* u = mel_gui_user(c->handle);
    if (down) { if (c->keyboard.on_key_down) c->keyboard.on_key_down(c->handle, web_key(code), u); }
    else      { if (c->keyboard.on_key_up)   c->keyboard.on_key_up(c->handle, web_key(code), u); }
    if (down && codepoint && c->keyboard.on_char) c->keyboard.on_char(c->handle, (u32)codepoint, u);
}

EMSCRIPTEN_KEEPALIVE void mel_web__ev_select(int id, int index) {
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (c && c->on_select) c->on_select(c->handle, index, mel_gui_user(c->handle));
}

// =============================================================================
// Backend hooks + generic widget ops
// =============================================================================

bool mel_gui__backend_init(void) {
    mel_web__js_init();
    return true;
}

void mel_gui__backend_destroy(Mel_Gui_Node* n) {
    if (!n || !n->native) return;
    int id = mel_web__id_of(n);
    n->native = NULL;
    n->content = NULL;
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (c) c->used = false;
    mel_web__el_destroy(id);
}

void mel_gui_set_text(Mel_Gui_Handle h, str8 text) {
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return;
    int id = mel_web__id_of(n);
    if (!id) return;
    char buf[2048];
    mel_web__cstr(text, buf, sizeof buf);
    Mel_Web_Ctl* c = mel_web__ctl(id);
    Mel_Web_Kind k = c ? c->kind : MEL_WEB_TEXT;
    if (k == MEL_WEB_FRAME)      mel_web__el_title(buf);
    else if (k == MEL_WEB_INPUT) mel_web__el_set_value(id, buf);
    else                         mel_web__el_text(id, buf);
}

size mel_gui_get_text(Mel_Gui_Handle h, char* buf, size cap) {
    if (buf && cap > 0) buf[0] = 0;
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !buf || cap <= 0) return 0;
    int id = mel_web__id_of(n);
    if (!id) return 0;
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (c && c->kind == MEL_WEB_INPUT) return (size)mel_web__el_get_value(id, buf, (int)cap);
    return (size)mel_web__el_get_text(id, buf, (int)cap);
}

void mel_gui_set_bounds(Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 height) {
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return;
    n->x = x; n->y = y; n->width = width; n->height = height;
    int id = mel_web__id_of(n);
    if (!id) return;
    mel_web__el_bounds(id, x, y, width, height);
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (c && c->canvas.on_paint) mel_web__canvas_repaint(n);
}

void mel_gui_set_visible(Mel_Gui_Handle h, bool visible) {
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return;
    n->hidden = !visible;
    int id = mel_web__id_of(n);
    if (id) mel_web__el_visible(id, visible);
}

void mel_gui_set_enabled(Mel_Gui_Handle h, bool enabled) {
    Mel_Gui_Node* n = mel_gui__node(h);
    int id = n ? mel_web__id_of(n) : 0;
    if (id) mel_web__el_enabled(id, enabled);
}

void mel_gui_set_focus(Mel_Gui_Handle h) {
    Mel_Gui_Node* n = mel_gui__node(h);
    int id = n ? mel_web__id_of(n) : 0;
    if (id) mel_web__el_focus(id);
}

void mel_gui_invalidate(Mel_Gui_Handle h) {
    Mel_Gui_Node* n = mel_gui__node(h);
    if (n) mel_web__canvas_repaint(n);
}

void mel_gui__nav_replace(Mel_Gui_Handle next, Mel_Gui_Handle prev) {
    mel_gui_set_visible(next, true);
    mel_gui_set_focus(next);
    if (!mel_gui_handle_is_none(prev)) mel_gui_set_visible(prev, false);
}

void mel_gui__nav_back(Mel_Gui_Handle prev, Mel_Gui_Handle cur) {
    mel_gui_set_visible(prev, true);
    mel_gui_set_focus(prev);
    if (!mel_gui_handle_is_none(cur)) mel_gui_set_visible(cur, false);
}
