#include "ui.layout.advgrid.h"
#include "allocator.h"

static Mel_GridAnchor* mel__advgrid_find_anchor(Mel_AdvGridLayout* layout, Mel_Layoutable* item)
{
    for (i32 i = 0; i < layout->anchor_count; i++)
    {
        if (layout->anchor_keys[i] == item)
        {
            return &layout->anchor_values[i];
        }
    }
    return nullptr;
}

static f32 mel__max_f32(f32 a, f32 b)
{
    return a > b ? a : b;
}

static Mel_Vec2 mel__advgrid_preferred_size(Mel_Layout* layout, Mel_Layoutable* container)
{
    Mel_AdvGridLayout* grid = (Mel_AdvGridLayout*)layout;

    f32 col_mins[64] = {0};
    f32 row_mins[64] = {0};

    i32 cols = grid->cols < 64 ? grid->cols : 64;
    i32 rows = grid->rows < 64 ? grid->rows : 64;

    for (Mel_Layoutable* child = mel_layoutable_first_child(container);
         child != nullptr;
         child = mel_layoutable_next_sibling(child))
    {
        if (!mel_layoutable_is_visible(child)) continue;

        Mel_GridAnchor* anchor = mel__advgrid_find_anchor(grid, child);
        if (anchor == nullptr) continue;

        Mel_Vec2 child_size = mel_layoutable_resolved_size(child);

        i32 cspan = anchor->col_span > 0 ? anchor->col_span : 1;
        i32 rspan = anchor->row_span > 0 ? anchor->row_span : 1;

        if (cspan == 1 && anchor->col < cols)
        {
            col_mins[anchor->col] = mel__max_f32(col_mins[anchor->col], child_size.x);
        }
        else if (cspan > 0 && anchor->col < cols)
        {
            f32 per_col = child_size.x / (f32)cspan;
            for (i32 c = anchor->col; c < anchor->col + cspan && c < cols; c++)
            {
                col_mins[c] = mel__max_f32(col_mins[c], per_col);
            }
        }

        if (rspan == 1 && anchor->row < rows)
        {
            row_mins[anchor->row] = mel__max_f32(row_mins[anchor->row], child_size.y);
        }
        else if (rspan > 0 && anchor->row < rows)
        {
            f32 per_row = child_size.y / (f32)rspan;
            for (i32 r = anchor->row; r < anchor->row + rspan && r < rows; r++)
            {
                row_mins[r] = mel__max_f32(row_mins[r], per_row);
            }
        }
    }

    f32 total_w = 0;
    for (i32 c = 0; c < cols; c++) total_w += col_mins[c];
    if (cols > 1) total_w += grid->spacing * (f32)(cols - 1);
    total_w += grid->margin * 2.0f;

    f32 total_h = 0;
    for (i32 r = 0; r < rows; r++) total_h += row_mins[r];
    if (rows > 1) total_h += grid->spacing * (f32)(rows - 1);
    total_h += grid->margin * 2.0f;

    return mel_vec2(total_w, total_h);
}

