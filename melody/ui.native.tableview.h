#pragma once

#include "ui.native.ctrl.h"

typedef const char* (*Mel_NTableView_Data_Cb)(i32 row, i32 col, void* user);
typedef void (*Mel_NTableView_Select_Cb)(i32 row, void* user);

typedef struct {
    const char* title;
    f32 width;
} Mel_NTableColumn;

typedef struct {
    Mel_NCtrl base;
    Mel_NTableColumn* columns;
    i32 column_count;
    i32 row_count;
    i32 selected;
    Mel_NTableView_Data_Cb data_cb;
    Mel_NTableView_Select_Cb on_select;
    void* user_data;
} Mel_NTableView;

typedef struct {
    Mel_NTableColumn* columns;
    i32 column_count;
    i32 row_count;
} Mel_NTableView_Opt;

void mel_ntableview_init_opt(Mel_NTableView* table, Mel_NTableView_Opt opt);
#define mel_ntableview_init(table, ...) mel_ntableview_init_opt((table), (Mel_NTableView_Opt){__VA_ARGS__})

void mel_ntableview_set_row_count(Mel_NTableView* table, i32 count);
void mel_ntableview_reload(Mel_NTableView* table);
void mel_ntableview_set_selected(Mel_NTableView* table, i32 row);

extern const Mel_NCtrl_VTable* mel__ntableview_vtable(void);
extern void mel__ntableview_reload_platform(Mel_NTableView* table);
extern void mel__ntableview_set_selected_platform(Mel_NTableView* table, i32 row);
