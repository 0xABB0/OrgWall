#include <gui.control/slider.h>

#include <core/compiler.h>
#include <string/str8.h>

static Mel_Atom mel__slider_atom;

extern void mel_gui_slider_platform_register(Mel_Atom atom);

static Mel_Gui_Result mel__slider_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_VALUE_CHANGED) {
        Mel_Gui_Handle parent = mel_gui_parent(h);
        if (!mel_gui_handle_is_none(parent)) {
            mel_gui_send_message(parent, MEL_GUI_MSG_COMMAND, (Mel_Gui_WParam)mel_gui_id(h), (Mel_Gui_LParam)(intptr_t)&h);
        }
    }
    return mel_gui_call_super(h, msg, w, l);
}

MEL_CONSTRUCTOR
static void mel__slider_register(void)
{
    mel__slider_atom = mel_gui_register_class(&(Mel_Gui_Class_Desc){
        .name  = MEL_GUI_CLASS_SLIDER,
        .proc  = mel__slider_proc,
        .style = MEL_GUI_WS_CHILD | MEL_GUI_WS_VISIBLE | MEL_GUI_WS_TABSTOP,
    });
    mel_gui_slider_platform_register(mel__slider_atom);
}

Mel_Atom mel_gui_slider_atom(void)
{
    if (mel__slider_atom == MEL_ATOM_NONE) mel__slider_register();
    return mel__slider_atom;
}

Mel_Gui_Handle mel_gui_create_slider(Mel_Gui_Handle parent, u32 id, i32 x, i32 y, i32 w, i32 h, Mel_Gui_Proc proc, void* user)
{
    return mel_gui_create(&(Mel_Gui_Create_Desc){
        .class_atom = mel_gui_slider_atom(),
        .text   = STR8_EMPTY,
        .style  = MEL_GUI_WS_CHILD | MEL_GUI_WS_VISIBLE | MEL_GUI_WS_TABSTOP,
        .x = x, .y = y, .w = w, .h = h,
        .parent = parent,
        .id     = id,
        .proc   = proc,
        .user   = user,
    });
}