static void mel__advgrid_perform_layout(Mel_Layout* layout, Mel_Layoutable* container)
{
    Mel_AdvGridLayout* grid = (Mel_AdvGridLayout*)layout;
    Mel_Vec2 container_size = mel_layoutable_get_size(container);

    f32 available_w = container_size.x - grid->margin * 2.0f;
    f32 available_h = container_size.y - grid->margin * 2.0f;

    i32 cols = grid->cols < 64 ? grid->cols : 64;
    i32 rows = grid->rows < 64 ? grid->rows : 64;

    f32 col_sizes[64] = {0};
    f32 row_sizes[64] = {0};

    for (Mel_Layoutable* child = mel_layoutable_first_child(container);
         child != nullptr;
         child = mel_layoutable_next_sibling(child))
    {
        if (!mel_layoutable_is_visible(child)) continue;

        Mel_GridAnchor* anchor = mel__advgrid_find_anchor(grid, child);
        if (anchor == nullptr) continue;

        Mel_Vec2 child_size = mel_layoutable_resolved_size(child);

        i32 cspan = anchor->col_span > 0 ? anchor->col_span : 1;
        i32 rspan = anchor->row_span > 0 ? anchor->row_span : 1;

        if (cspan == 1 && anchor->col < cols)
        {
            col_sizes[anchor->col] = mel__max_f32(col_sizes[anchor->col], child_size.x);
        }
        else if (cspan > 0 && anchor->col < cols)
        {
            f32 per_col = child_size.x / (f32)cspan;
            for (i32 c = anchor->col; c < anchor->col + cspan && c < cols; c++)
            {
                col_sizes[c] = mel__max_f32(col_sizes[c], per_col);
            }
        }

        if (rspan == 1 && anchor->row < rows)
        {
            row_sizes[anchor->row] = mel__max_f32(row_sizes[anchor->row], child_size.y);
        }
        else if (rspan > 0 && anchor->row < rows)
        {
            f32 per_row = child_size.y / (f32)rspan;
            for (i32 r = anchor->row; r < anchor->row + rspan && r < rows; r++)
            {
                row_sizes[r] = mel__max_f32(row_sizes[r], per_row);
            }
        }
    }

    f32 total_col_stretch = 0;
    f32 total_row_stretch = 0;

    for (i32 c = 0; c < cols; c++)
    {
        if (grid->col_stretch != nullptr) total_col_stretch += grid->col_stretch[c];
    }
    for (i32 r = 0; r < rows; r++)
    {
        if (grid->row_stretch != nullptr) total_row_stretch += grid->row_stretch[r];
    }

    f32 min_col_total = 0;
    for (i32 c = 0; c < cols; c++) min_col_total += col_sizes[c];
    if (cols > 1) min_col_total += grid->spacing * (f32)(cols - 1);

    f32 min_row_total = 0;
    for (i32 r = 0; r < rows; r++) min_row_total += row_sizes[r];
    if (rows > 1) min_row_total += grid->spacing * (f32)(rows - 1);

    f32 extra_w = available_w - min_col_total;
    if (extra_w < 0) extra_w = 0;

    f32 extra_h = available_h - min_row_total;
    if (extra_h < 0) extra_h = 0;

    if (total_col_stretch > 0 && extra_w > 0)
    {
        for (i32 c = 0; c < cols; c++)
        {
            f32 stretch = grid->col_stretch != nullptr ? grid->col_stretch[c] : 0;
            if (stretch > 0)
            {
                col_sizes[c] += extra_w * (stretch / total_col_stretch);
            }
        }
    }

    if (total_row_stretch > 0 && extra_h > 0)
    {
        for (i32 r = 0; r < rows; r++)
        {
            f32 stretch = grid->row_stretch != nullptr ? grid->row_stretch[r] : 0;
            if (stretch > 0)
            {
                row_sizes[r] += extra_h * (stretch / total_row_stretch);
            }
        }
    }

    f32 col_positions[64];
    col_positions[0] = 0;
    for (i32 c = 1; c < cols; c++)
    {
        col_positions[c] = col_positions[c - 1] + col_sizes[c - 1] + grid->spacing;
    }

    f32 row_positions[64];
    row_positions[0] = 0;
    for (i32 r = 1; r < rows; r++)
    {
        row_positions[r] = row_positions[r - 1] + row_sizes[r - 1] + grid->spacing;
    }

    for (Mel_Layoutable* child = mel_layoutable_first_child(container);
         child != nullptr;
         child = mel_layoutable_next_sibling(child))
    {
        if (!mel_layoutable_is_visible(child)) continue;

        Mel_GridAnchor* anchor = mel__advgrid_find_anchor(grid, child);
        if (anchor == nullptr) continue;

        i32 cspan = anchor->col_span > 0 ? anchor->col_span : 1;
        i32 rspan = anchor->row_span > 0 ? anchor->row_span : 1;

        f32 cell_x = col_positions[anchor->col];
        f32 cell_y = row_positions[anchor->row];

        f32 cell_w = 0;
        for (i32 c = anchor->col; c < anchor->col + cspan && c < cols; c++)
        {
            cell_w += col_sizes[c];
            if (c > anchor->col) cell_w += grid->spacing;
        }

        f32 cell_h = 0;
        for (i32 r = anchor->row; r < anchor->row + rspan && r < rows; r++)
        {
            cell_h += row_sizes[r];
            if (r > anchor->row) cell_h += grid->spacing;
        }

        Mel_Vec2 child_size = mel_layoutable_resolved_size(child);

        f32 final_x = cell_x;
        f32 final_y = cell_y;
        f32 final_w = child_size.x;
        f32 final_h = child_size.y;

        switch (anchor->align_x)
        {
            case MEL_ALIGN_MIN:
                break;
            case MEL_ALIGN_MIDDLE:
                final_x += (cell_w - final_w) * 0.5f;
                break;
            case MEL_ALIGN_MAX:
                final_x += cell_w - final_w;
                break;
            case MEL_ALIGN_FILL:
                final_w = cell_w;
                break;
        }

        switch (anchor->align_y)
        {
            case MEL_ALIGN_MIN:
                break;
            case MEL_ALIGN_MIDDLE:
                final_y += (cell_h - final_h) * 0.5f;
                break;
            case MEL_ALIGN_MAX:
                final_y += cell_h - final_h;
                break;
            case MEL_ALIGN_FILL:
                final_h = cell_h;
                break;
        }

        mel_layoutable_set_position(child,
            mel_vec2(grid->margin + final_x,
                     grid->margin + final_y));
        mel_layoutable_set_size(child, mel_vec2(final_w, final_h));
    }
}

