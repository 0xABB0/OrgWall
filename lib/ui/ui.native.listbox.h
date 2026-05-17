#pragma once

#include "ui.native.ctrl.h"
#include "str8.fwd.h"

typedef void (*Mel_NListbox_Select_Cb)(i32 index, void* user);

typedef struct {
    Mel_NCtrl base;
    str8* items;
    i32 item_count;
    i32 selected;
    Mel_NListbox_Select_Cb on_select;
    void* user_data;
} Mel_NListbox;

typedef struct {
    str8* items;
    i32 item_count;
} Mel_NListbox_Opt;

void mel_nlistbox_init_opt(Mel_NListbox* listbox, Mel_NListbox_Opt opt);
#define mel_nlistbox_init(listbox, ...) mel_nlistbox_init_opt((listbox), (Mel_NListbox_Opt){__VA_ARGS__})

void mel_nlistbox_set_items(Mel_NListbox* listbox, str8* items, i32 count);
void mel_nlistbox_set_selected(Mel_NListbox* listbox, i32 index);

extern const Mel_NCtrl_VTable* mel__nlistbox_vtable(void);
extern void mel__nlistbox_set_items_platform(Mel_NListbox* listbox, const char** items, i32 count);
extern void mel__nlistbox_set_selected_platform(Mel_NListbox* listbox, i32 index);
