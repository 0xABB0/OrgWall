#include "../melody/test.harness.h"
#include "../melody/ui.native.ctrl.h"
#include <string.h>

static i32 s_create_count;
static i32 s_destroy_count;
static i32 s_set_frame_count;
static i32 s_set_visible_count;
static i32 s_set_enabled_count;
static i32 s_add_child_count;
static i32 s_remove_child_count;

static f32 s_last_frame_x, s_last_frame_y, s_last_frame_w, s_last_frame_h;
static bool s_last_visible;
static bool s_last_enabled;

static void reset_counters(void)
{
    s_create_count = 0;
    s_destroy_count = 0;
    s_set_frame_count = 0;
    s_set_visible_count = 0;
    s_set_enabled_count = 0;
    s_add_child_count = 0;
    s_remove_child_count = 0;
    s_last_frame_x = s_last_frame_y = s_last_frame_w = s_last_frame_h = 0;
    s_last_visible = false;
    s_last_enabled = false;
}

static void stub_create(Mel_NCtrl* ctrl) { s_create_count++; (void)ctrl; }
static void stub_destroy(Mel_NCtrl* ctrl) { s_destroy_count++; (void)ctrl; }

static void stub_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    (void)ctrl;
    s_set_frame_count++;
    s_last_frame_x = x;
    s_last_frame_y = y;
    s_last_frame_w = w;
    s_last_frame_h = h;
}

static void stub_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    (void)ctrl;
    s_set_visible_count++;
    s_last_visible = visible;
}

static void stub_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    (void)ctrl;
    s_set_enabled_count++;
    s_last_enabled = enabled;
}

static Mel_Vec2 stub_preferred_size(Mel_NCtrl* ctrl)
{
    (void)ctrl;
    return mel_vec2(42.0f, 24.0f);
}

static void stub_add_child(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent; (void)child;
    s_add_child_count++;
}

static void stub_remove_child(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent; (void)child;
    s_remove_child_count++;
}

static const Mel_NCtrl_VTable s_stub_vtable = {
    .create_backing       = stub_create,
    .destroy_backing      = stub_destroy,
    .set_frame            = stub_set_frame,
    .set_visible          = stub_set_visible,
    .set_enabled          = stub_set_enabled,
    .preferred_size       = stub_preferred_size,
    .add_child_backing    = stub_add_child,
    .remove_child_backing = stub_remove_child,
};

static const Mel_NCtrl_VTable s_empty_vtable = {0};


MEL_TEST(init_null_vtable)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);

    MEL_ASSERT_NULL(ctrl.vtable);
    MEL_ASSERT_NULL(ctrl.backing);
    MEL_ASSERT_NULL(ctrl.parent);
    MEL_ASSERT_NULL(ctrl.first_child);
    MEL_ASSERT_NULL(ctrl.next_sibling);
    MEL_ASSERT_NULL(ctrl.layout);
    MEL_ASSERT(ctrl.visible);
    MEL_ASSERT(ctrl.enabled);
    MEL_ASSERT_FLOAT_EQ(ctrl.pos.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.pos.y, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.size.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.size.y, 0.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(init_does_not_create_backing)
{
    reset_counters();
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, &s_stub_vtable);

    MEL_ASSERT_EQ(ctrl.vtable, &s_stub_vtable);
    MEL_ASSERT_EQ(s_create_count, 0);
    MEL_ASSERT(ctrl.visible);
    MEL_ASSERT(ctrl.enabled);

    mel_nctrl_destroy(&ctrl);
    MEL_PASS();
}

MEL_TEST(create_backing_explicit)
{
    reset_counters();
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, &s_stub_vtable);
    MEL_ASSERT_EQ(s_create_count, 0);

    mel_nctrl_create_backing(&ctrl);
    MEL_ASSERT_EQ(s_create_count, 1);

    mel_nctrl_destroy(&ctrl);
    MEL_PASS();
}

MEL_TEST(create_backing_null_vtable)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);
    mel_nctrl_create_backing(&ctrl);
    MEL_PASS();
}

