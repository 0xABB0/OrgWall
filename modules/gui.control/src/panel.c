#include <gui.control/panel.h>

#include <core/compiler.h>
#include <string/string.str8.h>

static Mel_Atom mel__panel_atom;

extern void mel_gui_panel_platform_register(Mel_Atom atom);

static Mel_Gui_Result mel__panel_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    return mel_gui_call_super(h, msg, w, l);
}

MEL_CONSTRUCTOR
static void mel__panel_register(void)
{
    mel__panel_atom = mel_gui_register_class(&(Mel_Gui_Class_Desc){
        .name  = MEL_GUI_CLASS_PANEL,
        .proc  = mel__panel_proc,
        .style = MEL_GUI_WS_CHILD | MEL_GUI_WS_VISIBLE | MEL_GUI_WS_CLIPCHILDREN,
    });
    mel_gui_panel_platform_register(mel__panel_atom);
}

Mel_Atom mel_gui_panel_atom(void)
{
    if (mel__panel_atom == MEL_ATOM_NONE) mel__panel_register();
    return mel__panel_atom;
}

Mel_Gui_Handle mel_gui_create_panel(Mel_Gui_Handle parent, u32 id, i32 x, i32 y, i32 w, i32 h, Mel_Gui_Proc proc, void* user)
{
    return mel_gui_create(&(Mel_Gui_Create_Desc){
        .class_atom = mel_gui_panel_atom(),
        .text   = STR8_EMPTY,
        .style  = MEL_GUI_WS_CHILD | MEL_GUI_WS_VISIBLE | MEL_GUI_WS_CLIPCHILDREN,
        .x = x, .y = y, .w = w, .h = h,
        .parent = parent,
        .id     = id,
        .proc   = proc,
        .user   = user,
    });
}
