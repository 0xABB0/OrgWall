#include <gui.control/window.h>

#include <core/compiler.h>

static Mel_Atom mel__window_atom;

extern void mel_gui_window_platform_register(Mel_Atom atom);

static Mel_Gui_Result mel__window_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    return mel_gui_call_super(h, msg, w, l);
}

MEL_CONSTRUCTOR
static void mel__window_register(void)
{
    mel__window_atom = mel_gui_register_class(&(Mel_Gui_Class_Desc){
        .name  = MEL_GUI_CLASS_WINDOW,
        .proc  = mel__window_proc,
        .style = MEL_GUI_WS_VISIBLE,
    });
    mel_gui_window_platform_register(mel__window_atom);
}

Mel_Atom mel_gui_window_atom(void)
{
    if (mel__window_atom == MEL_ATOM_NONE) mel__window_register();
    return mel__window_atom;
}

Mel_Gui_Handle mel_gui_create_window(str8 title, i32 w, i32 h, Mel_Gui_Proc proc, void* user)
{
    return mel_gui_create(&(Mel_Gui_Create_Desc){
        .class_atom = mel_gui_window_atom(),
        .text       = title,
        .style      = MEL_GUI_WS_VISIBLE,
        .x = MEL_GUI_DEFAULT_POSITION,
        .y = MEL_GUI_DEFAULT_POSITION,
        .w = w,
        .h = h,
        .proc = proc,
        .user = user,
    });
}
