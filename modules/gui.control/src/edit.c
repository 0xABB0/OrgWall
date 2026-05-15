#include <gui.control/edit.h>

#include <core/compiler.h>

static Mel_Atom mel__edit_atom;

extern void mel_gui_edit_platform_register(Mel_Atom atom);

static Mel_Gui_Result mel__edit_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_TEXT_CHANGED) {
        Mel_Gui_Handle parent = mel_gui_parent(h);
        if (!mel_gui_handle_is_none(parent)) {
            mel_gui_send_message(parent, MEL_GUI_MSG_COMMAND, (Mel_Gui_WParam)mel_gui_id(h), (Mel_Gui_LParam)(intptr_t)&h);
        }
    }
    return mel_gui_call_super(h, msg, w, l);
}

MEL_CONSTRUCTOR
static void mel__edit_register(void)
{
    mel__edit_atom = mel_gui_register_class(&(Mel_Gui_Class_Desc){
        .name  = MEL_GUI_CLASS_EDIT,
        .proc  = mel__edit_proc,
        .style = MEL_GUI_WS_CHILD | MEL_GUI_WS_VISIBLE | MEL_GUI_WS_TABSTOP,
    });
    mel_gui_edit_platform_register(mel__edit_atom);
}

Mel_Atom mel_gui_edit_atom(void)
{
    if (mel__edit_atom == MEL_ATOM_NONE) mel__edit_register();
    return mel__edit_atom;
}

Mel_Gui_Handle mel_gui_create_edit(Mel_Gui_Handle parent, str8 text, u32 id, i32 x, i32 y, i32 w, i32 h, Mel_Gui_Proc proc, void* user)
{
    return mel_gui_create(&(Mel_Gui_Create_Desc){
        .class_atom = mel_gui_edit_atom(),
        .text   = text,
        .style  = MEL_GUI_WS_CHILD | MEL_GUI_WS_VISIBLE | MEL_GUI_WS_TABSTOP,
        .x = x, .y = y, .w = w, .h = h,
        .parent = parent,
        .id     = id,
        .proc   = proc,
        .user   = user,
    });
}