MEL_TEST(init_with_empty_vtable_no_crash)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, &s_empty_vtable);

    MEL_ASSERT_EQ(ctrl.vtable, &s_empty_vtable);
    MEL_ASSERT(ctrl.visible);
    MEL_ASSERT(ctrl.enabled);

    mel_nctrl_destroy(&ctrl);
    MEL_PASS();
}

MEL_TEST(destroy_calls_backing)
{
    reset_counters();
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, &s_stub_vtable);

    mel_nctrl_destroy(&ctrl);
    MEL_ASSERT_EQ(s_destroy_count, 1);
    MEL_ASSERT_NULL(ctrl.backing);
    MEL_ASSERT_NULL(ctrl.first_child);
    MEL_PASS();
}

MEL_TEST(destroy_recursive)
{
    reset_counters();
    Mel_NCtrl parent, child1, child2;
    mel_nctrl_init(&parent, &s_stub_vtable);
    mel_nctrl_init(&child1, &s_stub_vtable);
    mel_nctrl_init(&child2, &s_stub_vtable);

    mel_nctrl_add_child(&parent, &child1);
    mel_nctrl_add_child(&parent, &child2);

    s_destroy_count = 0;
    mel_nctrl_destroy(&parent);

    MEL_ASSERT_EQ(s_destroy_count, 3);
    MEL_ASSERT_NULL(parent.first_child);
    MEL_PASS();
}

