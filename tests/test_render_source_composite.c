#include "../melody/test.harness.h"
#include "../melody/render.source.composite.h"
#include "../melody/render.source.type.h"
#include "../melody/render.manager.fwd.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.array.h"

#include <string.h>

typedef struct {
    u32 sync_call_count;
    void* last_mgr;
} Mock_Source_Data;

static void mock_sync(Mel_Render_Source* self, Mel_Render_Manager* mgr)
{
    Mock_Source_Data* data = mel_render_source_instance(self);
    data->sync_call_count++;
    data->last_mgr = mgr;
}

static const Mel_Render_Source_Type mock_source_type = {
    .name = { .data = (u8*)"mock", .len = 4 },
    .sync            = mock_sync,
    .shutdown        = nullptr,
    .instance_size   = sizeof(Mock_Source_Data),
};

static Mel_Render_Source* make_mock_source(void)
{
    Mel_Render_Source* s = mel_render_source_create(
        .type = &mock_source_type,
        .alloc = mel_alloc_heap());
    memset(mel_render_source_instance(s), 0, sizeof(Mock_Source_Data));
    return s;
}

MEL_TEST(composite_create_destroy, .tags = "render")
{
    Mel_Render_Source* composite = mel_source_composite_create(mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(composite);
    MEL_ASSERT(composite->type == &mel_source_composite_type);

    mel_render_source_destroy(composite);
}

MEL_TEST(composite_add_children, .tags = "render")
{
    Mel_Render_Source* composite = mel_source_composite_create(mel_alloc_heap());

    Mel_Render_Source* child_a = make_mock_source();
    Mel_Render_Source* child_b = make_mock_source();
    Mel_Render_Source* child_c = make_mock_source();

    mel_source_composite_add(composite, child_a);
    mel_source_composite_add(composite, child_b);
    mel_source_composite_add(composite, child_c);

    typedef struct { Mel_Array(Mel_Render_Source*) children; } Mel_Composite_Source_Data;
    Mel_Composite_Source_Data* data = mel_render_source_instance(composite);
    MEL_ASSERT_EQ(data->children.count, (usize)3);
    MEL_ASSERT(data->children.items[0] == child_a);
    MEL_ASSERT(data->children.items[1] == child_b);
    MEL_ASSERT(data->children.items[2] == child_c);

    mel_render_source_destroy(composite);
    mel_render_source_destroy(child_a);
    mel_render_source_destroy(child_b);
    mel_render_source_destroy(child_c);
}

MEL_TEST(composite_sync_propagates_to_children, .tags = "render")
{
    Mel_Render_Source* composite = mel_source_composite_create(mel_alloc_heap());

    Mel_Render_Source* child_a = make_mock_source();
    Mel_Render_Source* child_b = make_mock_source();

    mel_source_composite_add(composite, child_a);
    mel_source_composite_add(composite, child_b);

    u32 fake_mgr = 0xDEADBEEF;

    mel_render_source_sync(composite, (Mel_Render_Manager*)&fake_mgr);

    Mock_Source_Data* da = mel_render_source_instance(child_a);
    Mock_Source_Data* db = mel_render_source_instance(child_b);

    MEL_ASSERT_EQ(da->sync_call_count, (u32)1);
    MEL_ASSERT_EQ(db->sync_call_count, (u32)1);

    MEL_ASSERT(da->last_mgr == &fake_mgr);
    MEL_ASSERT(db->last_mgr == &fake_mgr);

    mel_render_source_sync(composite, (Mel_Render_Manager*)&fake_mgr);

    MEL_ASSERT_EQ(da->sync_call_count, (u32)2);
    MEL_ASSERT_EQ(db->sync_call_count, (u32)2);

    mel_render_source_destroy(composite);
    mel_render_source_destroy(child_a);
    mel_render_source_destroy(child_b);
}

MEL_TEST(composite_children_share_sync_target, .tags = "render")
{
    Mel_Render_Source* composite = mel_source_composite_create(mel_alloc_heap());

    Mel_Render_Source* child_a = make_mock_source();
    Mel_Render_Source* child_b = make_mock_source();

    mel_source_composite_add(composite, child_a);
    mel_source_composite_add(composite, child_b);

    u32 fake_mgr = 0x12345678;
    mel_render_source_sync(composite, (Mel_Render_Manager*)&fake_mgr);

    Mock_Source_Data* da = mel_render_source_instance(child_a);
    Mock_Source_Data* db = mel_render_source_instance(child_b);

    MEL_ASSERT(da->last_mgr == &fake_mgr);
    MEL_ASSERT(db->last_mgr == &fake_mgr);

    mel_render_source_destroy(composite);
    mel_render_source_destroy(child_a);
    mel_render_source_destroy(child_b);
}
