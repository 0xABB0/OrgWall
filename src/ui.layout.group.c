#include "ui.layout.group.h"

static Mel_Vec2 mel__group_preferred_size(Mel_Layout* layout, Mel_Layoutable* container)
{
    Mel_GroupLayout* group = (Mel_GroupLayout*)layout;
    f32 width = 0;
    f32 height = 0;
    i32 count = 0;
    bool prev_was_header = false;

    for (Mel_Layoutable* child = mel_layoutable_first_child(container);
         child != nullptr;
         child = mel_layoutable_next_sibling(child))
    {
        if (!mel_layoutable_is_visible(child)) continue;

        Mel_Vec2 child_size = mel_layoutable_resolved_size(child);
        u32 flags = mel_layoutable_get_flags(child);
        bool is_header = (flags & MEL_LAYOUTABLE_FLAG_GROUP_HEADER) != 0;

        if (count > 0)
        {
            if (is_header)
            {
                height += group->group_spacing;
            }
            else
            {
                height += group->spacing;
            }
        }

        f32 child_w = child_size.x;
        if (!is_header)
        {
            child_w += group->group_indent;
        }

        if (child_w > width) width = child_w;

        height += child_size.y;
        prev_was_header = is_header;
        count++;
    }

    MEL_UNUSED(prev_was_header);

    width += group->margin * 2.0f;
    height += group->margin * 2.0f;

    return mel_vec2(width, height);
}

static void mel__group_perform_layout(Mel_Layout* layout, Mel_Layoutable* container)
{
    Mel_GroupLayout* group = (Mel_GroupLayout*)layout;
    Mel_Vec2 container_pos = mel_layoutable_get_position(container);
    Mel_Vec2 container_size = mel_layoutable_get_size(container);

    f32 available_width = container_size.x - group->margin * 2.0f;
    f32 cursor_y = group->margin;
    i32 count = 0;

    for (Mel_Layoutable* child = mel_layoutable_first_child(container);
         child != nullptr;
         child = mel_layoutable_next_sibling(child))
    {
        if (!mel_layoutable_is_visible(child)) continue;

        Mel_Vec2 child_size = mel_layoutable_resolved_size(child);
        u32 flags = mel_layoutable_get_flags(child);
        bool is_header = (flags & MEL_LAYOUTABLE_FLAG_GROUP_HEADER) != 0;

        if (count > 0)
        {
            if (is_header)
            {
                cursor_y += group->group_spacing;
            }
            else
            {
                cursor_y += group->spacing;
            }
        }

        f32 x_offset;
        f32 child_width;

        if (is_header)
        {
            x_offset = group->margin;
            child_width = available_width;
        }
        else
        {
            x_offset = group->margin + group->group_indent;
            child_width = available_width - group->group_indent;
        }

        mel_layoutable_set_position(child,
            mel_vec2(container_pos.x + x_offset, container_pos.y + cursor_y));
        mel_layoutable_set_size(child,
            mel_vec2(child_width, child_size.y));

        cursor_y += child_size.y;
        count++;
    }
}

static const Mel_Layout_VTable s_group_layout_vtable = {
    .preferred_size  = mel__group_preferred_size,
    .perform_layout  = mel__group_perform_layout,
};

void mel_group_layout_init_opt(Mel_GroupLayout* layout, Mel_GroupLayout_Opt opt)
{
    assert(layout != nullptr);
    *layout = (Mel_GroupLayout){0};
    layout->base.vtable = &s_group_layout_vtable;
    layout->margin = opt.margin > 0 ? opt.margin : 15.0f;
    layout->spacing = opt.spacing > 0 ? opt.spacing : 6.0f;
    layout->group_spacing = opt.group_spacing > 0 ? opt.group_spacing : 14.0f;
    layout->group_indent = opt.group_indent > 0 ? opt.group_indent : 20.0f;
}