MEL_TEST(add_single_child)
{
    reset_counters();
    Mel_NCtrl parent, child;
    mel_nctrl_init(&parent, &s_stub_vtable);
    mel_nctrl_init(&child, &s_stub_vtable);

    mel_nctrl_add_child(&parent, &child);

    MEL_ASSERT_EQ(parent.first_child, &child);
    MEL_ASSERT_EQ(child.parent, &parent);
    MEL_ASSERT_NULL(child.next_sibling);
    MEL_ASSERT_EQ(s_add_child_count, 1);

    mel_nctrl_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(add_multiple_children_preserves_order)
{
    reset_counters();
    Mel_NCtrl parent, c1, c2, c3;
    mel_nctrl_init(&parent, &s_stub_vtable);
    mel_nctrl_init(&c1, &s_stub_vtable);
    mel_nctrl_init(&c2, &s_stub_vtable);
    mel_nctrl_init(&c3, &s_stub_vtable);

    mel_nctrl_add_child(&parent, &c1);
    mel_nctrl_add_child(&parent, &c2);
    mel_nctrl_add_child(&parent, &c3);

    MEL_ASSERT_EQ(parent.first_child, &c1);
    MEL_ASSERT_EQ(c1.next_sibling, &c2);
    MEL_ASSERT_EQ(c2.next_sibling, &c3);
    MEL_ASSERT_NULL(c3.next_sibling);

    MEL_ASSERT_EQ(c1.parent, &parent);
    MEL_ASSERT_EQ(c2.parent, &parent);
    MEL_ASSERT_EQ(c3.parent, &parent);

    MEL_ASSERT_EQ(s_add_child_count, 3);

    mel_nctrl_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(remove_first_child)
{
    reset_counters();
    Mel_NCtrl parent, c1, c2;
    mel_nctrl_init(&parent, &s_stub_vtable);
    mel_nctrl_init(&c1, &s_stub_vtable);
    mel_nctrl_init(&c2, &s_stub_vtable);

    mel_nctrl_add_child(&parent, &c1);
    mel_nctrl_add_child(&parent, &c2);

    mel_nctrl_remove_child(&parent, &c1);

    MEL_ASSERT_EQ(parent.first_child, &c2);
    MEL_ASSERT_NULL(c1.parent);
    MEL_ASSERT_NULL(c1.next_sibling);
    MEL_ASSERT_EQ(s_remove_child_count, 1);

    mel_nctrl_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(remove_middle_child)
{
    reset_counters();
    Mel_NCtrl parent, c1, c2, c3;
    mel_nctrl_init(&parent, &s_stub_vtable);
    mel_nctrl_init(&c1, &s_stub_vtable);
    mel_nctrl_init(&c2, &s_stub_vtable);
    mel_nctrl_init(&c3, &s_stub_vtable);

    mel_nctrl_add_child(&parent, &c1);
    mel_nctrl_add_child(&parent, &c2);
    mel_nctrl_add_child(&parent, &c3);

    mel_nctrl_remove_child(&parent, &c2);

    MEL_ASSERT_EQ(parent.first_child, &c1);
    MEL_ASSERT_EQ(c1.next_sibling, &c3);
    MEL_ASSERT_NULL(c3.next_sibling);
    MEL_ASSERT_NULL(c2.parent);
    MEL_ASSERT_NULL(c2.next_sibling);

    mel_nctrl_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(remove_last_child)
{
    reset_counters();
    Mel_NCtrl parent, c1, c2;
    mel_nctrl_init(&parent, &s_stub_vtable);
    mel_nctrl_init(&c1, &s_stub_vtable);
    mel_nctrl_init(&c2, &s_stub_vtable);

    mel_nctrl_add_child(&parent, &c1);
    mel_nctrl_add_child(&parent, &c2);

    mel_nctrl_remove_child(&parent, &c2);

    MEL_ASSERT_EQ(parent.first_child, &c1);
    MEL_ASSERT_NULL(c1.next_sibling);
    MEL_ASSERT_NULL(c2.parent);

    mel_nctrl_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(remove_only_child)
{
    reset_counters();
    Mel_NCtrl parent, child;
    mel_nctrl_init(&parent, &s_stub_vtable);
    mel_nctrl_init(&child, &s_stub_vtable);

    mel_nctrl_add_child(&parent, &child);
    mel_nctrl_remove_child(&parent, &child);

    MEL_ASSERT_NULL(parent.first_child);
    MEL_ASSERT_NULL(child.parent);
    MEL_ASSERT_NULL(child.next_sibling);

    mel_nctrl_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(set_visible_dispatches)
{
    reset_counters();
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, &s_stub_vtable);

    mel_nctrl_set_visible(&ctrl, false);
    MEL_ASSERT(!ctrl.visible);
    MEL_ASSERT_EQ(s_set_visible_count, 1);
    MEL_ASSERT(!s_last_visible);

    mel_nctrl_set_visible(&ctrl, true);
    MEL_ASSERT(ctrl.visible);
    MEL_ASSERT_EQ(s_set_visible_count, 2);
    MEL_ASSERT(s_last_visible);

    mel_nctrl_destroy(&ctrl);
    MEL_PASS();
}

MEL_TEST(set_visible_null_vtable)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);

    mel_nctrl_set_visible(&ctrl, false);
    MEL_ASSERT(!ctrl.visible);

    mel_nctrl_set_visible(&ctrl, true);
    MEL_ASSERT(ctrl.visible);
    MEL_PASS();
}

MEL_TEST(set_enabled_dispatches)
{
    reset_counters();
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, &s_stub_vtable);

    mel_nctrl_set_enabled(&ctrl, false);
    MEL_ASSERT(!ctrl.enabled);
    MEL_ASSERT_EQ(s_set_enabled_count, 1);
    MEL_ASSERT(!s_last_enabled);

    mel_nctrl_set_enabled(&ctrl, true);
    MEL_ASSERT(ctrl.enabled);
    MEL_ASSERT_EQ(s_set_enabled_count, 2);
    MEL_ASSERT(s_last_enabled);

    mel_nctrl_destroy(&ctrl);
    MEL_PASS();
}

MEL_TEST(set_enabled_null_vtable)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);

    mel_nctrl_set_enabled(&ctrl, false);
    MEL_ASSERT(!ctrl.enabled);

    mel_nctrl_set_enabled(&ctrl, true);
    MEL_ASSERT(ctrl.enabled);
    MEL_PASS();
}

MEL_TEST(set_position_dispatches)
{
    reset_counters();
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, &s_stub_vtable);
    ctrl.size = mel_vec2(100.0f, 50.0f);

    mel_nctrl_set_position(&ctrl, mel_vec2(10.0f, 20.0f));

    MEL_ASSERT_FLOAT_EQ(ctrl.pos.x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.pos.y, 20.0f, 0.001f);
    MEL_ASSERT_EQ(s_set_frame_count, 1);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_y, 20.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_w, 100.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_h, 50.0f, 0.001f);

    mel_nctrl_destroy(&ctrl);
    MEL_PASS();
}

