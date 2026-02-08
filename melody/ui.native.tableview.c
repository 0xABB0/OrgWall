#include "ui.native.tableview.h"

void mel_ntableview_init_opt(Mel_NTableView* table, Mel_NTableView_Opt opt)
{
    assert(table != nullptr);

    mel_nctrl_init(&table->base, mel__ntableview_vtable());

    table->columns      = opt.columns;
    table->column_count = opt.column_count;
    table->row_count    = opt.row_count;
    table->selected     = -1;
    table->data_cb      = nullptr;
    table->on_select    = nullptr;
    table->user_data    = nullptr;

    mel_nctrl_create_backing(&table->base);
}

void mel_ntableview_set_row_count(Mel_NTableView* table, i32 count)
{
    assert(table != nullptr);
    table->row_count = count;
}

void mel_ntableview_reload(Mel_NTableView* table)
{
    assert(table != nullptr);
    if (table->base.backing)
        mel__ntableview_reload_platform(table);
}

void mel_ntableview_set_selected(Mel_NTableView* table, i32 row)
{
    assert(table != nullptr);
    table->selected = row;
    if (table->base.backing)
        mel__ntableview_set_selected_platform(table, row);
}
