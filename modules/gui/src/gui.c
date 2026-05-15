#include <gui/gui.h>

static const Mel_Gui_Backend* mel__gui_backend;

bool mel_gui_init(const Mel_Gui_Backend* backend)
{
    if (backend == NULL) return false;
    if (mel__gui_backend != NULL) return true;

    mel__gui_backend = backend;
    if (mel__gui_backend->init && !mel__gui_backend->init()) {
        mel__gui_backend = NULL;
        return false;
    }

    return true;
}

void mel_gui_shutdown(void)
{
    if (mel__gui_backend && mel__gui_backend->shutdown) {
        mel__gui_backend->shutdown();
    }
    mel__gui_backend = NULL;
}

Mel_Gui_Atom mel_gui_register_class(const Mel_Gui_Class_Desc* desc)
{
    if (mel__gui_backend == NULL || mel__gui_backend->register_class == NULL) return MEL_GUI_ATOM_NONE;
    return mel__gui_backend->register_class(desc);
}

Mel_Gui_Handle mel_gui_create(const Mel_Gui_Create_Desc* desc)
{
    if (mel__gui_backend == NULL || mel__gui_backend->create == NULL) return NULL;
    return mel__gui_backend->create(desc);
}

void mel_gui_destroy(Mel_Gui_Handle h)
{
    if (mel__gui_backend && mel__gui_backend->destroy) {
        mel__gui_backend->destroy(h);
    }
}

Mel_Gui_Result mel_gui_send_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam)
{
    if (mel__gui_backend == NULL || mel__gui_backend->send_message == NULL) return 0;
    return mel__gui_backend->send_message(h, msg, wparam, lparam);
}

bool mel_gui_post_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam)
{
    if (mel__gui_backend == NULL || mel__gui_backend->post_message == NULL) return false;
    return mel__gui_backend->post_message(h, msg, wparam, lparam);
}

bool mel_gui_get_message(Mel_Gui_Message* out)
{
    if (mel__gui_backend == NULL || mel__gui_backend->get_message == NULL) return false;
    return mel__gui_backend->get_message(out);
}

Mel_Gui_Result mel_gui_dispatch_message(const Mel_Gui_Message* message)
{
    if (mel__gui_backend == NULL || mel__gui_backend->dispatch_message == NULL) return 0;
    return mel__gui_backend->dispatch_message(message);
}

void mel_gui_show(Mel_Gui_Handle h, bool visible)
{
    if (mel__gui_backend && mel__gui_backend->show) {
        mel__gui_backend->show(h, visible);
    }
}

void mel_gui_enable(Mel_Gui_Handle h, bool enabled)
{
    if (mel__gui_backend && mel__gui_backend->enable) {
        mel__gui_backend->enable(h, enabled);
    }
}

void mel_gui_set_text(Mel_Gui_Handle h, str8 text)
{
    if (mel__gui_backend && mel__gui_backend->set_text) {
        mel__gui_backend->set_text(h, text);
    }
}

void mel_gui_set_rect(Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 height)
{
    if (mel__gui_backend && mel__gui_backend->set_rect) {
        mel__gui_backend->set_rect(h, x, y, width, height);
    }
}

void* mel_gui_native_handle(Mel_Gui_Handle h)
{
    if (mel__gui_backend == NULL || mel__gui_backend->native_handle == NULL) return NULL;
    return mel__gui_backend->native_handle(h);
}