MEL_TEST(set_size_dispatches)
{
    reset_counters();
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, &s_stub_vtable);
    ctrl.pos = mel_vec2(5.0f, 15.0f);

    mel_nctrl_set_size(&ctrl, mel_vec2(200.0f, 150.0f));

    MEL_ASSERT_FLOAT_EQ(ctrl.size.x, 200.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.size.y, 150.0f, 0.001f);
    MEL_ASSERT_EQ(s_set_frame_count, 1);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_x, 5.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_y, 15.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_w, 200.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_h, 150.0f, 0.001f);

    mel_nctrl_destroy(&ctrl);
    MEL_PASS();
}

MEL_TEST(set_position_null_vtable)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);

    mel_nctrl_set_position(&ctrl, mel_vec2(33.0f, 44.0f));
    MEL_ASSERT_FLOAT_EQ(ctrl.pos.x, 33.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.pos.y, 44.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(set_size_null_vtable)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);

    mel_nctrl_set_size(&ctrl, mel_vec2(77.0f, 88.0f));
    MEL_ASSERT_FLOAT_EQ(ctrl.size.x, 77.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.size.y, 88.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(layoutable_preferred_size_uses_vtable)
{
    reset_counters();
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, &s_stub_vtable);

    Mel_Vec2 pref = mel_layoutable_preferred_size(&ctrl.layoutable);
    MEL_ASSERT_FLOAT_EQ(pref.x, 42.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(pref.y, 24.0f, 0.001f);

    mel_nctrl_destroy(&ctrl);
    MEL_PASS();
}

MEL_TEST(layoutable_preferred_size_fallback)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);
    ctrl.size = mel_vec2(99.0f, 55.0f);

    Mel_Vec2 pref = mel_layoutable_preferred_size(&ctrl.layoutable);
    MEL_ASSERT_FLOAT_EQ(pref.x, 99.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(pref.y, 55.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(layoutable_position_roundtrip)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);

    mel_layoutable_set_position(&ctrl.layoutable, mel_vec2(10.0f, 20.0f));
    Mel_Vec2 pos = mel_layoutable_get_position(&ctrl.layoutable);
    MEL_ASSERT_FLOAT_EQ(pos.x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(pos.y, 20.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(layoutable_size_roundtrip)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);

    mel_layoutable_set_size(&ctrl.layoutable, mel_vec2(300.0f, 400.0f));
    Mel_Vec2 sz = mel_layoutable_get_size(&ctrl.layoutable);
    MEL_ASSERT_FLOAT_EQ(sz.x, 300.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(sz.y, 400.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(layoutable_fixed_size)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);
    ctrl.fixed_size = mel_vec2(64.0f, 32.0f);

    Mel_Vec2 fs = mel_layoutable_get_fixed_size(&ctrl.layoutable);
    MEL_ASSERT_FLOAT_EQ(fs.x, 64.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(fs.y, 32.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(layoutable_visibility)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);

    MEL_ASSERT(mel_layoutable_is_visible(&ctrl.layoutable));

    ctrl.visible = false;
    MEL_ASSERT(!mel_layoutable_is_visible(&ctrl.layoutable));
    MEL_PASS();
}

MEL_TEST(layoutable_flags)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);

    MEL_ASSERT_EQ(mel_layoutable_get_flags(&ctrl.layoutable), (u32)0);

    ctrl.flags = MEL_LAYOUTABLE_FLAG_GROUP_HEADER;
    MEL_ASSERT_EQ(mel_layoutable_get_flags(&ctrl.layoutable), (u32)MEL_LAYOUTABLE_FLAG_GROUP_HEADER);
    MEL_PASS();
}

MEL_TEST(layoutable_child_traversal)
{
    Mel_NCtrl parent, c1, c2, c3;
    mel_nctrl_init(&parent, nullptr);
    mel_nctrl_init(&c1, nullptr);
    mel_nctrl_init(&c2, nullptr);
    mel_nctrl_init(&c3, nullptr);

    mel_nctrl_add_child(&parent, &c1);
    mel_nctrl_add_child(&parent, &c2);
    mel_nctrl_add_child(&parent, &c3);

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

    mel_nctrl_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(layoutable_no_children)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);

    Mel_Layoutable* first = mel_layoutable_first_child(&ctrl.layoutable);
    MEL_ASSERT_NULL(first);
    MEL_PASS();
}

