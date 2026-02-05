#include "ui.layout.box.h"

static Mel_Vec2 mel__box_preferred_size(Mel_Layout* layout, Mel_Layoutable* container)
{
    Mel_BoxLayout* box = (Mel_BoxLayout*)layout;
    f32 primary = 0;
    f32 cross = 0;
    i32 count = 0;

    for (Mel_Layoutable* child = mel_layoutable_first_child(container);
         child != nullptr;
         child = mel_layoutable_next_sibling(child))
    {
        if (!mel_layoutable_is_visible(child)) continue;

        Mel_Vec2 child_size = mel_layoutable_resolved_size(child);

        if (box->orientation == MEL_ORIENTATION_HORIZONTAL)
        {
            primary += child_size.x;
            if (child_size.y > cross) cross = child_size.y;
        }
        else
        {
            primary += child_size.y;
            if (child_size.x > cross) cross = child_size.x;
        }
        count++;
    }

    if (count > 1)
    {
        primary += box->spacing * (f32)(count - 1);
    }

    primary += box->margin * 2.0f;
    cross += box->margin * 2.0f;

    if (box->orientation == MEL_ORIENTATION_HORIZONTAL)
    {
        return mel_vec2(primary, cross);
    }
    return mel_vec2(cross, primary);
}

static void mel__box_perform_layout(Mel_Layout* layout, Mel_Layoutable* container)
{
    Mel_BoxLayout* box = (Mel_BoxLayout*)layout;
    Mel_Vec2 container_pos = mel_layoutable_get_position(container);
    Mel_Vec2 container_size = mel_layoutable_get_size(container);

    f32 cursor = box->margin;

    for (Mel_Layoutable* child = mel_layoutable_first_child(container);
         child != nullptr;
         child = mel_layoutable_next_sibling(child))
    {
        if (!mel_layoutable_is_visible(child)) continue;

        Mel_Vec2 child_size = mel_layoutable_resolved_size(child);

        f32 pos_primary = cursor;
        f32 pos_cross;
        f32 size_primary;
        f32 size_cross;
        f32 container_cross;

        if (box->orientation == MEL_ORIENTATION_HORIZONTAL)
        {
            container_cross = container_size.y;
            size_primary = child_size.x;
        }
        else
        {
            container_cross = container_size.x;
            size_primary = child_size.y;
        }

        f32 cross_dim = (box->orientation == MEL_ORIENTATION_HORIZONTAL) ? child_size.y : child_size.x;
        f32 available_cross = container_cross - box->margin * 2.0f;

        switch (box->alignment)
        {
            case MEL_ALIGN_MIN:
                pos_cross = box->margin;
                size_cross = cross_dim;
                break;
            case MEL_ALIGN_MIDDLE:
                pos_cross = box->margin + (available_cross - cross_dim) * 0.5f;
                size_cross = cross_dim;
                break;
            case MEL_ALIGN_MAX:
                pos_cross = box->margin + available_cross - cross_dim;
                size_cross = cross_dim;
                break;
            case MEL_ALIGN_FILL:
                pos_cross = box->margin;
                size_cross = available_cross;
                break;
            default:
                pos_cross = box->margin;
                size_cross = cross_dim;
                break;
        }

        Mel_Vec2 final_pos;
        Mel_Vec2 final_size;

        if (box->orientation == MEL_ORIENTATION_HORIZONTAL)
        {
            final_pos = mel_vec2(container_pos.x + pos_primary, container_pos.y + pos_cross);
            final_size = mel_vec2(size_primary, size_cross);
        }
        else
        {
            final_pos = mel_vec2(container_pos.x + pos_cross, container_pos.y + pos_primary);
            final_size = mel_vec2(size_cross, size_primary);
        }

        mel_layoutable_set_position(child, final_pos);
        mel_layoutable_set_size(child, final_size);

        cursor += size_primary + box->spacing;
    }
}

static const Mel_Layout_VTable s_box_layout_vtable = {
    .preferred_size  = mel__box_preferred_size,
    .perform_layout  = mel__box_perform_layout,
};

void mel_box_layout_init_opt(Mel_BoxLayout* layout, Mel_BoxLayout_Opt opt)
{
    assert(layout != nullptr);
    *layout = (Mel_BoxLayout){0};
    layout->base.vtable = &s_box_layout_vtable;
    layout->orientation = opt.orientation;
    layout->alignment = opt.alignment;
    layout->margin = opt.margin;
    layout->spacing = opt.spacing;
}
