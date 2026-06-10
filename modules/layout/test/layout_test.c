#include <test/test.h>

#include <layout/linear.h>
#include <layout/stack.h>

static Mel_Layout_Item item(i32 pref_w, i32 pref_h, i32 weight, u8 cross_align)
{
    Mel_Layout_Item it = { 0 };
    it.spec.preferred_w = pref_w;
    it.spec.preferred_h = pref_h;
    it.spec.weight = weight;
    it.spec.cross_align = cross_align;
    return it;
}

MEL_TEST(layout, column_stacks_with_spacing_and_margin)
{
    Mel_Linear_Layout col;
    mel_linear_layout_init(&col, true, (Mel_Linear_Layout_Opt){ .spacing = 8, .margin = 16 });

    Mel_Layout_Item items[] = {
        item(100, 30, 0, MEL_ALIGN_DEFAULT),
        item(100, 50, 0, MEL_ALIGN_DEFAULT),
    };

    col.base.cls->arrange(&col.base, items, 2, 400, 300);
    MEL_EXPECT_EQ(items[0].x, 16);
    MEL_EXPECT_EQ(items[0].y, 16);
    MEL_EXPECT_EQ(items[0].w, 100);
    MEL_EXPECT_EQ(items[0].h, 30);
    MEL_EXPECT_EQ(items[1].y, 16 + 30 + 8);
}

MEL_TEST(layout, column_weight_distributes_leftover)
{
    Mel_Linear_Layout col;
    mel_linear_layout_init(&col, true, (Mel_Linear_Layout_Opt){ 0 });

    Mel_Layout_Item items[] = {
        item(50, 40, 0, MEL_ALIGN_DEFAULT),
        item(50, 20, 1, MEL_ALIGN_DEFAULT),
    };

    col.base.cls->arrange(&col.base, items, 2, 100, 200);
    MEL_EXPECT_EQ(items[0].h, 40);
    MEL_EXPECT_EQ(items[1].h, 20 + (200 - 60));
}

MEL_TEST(layout, column_cross_alignment)
{
    Mel_Linear_Layout col;
    mel_linear_layout_init(&col, true, (Mel_Linear_Layout_Opt){ .cross_align = MEL_ALIGN_STRETCH });

    Mel_Layout_Item items[] = {
        item(50, 20, 0, MEL_ALIGN_DEFAULT),
        item(50, 20, 0, MEL_ALIGN_CENTER),
        item(50, 20, 0, MEL_ALIGN_END),
    };

    col.base.cls->arrange(&col.base, items, 3, 200, 300);
    MEL_EXPECT_EQ(items[0].w, 200);
    MEL_EXPECT_EQ(items[1].x, (200 - 50) / 2);
    MEL_EXPECT_EQ(items[2].x, 200 - 50);
}

MEL_TEST(layout, row_mirrors_column)
{
    Mel_Linear_Layout row;
    mel_linear_layout_init(&row, false, (Mel_Linear_Layout_Opt){ .spacing = 4 });

    Mel_Layout_Item items[] = {
        item(30, 100, 0, MEL_ALIGN_DEFAULT),
        item(50, 100, 0, MEL_ALIGN_DEFAULT),
    };

    row.base.cls->arrange(&row.base, items, 2, 300, 120);
    MEL_EXPECT_EQ(items[0].x, 0);
    MEL_EXPECT_EQ(items[0].w, 30);
    MEL_EXPECT_EQ(items[1].x, 30 + 4);
    MEL_EXPECT_EQ(items[1].w, 50);
}

MEL_TEST(layout, linear_measure)
{
    Mel_Linear_Layout col;
    mel_linear_layout_init(&col, true, (Mel_Linear_Layout_Opt){ .spacing = 10, .margin = 5 });

    Mel_Layout_Item items[] = {
        item(80, 30, 0, MEL_ALIGN_DEFAULT),
        item(120, 40, 0, MEL_ALIGN_DEFAULT),
    };

    i32 w = 0, h = 0;
    col.base.cls->measure(&col.base, items, 2, 0, 0, &w, &h);
    MEL_EXPECT_EQ(w, 120 + 10);
    MEL_EXPECT_EQ(h, 30 + 40 + 10 + 10);
}

MEL_TEST(layout, fixed_beats_preferred_beats_natural)
{
    Mel_Layout_Item it = { 0 };
    it.natural_w = 10;
    it.natural_h = 11;

    i32 w, h;
    mel_layout_item_preferred(&it, &w, &h);
    MEL_EXPECT_EQ(w, 10);
    MEL_EXPECT_EQ(h, 11);

    it.spec.preferred_w = 20;
    it.spec.preferred_h = 21;
    mel_layout_item_preferred(&it, &w, &h);
    MEL_EXPECT_EQ(w, 20);
    MEL_EXPECT_EQ(h, 21);

    it.spec.fixed_w = 30;
    it.spec.fixed_h = 31;
    mel_layout_item_preferred(&it, &w, &h);
    MEL_EXPECT_EQ(w, 30);
    MEL_EXPECT_EQ(h, 31);
}

MEL_TEST(layout, stack_overlays_children)
{
    Mel_Stack_Layout st;
    mel_stack_layout_init(&st, (Mel_Stack_Layout_Opt){ .margin = 10 });

    Mel_Layout_Item items[] = {
        item(50, 50, 0, MEL_ALIGN_DEFAULT),
        item(60, 60, 0, MEL_ALIGN_DEFAULT),
    };
    items[1].spec.margin_l = 5;

    st.base.cls->arrange(&st.base, items, 2, 200, 100);
    MEL_EXPECT_EQ(items[0].x, 10);
    MEL_EXPECT_EQ(items[0].w, 180);
    MEL_EXPECT_EQ(items[0].h, 80);
    MEL_EXPECT_EQ(items[1].x, 15);
    MEL_EXPECT_EQ(items[1].w, 175);
}
