#include <gui.control/label.h>

#include <core/compiler.h>

static Mel_Atom mel__label_atom;

extern void mel_gui_label_platform_register(Mel_Atom atom);

static Mel_Gui_Result mel__label_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    return mel_gui_call_super(h, msg, w, l);
}

MEL_CONSTRUCTOR
static void mel__label_register(void)
{
    mel__label_atom = mel_gui_register_class(&(Mel_Gui_Class_Desc){
        .name  = MEL_GUI_CLASS_LABEL,
        .proc  = mel__label_proc,
        .style = MEL_GUI_WS_CHILD | MEL_GUI_WS_VISIBLE,
    });
    mel_gui_label_platform_register(mel__label_atom);
}

Mel_Atom mel_gui_label_atom(void)
{
    if (mel__label_atom == MEL_ATOM_NONE) mel__label_register();
    return mel__label_atom;
}

Mel_Gui_Handle mel_gui_create_label(Mel_Gui_Handle parent, str8 text, u32 id, i32 x, i32 y, i32 w, i32 h)
{
    return mel_gui_create(&(Mel_Gui_Create_Desc){
        .class_atom = mel_gui_label_atom(),
        .text   = text,
        .style  = MEL_GUI_WS_CHILD | MEL_GUI_WS_VISIBLE,
        .x = x, .y = y, .w = w, .h = h,
        .parent = parent,
        .id     = id,
    });
}
