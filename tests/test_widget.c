#include "../melody/test.h"
#include "../melody/ui.widget.h"
#include "../melody/ui.layout.box.h"
#include <string.h>

MEL_TEST(init_defaults)
{
    Mel_Widget w;
    mel_widget_init(&w);

    MEL_ASSERT_NULL(w.parent);
    MEL_ASSERT_NULL(w.first_child);
    MEL_ASSERT_NULL(w.next_sibling);
    MEL_ASSERT_NULL(w.layout);
    MEL_ASSERT(w.visible);
    MEL_ASSERT(w.enabled);
    MEL_ASSERT_FLOAT_EQ(w.pos.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(w.pos.y, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(w.size.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(w.size.y, 0.0f, 0.001f);
    MEL_ASSERT_EQ(w.flags, (u32)0);
    MEL_ASSERT_EQ(w.state, (u32)0);
    MEL_ASSERT_EQ(w.type_tag, 0);
    MEL_ASSERT_NULL(w.draw);
    MEL_ASSERT_NULL(w.measure);
    MEL_ASSERT_NULL(w.on_destroy);
    MEL_ASSERT_NULL(w.on_mouse_down);
    MEL_ASSERT_NULL(w.on_mouse_up);
    MEL_ASSERT_NULL(w.on_mouse_move);
    MEL_PASS();
}

MEL_TEST(init_zeroes_struct)
{
    Mel_Widget w;
    memset(&w, 0xFF, sizeof(w));
    mel_widget_init(&w);

    MEL_ASSERT_NULL(w.parent);
    MEL_ASSERT_NULL(w.first_child);
    MEL_ASSERT_NULL(w.next_sibling);
    MEL_ASSERT_NULL(w.layout);
    MEL_ASSERT_EQ(w.flags, (u32)0);
    MEL_ASSERT_EQ(w.state, (u32)0);
    MEL_ASSERT_FLOAT_EQ(w.pos.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(w.pos.y, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(w.size.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(w.size.y, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(w.fixed_size.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(w.fixed_size.y, 0.0f, 0.001f);
    MEL_ASSERT_NULL(w.draw);
    MEL_ASSERT_NULL(w.measure);
    MEL_ASSERT_NULL(w.on_destroy);
    MEL_PASS();
}

MEL_TEST(destroy_empty)
{
    Mel_Widget w;
    mel_widget_init(&w);
    mel_widget_destroy(&w);

    MEL_ASSERT_NULL(w.first_child);
    MEL_PASS();
}

MEL_TEST(destroy_recursive)
{
    Mel_Widget parent, c1, c2;
    mel_widget_init(&parent);
    mel_widget_init(&c1);
    mel_widget_init(&c2);

    mel_widget_add_child(&parent, &c1);
    mel_widget_add_child(&parent, &c2);

    mel_widget_destroy(&parent);

    MEL_ASSERT_NULL(parent.first_child);
    MEL_PASS();
}

MEL_TEST(destroy_deep_hierarchy)
{
    Mel_Widget root, l1a, l1b, l2a;
    mel_widget_init(&root);
    mel_widget_init(&l1a);
    mel_widget_init(&l1b);
    mel_widget_init(&l2a);

    mel_widget_add_child(&root, &l1a);
    mel_widget_add_child(&root, &l1b);
    mel_widget_add_child(&l1a, &l2a);

    mel_widget_destroy(&root);

    MEL_ASSERT_NULL(root.first_child);
    MEL_PASS();
}

MEL_TEST(add_single_child)
{
    Mel_Widget parent, child;
    mel_widget_init(&parent);
    mel_widget_init(&child);

    mel_widget_add_child(&parent, &child);

    MEL_ASSERT_EQ(parent.first_child, &child);
    MEL_ASSERT_EQ(child.parent, &parent);
    MEL_ASSERT_NULL(child.next_sibling);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(add_multiple_children_preserves_order)
{
    Mel_Widget parent, c1, c2, c3;
    mel_widget_init(&parent);
    mel_widget_init(&c1);
    mel_widget_init(&c2);
    mel_widget_init(&c3);

    mel_widget_add_child(&parent, &c1);
    mel_widget_add_child(&parent, &c2);
    mel_widget_add_child(&parent, &c3);

    MEL_ASSERT_EQ(parent.first_child, &c1);
    MEL_ASSERT_EQ(c1.next_sibling, &c2);
    MEL_ASSERT_EQ(c2.next_sibling, &c3);
    MEL_ASSERT_NULL(c3.next_sibling);

    MEL_ASSERT_EQ(c1.parent, &parent);
    MEL_ASSERT_EQ(c2.parent, &parent);
    MEL_ASSERT_EQ(c3.parent, &parent);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(remove_first_child)
{
    Mel_Widget parent, c1, c2;
    mel_widget_init(&parent);
    mel_widget_init(&c1);
    mel_widget_init(&c2);

    mel_widget_add_child(&parent, &c1);
    mel_widget_add_child(&parent, &c2);
    mel_widget_remove_child(&parent, &c1);

    MEL_ASSERT_EQ(parent.first_child, &c2);
    MEL_ASSERT_NULL(c1.parent);
    MEL_ASSERT_NULL(c1.next_sibling);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(remove_middle_child)
{
    Mel_Widget parent, c1, c2, c3;
    mel_widget_init(&parent);
    mel_widget_init(&c1);
    mel_widget_init(&c2);
    mel_widget_init(&c3);

    mel_widget_add_child(&parent, &c1);
    mel_widget_add_child(&parent, &c2);
    mel_widget_add_child(&parent, &c3);
    mel_widget_remove_child(&parent, &c2);

    MEL_ASSERT_EQ(parent.first_child, &c1);
    MEL_ASSERT_EQ(c1.next_sibling, &c3);
    MEL_ASSERT_NULL(c3.next_sibling);
    MEL_ASSERT_NULL(c2.parent);
    MEL_ASSERT_NULL(c2.next_sibling);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(remove_last_child)
{
    Mel_Widget parent, c1, c2;
    mel_widget_init(&parent);
    mel_widget_init(&c1);
    mel_widget_init(&c2);

    mel_widget_add_child(&parent, &c1);
    mel_widget_add_child(&parent, &c2);
    mel_widget_remove_child(&parent, &c2);

    MEL_ASSERT_EQ(parent.first_child, &c1);
    MEL_ASSERT_NULL(c1.next_sibling);
    MEL_ASSERT_NULL(c2.parent);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(remove_only_child)
{
    Mel_Widget parent, child;
    mel_widget_init(&parent);
    mel_widget_init(&child);

    mel_widget_add_child(&parent, &child);
    mel_widget_remove_child(&parent, &child);

    MEL_ASSERT_NULL(parent.first_child);
    MEL_ASSERT_NULL(child.parent);
    MEL_ASSERT_NULL(child.next_sibling);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(remove_and_readd)
{
    Mel_Widget parent, child;
    mel_widget_init(&parent);
    mel_widget_init(&child);

    mel_widget_add_child(&parent, &child);
    MEL_ASSERT_EQ(parent.first_child, &child);

    mel_widget_remove_child(&parent, &child);
    MEL_ASSERT_NULL(parent.first_child);
    MEL_ASSERT_NULL(child.parent);

    mel_widget_add_child(&parent, &child);
    MEL_ASSERT_EQ(parent.first_child, &child);
    MEL_ASSERT_EQ(child.parent, &parent);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(deep_hierarchy)
{
    Mel_Widget root, level1, level2, level3;
    mel_widget_init(&root);
    mel_widget_init(&level1);
    mel_widget_init(&level2);
    mel_widget_init(&level3);

    mel_widget_add_child(&root, &level1);
    mel_widget_add_child(&level1, &level2);
    mel_widget_add_child(&level2, &level3);

    MEL_ASSERT_EQ(root.first_child, &level1);
    MEL_ASSERT_EQ(level1.parent, &root);
    MEL_ASSERT_EQ(level1.first_child, &level2);
    MEL_ASSERT_EQ(level2.parent, &level1);
    MEL_ASSERT_EQ(level2.first_child, &level3);
    MEL_ASSERT_EQ(level3.parent, &level2);
    MEL_ASSERT_NULL(level3.first_child);

    mel_widget_destroy(&root);
    MEL_PASS();
}

MEL_TEST(set_visible)
{
    Mel_Widget w;
    mel_widget_init(&w);

    mel_widget_set_visible(&w, false);
    MEL_ASSERT(!w.visible);

    mel_widget_set_visible(&w, true);
    MEL_ASSERT(w.visible);
    MEL_PASS();
}

MEL_TEST(set_enabled)
{
    Mel_Widget w;
    mel_widget_init(&w);

    mel_widget_set_enabled(&w, false);
    MEL_ASSERT(!w.enabled);
    MEL_ASSERT(w.state & MEL_WIDGET_STATE_DISABLED);

    mel_widget_set_enabled(&w, true);
    MEL_ASSERT(w.enabled);
    MEL_ASSERT(!(w.state & MEL_WIDGET_STATE_DISABLED));
    MEL_PASS();
}

MEL_TEST(set_position)
{
    Mel_Widget w;
    mel_widget_init(&w);

    mel_widget_set_position(&w, mel_vec2(10.0f, 20.0f));
    MEL_ASSERT_FLOAT_EQ(w.pos.x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(w.pos.y, 20.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(set_size)
{
    Mel_Widget w;
    mel_widget_init(&w);

    mel_widget_set_size(&w, mel_vec2(200.0f, 150.0f));
    MEL_ASSERT_FLOAT_EQ(w.size.x, 200.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(w.size.y, 150.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(preferred_size_fallback)
{
    Mel_Widget w;
    mel_widget_init(&w);
    w.size = mel_vec2(99.0f, 55.0f);

    Mel_Vec2 pref = mel_layoutable_preferred_size(&w.layoutable);
    MEL_ASSERT_FLOAT_EQ(pref.x, 99.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(pref.y, 55.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(position_roundtrip)
{
    Mel_Widget w;
    mel_widget_init(&w);

    mel_layoutable_set_position(&w.layoutable, mel_vec2(10.0f, 20.0f));
    Mel_Vec2 pos = mel_layoutable_get_position(&w.layoutable);
    MEL_ASSERT_FLOAT_EQ(pos.x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(pos.y, 20.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(size_roundtrip)
{
    Mel_Widget w;
    mel_widget_init(&w);

    mel_layoutable_set_size(&w.layoutable, mel_vec2(300.0f, 400.0f));
    Mel_Vec2 sz = mel_layoutable_get_size(&w.layoutable);
    MEL_ASSERT_FLOAT_EQ(sz.x, 300.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(sz.y, 400.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(fixed_size)
{
    Mel_Widget w;
    mel_widget_init(&w);
    w.fixed_size = mel_vec2(64.0f, 32.0f);

    Mel_Vec2 fs = mel_layoutable_get_fixed_size(&w.layoutable);
    MEL_ASSERT_FLOAT_EQ(fs.x, 64.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(fs.y, 32.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(visibility)
{
    Mel_Widget w;
    mel_widget_init(&w);

    MEL_ASSERT(mel_layoutable_is_visible(&w.layoutable));

    w.visible = false;
    MEL_ASSERT(!mel_layoutable_is_visible(&w.layoutable));
    MEL_PASS();
}

MEL_TEST(flags)
{
    Mel_Widget w;
    mel_widget_init(&w);

    MEL_ASSERT_EQ(mel_layoutable_get_flags(&w.layoutable), (u32)0);

    w.flags = MEL_LAYOUTABLE_FLAG_GROUP_HEADER;
    MEL_ASSERT_EQ(mel_layoutable_get_flags(&w.layoutable), (u32)MEL_LAYOUTABLE_FLAG_GROUP_HEADER);
    MEL_PASS();
}

MEL_TEST(child_traversal)
{
    Mel_Widget parent, c1, c2, c3;
    mel_widget_init(&parent);
    mel_widget_init(&c1);
    mel_widget_init(&c2);
    mel_widget_init(&c3);

    mel_widget_add_child(&parent, &c1);
    mel_widget_add_child(&parent, &c2);
    mel_widget_add_child(&parent, &c3);

    Mel_Layoutable* first = mel_layoutable_first_child(&parent.layoutable);
    MEL_ASSERT_NOT_NULL(first);
    MEL_ASSERT_EQ(first, &c1.layoutable);

    Mel_Layoutable* second = mel_layoutable_next_sibling(first);
    MEL_ASSERT_NOT_NULL(second);
    MEL_ASSERT_EQ(second, &c2.layoutable);

    Mel_Layoutable* third = mel_layoutable_next_sibling(second);
    MEL_ASSERT_NOT_NULL(third);
    MEL_ASSERT_EQ(third, &c3.layoutable);

    Mel_Layoutable* end = mel_layoutable_next_sibling(third);
    MEL_ASSERT_NULL(end);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(no_children)
{
    Mel_Widget w;
    mel_widget_init(&w);

    Mel_Layoutable* first = mel_layoutable_first_child(&w.layoutable);
    MEL_ASSERT_NULL(first);
    MEL_PASS();
}

MEL_TEST(set_layout_stores)
{
    Mel_Widget w;
    mel_widget_init(&w);

    Mel_Layout fake_layout = {0};
    mel_widget_set_layout(&w, &fake_layout);
    MEL_ASSERT_EQ(w.layout, &fake_layout);

    mel_widget_set_layout(&w, nullptr);
    MEL_ASSERT_NULL(w.layout);
    MEL_PASS();
}

MEL_TEST(perform_layout_with_box_layout)
{
    Mel_Widget parent, c1, c2, c3;
    mel_widget_init(&parent);
    mel_widget_init(&c1);
    mel_widget_init(&c2);
    mel_widget_init(&c3);

    c1.fixed_size = mel_vec2(100.0f, 30.0f);
    c2.fixed_size = mel_vec2(100.0f, 40.0f);
    c3.fixed_size = mel_vec2(100.0f, 50.0f);

    mel_widget_add_child(&parent, &c1);
    mel_widget_add_child(&parent, &c2);
    mel_widget_add_child(&parent, &c3);

    parent.size = mel_vec2(200.0f, 300.0f);

    Mel_BoxLayout box;
    mel_box_layout_init(&box, .orientation = MEL_ORIENTATION_VERTICAL, .alignment = MEL_ALIGN_MIN);
    mel_widget_set_layout(&parent, &box.base);

    mel_widget_perform_layout(&parent);

    MEL_ASSERT_FLOAT_EQ(c1.pos.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(c1.pos.y, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(c1.size.x, 100.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(c1.size.y, 30.0f, 0.001f);

    MEL_ASSERT_FLOAT_EQ(c2.pos.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(c2.pos.y, 30.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(c2.size.x, 100.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(c2.size.y, 40.0f, 0.001f);

    MEL_ASSERT_FLOAT_EQ(c3.pos.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(c3.pos.y, 70.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(c3.size.x, 100.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(c3.size.y, 50.0f, 0.001f);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(perform_layout_no_layout_no_crash)
{
    Mel_Widget parent, child;
    mel_widget_init(&parent);
    mel_widget_init(&child);

    mel_widget_add_child(&parent, &child);
    mel_widget_perform_layout(&parent);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(perform_layout_recursive)
{
    Mel_Widget root, mid, leaf;
    mel_widget_init(&root);
    mel_widget_init(&mid);
    mel_widget_init(&leaf);

    leaf.fixed_size = mel_vec2(50.0f, 20.0f);

    mel_widget_add_child(&root, &mid);
    mel_widget_add_child(&mid, &leaf);

    root.size = mel_vec2(300.0f, 300.0f);
    mid.size = mel_vec2(200.0f, 200.0f);

    Mel_BoxLayout box_root, box_mid;
    mel_box_layout_init(&box_root, .orientation = MEL_ORIENTATION_VERTICAL);
    mel_box_layout_init(&box_mid, .orientation = MEL_ORIENTATION_HORIZONTAL);
    mel_widget_set_layout(&root, &box_root.base);
    mel_widget_set_layout(&mid, &box_mid.base);

    mel_widget_perform_layout(&root);

    MEL_ASSERT_FLOAT_EQ(leaf.pos.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(leaf.pos.y, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(leaf.size.x, 50.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(leaf.size.y, 20.0f, 0.001f);

    mel_widget_destroy(&root);
    MEL_PASS();
}

static i32 s_draw_order[16];
static i32 s_draw_count;

static void mock_draw(Mel_Widget* w, void* ctx)
{
    MEL_UNUSED(ctx);
    s_draw_order[s_draw_count++] = w->type_tag;
}

MEL_TEST(contains_inside)
{
    Mel_Widget w;
    mel_widget_init(&w);
    mel_widget_set_position(&w, mel_vec2(10.0f, 20.0f));
    mel_widget_set_size(&w, mel_vec2(100.0f, 50.0f));

    MEL_ASSERT(mel_widget_contains(&w, mel_vec2(10.0f, 20.0f)));
    MEL_ASSERT(mel_widget_contains(&w, mel_vec2(50.0f, 40.0f)));
    MEL_ASSERT(mel_widget_contains(&w, mel_vec2(109.9f, 69.9f)));
    MEL_PASS();
}

MEL_TEST(contains_outside)
{
    Mel_Widget w;
    mel_widget_init(&w);
    mel_widget_set_position(&w, mel_vec2(10.0f, 20.0f));
    mel_widget_set_size(&w, mel_vec2(100.0f, 50.0f));

    MEL_ASSERT(!mel_widget_contains(&w, mel_vec2(9.9f, 20.0f)));
    MEL_ASSERT(!mel_widget_contains(&w, mel_vec2(10.0f, 19.9f)));
    MEL_ASSERT(!mel_widget_contains(&w, mel_vec2(110.0f, 20.0f)));
    MEL_ASSERT(!mel_widget_contains(&w, mel_vec2(10.0f, 70.0f)));
    MEL_PASS();
}

MEL_TEST(contains_zero_size)
{
    Mel_Widget w;
    mel_widget_init(&w);
    mel_widget_set_position(&w, mel_vec2(10.0f, 10.0f));

    MEL_ASSERT(!mel_widget_contains(&w, mel_vec2(10.0f, 10.0f)));
    MEL_PASS();
}

MEL_TEST(hit_test_single)
{
    Mel_Widget w;
    mel_widget_init(&w);
    mel_widget_set_position(&w, mel_vec2(0.0f, 0.0f));
    mel_widget_set_size(&w, mel_vec2(100.0f, 100.0f));

    MEL_ASSERT_EQ(mel_widget_hit_test(&w, mel_vec2(50.0f, 50.0f)), &w);
    MEL_ASSERT_NULL(mel_widget_hit_test(&w, mel_vec2(200.0f, 200.0f)));
    MEL_PASS();
}

MEL_TEST(hit_test_returns_deepest)
{
    Mel_Widget parent, child;
    mel_widget_init(&parent);
    mel_widget_init(&child);

    mel_widget_set_position(&parent, mel_vec2(0.0f, 0.0f));
    mel_widget_set_size(&parent, mel_vec2(200.0f, 200.0f));
    mel_widget_set_position(&child, mel_vec2(10.0f, 10.0f));
    mel_widget_set_size(&child, mel_vec2(50.0f, 50.0f));
    mel_widget_add_child(&parent, &child);

    MEL_ASSERT_EQ(mel_widget_hit_test(&parent, mel_vec2(20.0f, 20.0f)), &child);
    MEL_ASSERT_EQ(mel_widget_hit_test(&parent, mel_vec2(150.0f, 150.0f)), &parent);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(hit_test_skips_invisible)
{
    Mel_Widget parent, child;
    mel_widget_init(&parent);
    mel_widget_init(&child);

    mel_widget_set_position(&parent, mel_vec2(0.0f, 0.0f));
    mel_widget_set_size(&parent, mel_vec2(200.0f, 200.0f));
    mel_widget_set_position(&child, mel_vec2(10.0f, 10.0f));
    mel_widget_set_size(&child, mel_vec2(50.0f, 50.0f));
    mel_widget_set_visible(&child, false);
    mel_widget_add_child(&parent, &child);

    MEL_ASSERT_EQ(mel_widget_hit_test(&parent, mel_vec2(20.0f, 20.0f)), &parent);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(hit_test_skips_disabled)
{
    Mel_Widget parent, child;
    mel_widget_init(&parent);
    mel_widget_init(&child);

    mel_widget_set_position(&parent, mel_vec2(0.0f, 0.0f));
    mel_widget_set_size(&parent, mel_vec2(200.0f, 200.0f));
    mel_widget_set_position(&child, mel_vec2(10.0f, 10.0f));
    mel_widget_set_size(&child, mel_vec2(50.0f, 50.0f));
    mel_widget_set_enabled(&child, false);
    mel_widget_add_child(&parent, &child);

    MEL_ASSERT_EQ(mel_widget_hit_test(&parent, mel_vec2(20.0f, 20.0f)), &parent);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(draw_traversal_order)
{
    Mel_Widget root, c1, c2;
    mel_widget_init(&root);
    mel_widget_init(&c1);
    mel_widget_init(&c2);

    root.type_tag = 1;
    c1.type_tag = 2;
    c2.type_tag = 3;
    root.draw = mock_draw;
    c1.draw = mock_draw;
    c2.draw = mock_draw;

    mel_widget_set_size(&root, mel_vec2(100.0f, 100.0f));
    mel_widget_add_child(&root, &c1);
    mel_widget_add_child(&root, &c2);

    s_draw_count = 0;
    mel_widget_draw(&root, nullptr);

    MEL_ASSERT_EQ(s_draw_count, 3);
    MEL_ASSERT_EQ(s_draw_order[0], 1);
    MEL_ASSERT_EQ(s_draw_order[1], 2);
    MEL_ASSERT_EQ(s_draw_order[2], 3);

    mel_widget_destroy(&root);
    MEL_PASS();
}

MEL_TEST(draw_skips_invisible)
{
    Mel_Widget root, child;
    mel_widget_init(&root);
    mel_widget_init(&child);

    root.type_tag = 1;
    child.type_tag = 2;
    root.draw = mock_draw;
    child.draw = mock_draw;

    mel_widget_set_visible(&child, false);
    mel_widget_add_child(&root, &child);

    s_draw_count = 0;
    mel_widget_draw(&root, nullptr);

    MEL_ASSERT_EQ(s_draw_count, 1);
    MEL_ASSERT_EQ(s_draw_order[0], 1);

    mel_widget_destroy(&root);
    MEL_PASS();
}

MEL_TEST(draw_null_callback_no_crash)
{
    Mel_Widget w;
    mel_widget_init(&w);

    mel_widget_draw(&w, nullptr);
    MEL_PASS();
}

static bool s_mouse_down_called;
static Mel_Vec2 s_mouse_down_pos;
static i32 s_mouse_down_button;

static bool mock_mouse_down(Mel_Widget* w, Mel_Vec2 pos, i32 button)
{
    MEL_UNUSED(w);
    s_mouse_down_called = true;
    s_mouse_down_pos = pos;
    s_mouse_down_button = button;
    return true;
}

MEL_TEST(mouse_down_dispatches)
{
    Mel_Widget w;
    mel_widget_init(&w);
    mel_widget_set_position(&w, mel_vec2(0.0f, 0.0f));
    mel_widget_set_size(&w, mel_vec2(100.0f, 100.0f));
    w.on_mouse_down = mock_mouse_down;

    s_mouse_down_called = false;
    bool consumed = mel_widget_mouse_down(&w, mel_vec2(50.0f, 50.0f), 1);

    MEL_ASSERT(consumed);
    MEL_ASSERT(s_mouse_down_called);
    MEL_ASSERT_EQ(s_mouse_down_button, 1);
    MEL_PASS();
}

MEL_TEST(mouse_down_outside_does_not_dispatch)
{
    Mel_Widget w;
    mel_widget_init(&w);
    mel_widget_set_position(&w, mel_vec2(0.0f, 0.0f));
    mel_widget_set_size(&w, mel_vec2(100.0f, 100.0f));
    w.on_mouse_down = mock_mouse_down;

    s_mouse_down_called = false;
    bool consumed = mel_widget_mouse_down(&w, mel_vec2(200.0f, 200.0f), 1);

    MEL_ASSERT(!consumed);
    MEL_ASSERT(!s_mouse_down_called);
    MEL_PASS();
}

MEL_TEST(mouse_down_children_first)
{
    Mel_Widget parent, child;
    mel_widget_init(&parent);
    mel_widget_init(&child);

    mel_widget_set_position(&parent, mel_vec2(0.0f, 0.0f));
    mel_widget_set_size(&parent, mel_vec2(200.0f, 200.0f));
    mel_widget_set_position(&child, mel_vec2(10.0f, 10.0f));
    mel_widget_set_size(&child, mel_vec2(50.0f, 50.0f));
    child.on_mouse_down = mock_mouse_down;
    mel_widget_add_child(&parent, &child);

    s_mouse_down_called = false;
    bool consumed = mel_widget_mouse_down(&parent, mel_vec2(20.0f, 20.0f), 1);

    MEL_ASSERT(consumed);
    MEL_ASSERT(s_mouse_down_called);

    mel_widget_destroy(&parent);
    MEL_PASS();
}

static bool s_mouse_up_called;

static bool mock_mouse_up(Mel_Widget* w, Mel_Vec2 pos, i32 button)
{
    MEL_UNUSED(w);
    MEL_UNUSED(pos);
    MEL_UNUSED(button);
    s_mouse_up_called = true;
    return true;
}

MEL_TEST(mouse_up_dispatches_outside)
{
    Mel_Widget w;
    mel_widget_init(&w);
    mel_widget_set_position(&w, mel_vec2(0.0f, 0.0f));
    mel_widget_set_size(&w, mel_vec2(100.0f, 100.0f));
    w.on_mouse_up = mock_mouse_up;

    s_mouse_up_called = false;
    bool consumed = mel_widget_mouse_up(&w, mel_vec2(200.0f, 200.0f), 1);

    MEL_ASSERT(consumed);
    MEL_ASSERT(s_mouse_up_called);
    MEL_PASS();
}

static bool s_mouse_move_called;

static bool mock_mouse_move(Mel_Widget* w, Mel_Vec2 pos)
{
    MEL_UNUSED(w);
    MEL_UNUSED(pos);
    s_mouse_move_called = true;
    return false;
}

MEL_TEST(mouse_move_dispatches_outside)
{
    Mel_Widget w;
    mel_widget_init(&w);
    mel_widget_set_position(&w, mel_vec2(0.0f, 0.0f));
    mel_widget_set_size(&w, mel_vec2(100.0f, 100.0f));
    w.on_mouse_move = mock_mouse_move;

    s_mouse_move_called = false;
    mel_widget_mouse_move(&w, mel_vec2(200.0f, 200.0f));

    MEL_ASSERT(s_mouse_move_called);
    MEL_PASS();
}

static Mel_Vec2 mock_measure_result;

static Mel_Vec2 mock_measure(Mel_Widget* w)
{
    MEL_UNUSED(w);
    return mock_measure_result;
}

MEL_TEST(measure_callback_preferred_size)
{
    Mel_Widget w;
    mel_widget_init(&w);
    w.size = mel_vec2(10.0f, 10.0f);
    mock_measure_result = mel_vec2(200.0f, 100.0f);
    w.measure = mock_measure;

    Mel_Vec2 pref = mel_layoutable_preferred_size(&w.layoutable);
    MEL_ASSERT_FLOAT_EQ(pref.x, 200.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(pref.y, 100.0f, 0.001f);
    MEL_PASS();
}

static bool s_destroy_called;

static void mock_on_destroy(Mel_Widget* w)
{
    MEL_UNUSED(w);
    s_destroy_called = true;
}

MEL_TEST(on_destroy_called)
{
    Mel_Widget w;
    mel_widget_init(&w);
    w.on_destroy = mock_on_destroy;

    s_destroy_called = false;
    mel_widget_destroy(&w);

    MEL_ASSERT(s_destroy_called);
    MEL_PASS();
}

static i32 s_destroy_order[8];
static i32 s_destroy_order_count;

static void mock_on_destroy_order(Mel_Widget* w)
{
    s_destroy_order[s_destroy_order_count++] = w->type_tag;
}

MEL_TEST(on_destroy_children_first)
{
    Mel_Widget parent, child;
    mel_widget_init(&parent);
    mel_widget_init(&child);

    parent.type_tag = 1;
    child.type_tag = 2;
    parent.on_destroy = mock_on_destroy_order;
    child.on_destroy = mock_on_destroy_order;
    mel_widget_add_child(&parent, &child);

    s_destroy_order_count = 0;
    mel_widget_destroy(&parent);

    MEL_ASSERT_EQ(s_destroy_order_count, 2);
    MEL_ASSERT_EQ(s_destroy_order[0], 2);
    MEL_ASSERT_EQ(s_destroy_order[1], 1);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Retained Widget (Mel_Widget) Tests");

    MEL_RUN_TEST(init_defaults);
    MEL_RUN_TEST(init_zeroes_struct);
    MEL_RUN_TEST(destroy_empty);
    MEL_RUN_TEST(destroy_recursive);
    MEL_RUN_TEST(destroy_deep_hierarchy);

    MEL_RUN_TEST(add_single_child);
    MEL_RUN_TEST(add_multiple_children_preserves_order);
    MEL_RUN_TEST(remove_first_child);
    MEL_RUN_TEST(remove_middle_child);
    MEL_RUN_TEST(remove_last_child);
    MEL_RUN_TEST(remove_only_child);
    MEL_RUN_TEST(remove_and_readd);
    MEL_RUN_TEST(deep_hierarchy);

    MEL_RUN_TEST(set_visible);
    MEL_RUN_TEST(set_enabled);
    MEL_RUN_TEST(set_position);
    MEL_RUN_TEST(set_size);

    MEL_RUN_TEST(preferred_size_fallback);
    MEL_RUN_TEST(position_roundtrip);
    MEL_RUN_TEST(size_roundtrip);
    MEL_RUN_TEST(fixed_size);
    MEL_RUN_TEST(visibility);
    MEL_RUN_TEST(flags);
    MEL_RUN_TEST(child_traversal);
    MEL_RUN_TEST(no_children);

    MEL_RUN_TEST(set_layout_stores);
    MEL_RUN_TEST(perform_layout_with_box_layout);
    MEL_RUN_TEST(perform_layout_no_layout_no_crash);
    MEL_RUN_TEST(perform_layout_recursive);

    MEL_RUN_TEST(contains_inside);
    MEL_RUN_TEST(contains_outside);
    MEL_RUN_TEST(contains_zero_size);

    MEL_RUN_TEST(hit_test_single);
    MEL_RUN_TEST(hit_test_returns_deepest);
    MEL_RUN_TEST(hit_test_skips_invisible);
    MEL_RUN_TEST(hit_test_skips_disabled);

    MEL_RUN_TEST(draw_traversal_order);
    MEL_RUN_TEST(draw_skips_invisible);
    MEL_RUN_TEST(draw_null_callback_no_crash);

    MEL_RUN_TEST(mouse_down_dispatches);
    MEL_RUN_TEST(mouse_down_outside_does_not_dispatch);
    MEL_RUN_TEST(mouse_down_children_first);
    MEL_RUN_TEST(mouse_up_dispatches_outside);
    MEL_RUN_TEST(mouse_move_dispatches_outside);

    MEL_RUN_TEST(measure_callback_preferred_size);

    MEL_RUN_TEST(on_destroy_called);
    MEL_RUN_TEST(on_destroy_children_first);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
