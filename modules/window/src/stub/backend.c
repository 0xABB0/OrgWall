#include "../window_internal.h"

bool mel_window__backend_init(void) { return true; }

void mel_window__backend_create(Mel_Window_Node* n, const Mel_Window_Opt* o)
{
    (void)n;
    (void)o;
}

void mel_window__backend_destroy(Mel_Window_Node* n) { (void)n; }

bool mel_window_should_close(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (n && n->lifecycle.on_close_request)
        return n->lifecycle.on_close_request(w, n->user);
    return true;
}

void mel_window_set_title(Mel_Window w, str8 title)
{
    (void)w;
    (void)title;
}

void mel_window_set_bounds(Mel_Window w, i32 x, i32 y, i32 width, i32 height)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n)
        return;
    n->x = x;
    n->y = y;
    n->w = width;
    n->h = height;
}

void mel_window_set_visible(Mel_Window w, bool visible)
{
    (void)w;
    (void)visible;
}

void mel_window_set_focus(Mel_Window w) { (void)w; }

void mel_window_refresh(Mel_Window w) { (void)w; }