static const Mel_Layout_VTable s_advgrid_layout_vtable = {
    .preferred_size  = mel__advgrid_preferred_size,
    .perform_layout  = mel__advgrid_perform_layout,
};

void mel_advgrid_layout_init_opt(Mel_AdvGridLayout* layout, Mel_AdvGridLayout_Opt opt)
{
    assert(layout != nullptr);
    assert(opt.allocator != nullptr);
    assert(opt.cols > 0);
    assert(opt.rows > 0);

    *layout = (Mel_AdvGridLayout){0};
    layout->base.vtable = &s_advgrid_layout_vtable;
    layout->allocator = opt.allocator;
    layout->cols = opt.cols;
    layout->rows = opt.rows;
    layout->col_stretch = opt.col_stretch;
    layout->row_stretch = opt.row_stretch;
    layout->margin = opt.margin;
    layout->spacing = opt.spacing;
    layout->anchor_count = 0;
    layout->anchor_capacity = 0;
    layout->anchor_keys = nullptr;
    layout->anchor_values = nullptr;
}

void mel_advgrid_layout_set_anchor(Mel_AdvGridLayout* layout, Mel_Layoutable* item, Mel_GridAnchor anchor)
{
    assert(layout != nullptr);
    assert(item != nullptr);
    assert(layout->allocator != nullptr);

    for (i32 i = 0; i < layout->anchor_count; i++)
    {
        if (layout->anchor_keys[i] == item)
        {
            layout->anchor_values[i] = anchor;
            return;
        }
    }

    if (layout->anchor_count >= layout->anchor_capacity)
    {
        i32 new_cap = layout->anchor_capacity == 0 ? 8 : layout->anchor_capacity * 2;

        Mel_Layoutable** new_keys;
        Mel_GridAnchor* new_values;

        if (layout->anchor_keys != nullptr)
        {
            new_keys = (Mel_Layoutable**)mel_realloc(layout->allocator, layout->anchor_keys,
                (usize)new_cap * sizeof(Mel_Layoutable*));
            new_values = (Mel_GridAnchor*)mel_realloc(layout->allocator, layout->anchor_values,
                (usize)new_cap * sizeof(Mel_GridAnchor));
        }
        else
        {
            new_keys = (Mel_Layoutable**)mel_alloc(layout->allocator,
                (usize)new_cap * sizeof(Mel_Layoutable*));
            new_values = (Mel_GridAnchor*)mel_alloc(layout->allocator,
                (usize)new_cap * sizeof(Mel_GridAnchor));
        }

        layout->anchor_keys = new_keys;
        layout->anchor_values = new_values;
        layout->anchor_capacity = new_cap;
    }

    layout->anchor_keys[layout->anchor_count] = item;
    layout->anchor_values[layout->anchor_count] = anchor;
    layout->anchor_count++;
}

void mel_advgrid_layout_destroy(Mel_AdvGridLayout* layout)
{
    assert(layout != nullptr);

    if (layout->anchor_keys != nullptr)
    {
        mel_dealloc(layout->allocator, layout->anchor_keys);
        layout->anchor_keys = nullptr;
    }
    if (layout->anchor_values != nullptr)
    {
        mel_dealloc(layout->allocator, layout->anchor_values);
        layout->anchor_values = nullptr;
    }
    layout->anchor_count = 0;
    layout->anchor_capacity = 0;
}