MEL_TEST(set_layout_stores_layout)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);

    Mel_Layout fake_layout = {0};
    mel_nctrl_set_layout(&ctrl, &fake_layout);
    MEL_ASSERT_EQ(ctrl.layout, &fake_layout);

    mel_nctrl_set_layout(&ctrl, nullptr);
    MEL_ASSERT_NULL(ctrl.layout);
    MEL_PASS();
}

MEL_TEST(layoutable_set_position_dispatches_frame)
{
    reset_counters();
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, &s_stub_vtable);
    ctrl.size = mel_vec2(80.0f, 60.0f);

    mel_layoutable_set_position(&ctrl.layoutable, mel_vec2(7.0f, 13.0f));

    MEL_ASSERT_FLOAT_EQ(ctrl.pos.x, 7.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.pos.y, 13.0f, 0.001f);
    MEL_ASSERT_EQ(s_set_frame_count, 1);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_x, 7.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_y, 13.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_w, 80.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_h, 60.0f, 0.001f);

    mel_nctrl_destroy(&ctrl);
    MEL_PASS();
}

MEL_TEST(layoutable_set_size_dispatches_frame)
{
    reset_counters();
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, &s_stub_vtable);
    ctrl.pos = mel_vec2(3.0f, 4.0f);

    mel_layoutable_set_size(&ctrl.layoutable, mel_vec2(120.0f, 90.0f));

    MEL_ASSERT_FLOAT_EQ(ctrl.size.x, 120.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.size.y, 90.0f, 0.001f);
    MEL_ASSERT_EQ(s_set_frame_count, 1);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_x, 3.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_y, 4.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_w, 120.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(s_last_frame_h, 90.0f, 0.001f);

    mel_nctrl_destroy(&ctrl);
    MEL_PASS();
}

