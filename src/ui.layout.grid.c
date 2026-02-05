#include "ui.layout.grid.h"

static Mel_Vec2 mel__grid_preferred_size(Mel_Layout* layout, Mel_Layoutable* container)
{
    Mel_GridLayout* grid = (Mel_GridLayout*)layout;

    f32 max_cell_w = 0;
    f32 max_cell_h = 0;
    i32 child_count = 0;

    for (Mel_Layoutable* child = mel_layoutable_first_child(container);
         child != nullptr;
         child = mel_layoutable_next_sibling(child))
    {
        if (!mel_layoutable_is_visible(child)) continue;

        Mel_Vec2 child_size = mel_layoutable_resolved_size(child);
        if (child_size.x > max_cell_w) max_cell_w = child_size.x;
        if (child_size.y > max_cell_h) max_cell_h = child_size.y;
        child_count++;
    }

    if (child_count == 0) return mel_vec2(grid->margin * 2.0f, grid->margin * 2.0f);

    i32 fixed_axis_count = grid->resolution;
    i32 variable_axis_count = (child_count + fixed_axis_count - 1) / fixed_axis_count;

    f32 fixed_extent;
    f32 variable_extent;

    if (grid->orientation == MEL_ORIENTATION_VERTICAL)
    {
        fixed_extent = (f32)fixed_axis_count * max_cell_w
            + grid->spacing * (f32)(fixed_axis_count - 1)
            + grid->margin * 2.0f;
        variable_extent = (f32)variable_axis_count * max_cell_h
            + grid->spacing * (f32)(variable_axis_count - 1)
            + grid->margin * 2.0f;
        return mel_vec2(fixed_extent, variable_extent);
    }

    fixed_extent = (f32)fixed_axis_count * max_cell_h
        + grid->spacing * (f32)(fixed_axis_count - 1)
        + grid->margin * 2.0f;
    variable_extent = (f32)variable_axis_count * max_cell_w
        + grid->spacing * (f32)(variable_axis_count - 1)
        + grid->margin * 2.0f;
    return mel_vec2(variable_extent, fixed_extent);
}

static void mel__grid_perform_layout(Mel_Layout* layout, Mel_Layoutable* container)
{
    Mel_GridLayout* grid = (Mel_GridLayout*)layout;
    Mel_Vec2 container_pos = mel_layoutable_get_position(container);
    Mel_Vec2 container_size = mel_layoutable_get_size(container);

    f32 available_w = container_size.x - grid->margin * 2.0f;
    f32 available_h = container_size.y - grid->margin * 2.0f;

    i32 res = grid->resolution;

    f32 cell_w;
    f32 cell_h;

    if (grid->orientation == MEL_ORIENTATION_VERTICAL)
    {
        cell_w = (available_w - grid->spacing * (f32)(res - 1)) / (f32)res;

        i32 child_count = 0;
        for (Mel_Layoutable* child = mel_layoutable_first_child(container);
             child != nullptr;
             child = mel_layoutable_next_sibling(child))
        {
            if (mel_layoutable_is_visible(child)) child_count++;
        }

        i32 row_count = (child_count + res - 1) / res;
        if (row_count > 0)
        {
            cell_h = (available_h - grid->spacing * (f32)(row_count - 1)) / (f32)row_count;
        }
        else
        {
            cell_h = available_h;
        }
    }
    else
    {
        cell_h = (available_h - grid->spacing * (f32)(res - 1)) / (f32)res;

        i32 child_count = 0;
        for (Mel_Layoutable* child = mel_layoutable_first_child(container);
             child != nullptr;
             child = mel_layoutable_next_sibling(child))
        {
            if (mel_layoutable_is_visible(child)) child_count++;
        }

        i32 col_count = (child_count + res - 1) / res;
        if (col_count > 0)
        {
            cell_w = (available_w - grid->spacing * (f32)(col_count - 1)) / (f32)col_count;
        }
        else
        {
            cell_w = available_w;
        }
    }

    i32 index = 0;

    for (Mel_Layoutable* child = mel_layoutable_first_child(container);
         child != nullptr;
         child = mel_layoutable_next_sibling(child))
    {
        if (!mel_layoutable_is_visible(child)) continue;

        i32 col;
        i32 row;

        if (grid->orientation == MEL_ORIENTATION_VERTICAL)
        {
            col = index % res;
            row = index / res;
        }
        else
        {
            row = index % res;
            col = index / res;
        }

        f32 x = grid->margin + (f32)col * (cell_w + grid->spacing);
        f32 y = grid->margin + (f32)row * (cell_h + grid->spacing);

        Mel_Vec2 child_size = mel_layoutable_resolved_size(child);
        f32 final_w = child_size.x;
        f32 final_h = child_size.y;
        f32 offset_x = 0;
        f32 offset_y = 0;

        switch (grid->alignment)
        {
            case MEL_ALIGN_MIN:
                break;
            case MEL_ALIGN_MIDDLE:
                offset_x = (cell_w - final_w) * 0.5f;
                offset_y = (cell_h - final_h) * 0.5f;
                break;
            case MEL_ALIGN_MAX:
                offset_x = cell_w - final_w;
                offset_y = cell_h - final_h;
                break;
            case MEL_ALIGN_FILL:
                final_w = cell_w;
                final_h = cell_h;
                break;
        }

        mel_layoutable_set_position(child,
            mel_vec2(container_pos.x + x + offset_x, container_pos.y + y + offset_y));
        mel_layoutable_set_size(child, mel_vec2(final_w, final_h));

        index++;
    }
}

static const Mel_Layout_VTable s_grid_layout_vtable = {
    .preferred_size  = mel__grid_preferred_size,
    .perform_layout  = mel__grid_perform_layout,
};

void mel_grid_layout_init_opt(Mel_GridLayout* layout, Mel_GridLayout_Opt opt)
{
    assert(layout != nullptr);
    *layout = (Mel_GridLayout){0};
    layout->base.vtable = &s_grid_layout_vtable;
    layout->orientation = opt.orientation;
    layout->resolution = opt.resolution > 0 ? opt.resolution : 2;
    layout->alignment = opt.alignment;
    layout->margin = opt.margin;
    layout->spacing = opt.spacing;
}