MEL_TEST(perform_layout_no_layout_no_crash)
{
    Mel_NCtrl parent, child;
    mel_nctrl_init(&parent, nullptr);
    mel_nctrl_init(&child, nullptr);

    mel_nctrl_add_child(&parent, &child);

    mel_nctrl_perform_layout(&parent);

    mel_nctrl_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(deep_hierarchy)
{
    Mel_NCtrl root, level1, level2, level3;
    mel_nctrl_init(&root, nullptr);
    mel_nctrl_init(&level1, nullptr);
    mel_nctrl_init(&level2, nullptr);
    mel_nctrl_init(&level3, nullptr);

    mel_nctrl_add_child(&root, &level1);
    mel_nctrl_add_child(&level1, &level2);
    mel_nctrl_add_child(&level2, &level3);

    MEL_ASSERT_EQ(root.first_child, &level1);
    MEL_ASSERT_EQ(level1.parent, &root);
    MEL_ASSERT_EQ(level1.first_child, &level2);
    MEL_ASSERT_EQ(level2.parent, &level1);
    MEL_ASSERT_EQ(level2.first_child, &level3);
    MEL_ASSERT_EQ(level3.parent, &level2);
    MEL_ASSERT_NULL(level3.first_child);

    mel_nctrl_destroy(&root);
    MEL_PASS();
}

MEL_TEST(remove_and_readd_child)
{
    Mel_NCtrl parent, child;
    mel_nctrl_init(&parent, nullptr);
    mel_nctrl_init(&child, nullptr);

    mel_nctrl_add_child(&parent, &child);
    MEL_ASSERT_EQ(parent.first_child, &child);

    mel_nctrl_remove_child(&parent, &child);
    MEL_ASSERT_NULL(parent.first_child);
    MEL_ASSERT_NULL(child.parent);

    mel_nctrl_add_child(&parent, &child);
    MEL_ASSERT_EQ(parent.first_child, &child);
    MEL_ASSERT_EQ(child.parent, &parent);

    mel_nctrl_destroy(&parent);
    MEL_PASS();
}

MEL_TEST(init_zeroes_struct)
{
    Mel_NCtrl ctrl;
    memset(&ctrl, 0xFF, sizeof(ctrl));
    mel_nctrl_init(&ctrl, nullptr);

    MEL_ASSERT_NULL(ctrl.backing);
    MEL_ASSERT_NULL(ctrl.parent);
    MEL_ASSERT_NULL(ctrl.first_child);
    MEL_ASSERT_NULL(ctrl.next_sibling);
    MEL_ASSERT_NULL(ctrl.layout);
    MEL_ASSERT_EQ(ctrl.flags, (u32)0);
    MEL_ASSERT_FLOAT_EQ(ctrl.pos.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.pos.y, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.size.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.size.y, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.fixed_size.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ctrl.fixed_size.y, 0.0f, 0.001f);
    MEL_PASS();
}

MEL_TEST(destroy_with_null_vtable)
{
    Mel_NCtrl ctrl;
    mel_nctrl_init(&ctrl, nullptr);
    mel_nctrl_destroy(&ctrl);

    MEL_ASSERT_NULL(ctrl.backing);
    MEL_ASSERT_NULL(ctrl.first_child);
    MEL_PASS();
}

MEL_TEST(destroy_deep_hierarchy)
{
    reset_counters();
    Mel_NCtrl root, l1a, l1b, l2a;
    mel_nctrl_init(&root, &s_stub_vtable);
    mel_nctrl_init(&l1a, &s_stub_vtable);
    mel_nctrl_init(&l1b, &s_stub_vtable);
    mel_nctrl_init(&l2a, &s_stub_vtable);

    mel_nctrl_add_child(&root, &l1a);
    mel_nctrl_add_child(&root, &l1b);
    mel_nctrl_add_child(&l1a, &l2a);

    s_destroy_count = 0;
    mel_nctrl_destroy(&root);

    MEL_ASSERT_EQ(s_destroy_count, 4);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Native Control (Mel_NCtrl) Tests");

    MEL_RUN_TEST(init_null_vtable);
    MEL_RUN_TEST(init_does_not_create_backing);
    MEL_RUN_TEST(create_backing_explicit);
    MEL_RUN_TEST(create_backing_null_vtable);
    MEL_RUN_TEST(init_with_empty_vtable_no_crash);
    MEL_RUN_TEST(init_zeroes_struct);
    MEL_RUN_TEST(destroy_calls_backing);
    MEL_RUN_TEST(destroy_recursive);
    MEL_RUN_TEST(destroy_with_null_vtable);
    MEL_RUN_TEST(destroy_deep_hierarchy);
    MEL_RUN_TEST(add_single_child);
    MEL_RUN_TEST(add_multiple_children_preserves_order);
    MEL_RUN_TEST(remove_first_child);
    MEL_RUN_TEST(remove_middle_child);
    MEL_RUN_TEST(remove_last_child);
    MEL_RUN_TEST(remove_only_child);
    MEL_RUN_TEST(remove_and_readd_child);
    MEL_RUN_TEST(set_visible_dispatches);
    MEL_RUN_TEST(set_visible_null_vtable);
    MEL_RUN_TEST(set_enabled_dispatches);
    MEL_RUN_TEST(set_enabled_null_vtable);
    MEL_RUN_TEST(set_position_dispatches);
    MEL_RUN_TEST(set_size_dispatches);
    MEL_RUN_TEST(set_position_null_vtable);
    MEL_RUN_TEST(set_size_null_vtable);
    MEL_RUN_TEST(layoutable_preferred_size_uses_vtable);
    MEL_RUN_TEST(layoutable_preferred_size_fallback);
    MEL_RUN_TEST(layoutable_position_roundtrip);
    MEL_RUN_TEST(layoutable_size_roundtrip);
    MEL_RUN_TEST(layoutable_fixed_size);
    MEL_RUN_TEST(layoutable_visibility);
    MEL_RUN_TEST(layoutable_flags);
    MEL_RUN_TEST(layoutable_child_traversal);
    MEL_RUN_TEST(layoutable_no_children);
    MEL_RUN_TEST(set_layout_stores_layout);
    MEL_RUN_TEST(layoutable_set_position_dispatches_frame);
    MEL_RUN_TEST(layoutable_set_size_dispatches_frame);
    MEL_RUN_TEST(perform_layout_no_layout_no_crash);
    MEL_RUN_TEST(deep_hierarchy);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
