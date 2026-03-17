#include "../melody/test.harness.h"
#include "../melody/render.source.h"
#include "../melody/render.material.h"
#include "../melody/render.list.h"
#include "../melody/gpu.buffer.h"
#include "../melody/gpu.device.h"
#include "../melody/allocator.heap.h"
#include "../melody/string.str8.h"

MEL_TEST(render_source_render_list_wrappers_are_stable_and_refcounted, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list,
        .name = S8("stable_list"),
        .entry_stride = 16,
        .alloc = mel_alloc_heap());

    Mel_Source_Handle a = mel_source_from_render_list(&list, MEL_SCHEMA_SPRITE);
    Mel_Source_Handle b = mel_source_from_render_list(&list, MEL_SCHEMA_SPRITE);

    MEL_ASSERT(a.handle.index == b.handle.index);
    MEL_ASSERT(a.handle.generation == b.handle.generation);
    MEL_ASSERT_EQ(mel_source_refcount(a), (u32)2);
    MEL_ASSERT(mel_source_render_list(a) == &list);

    mel_source_destroy(a);
    MEL_ASSERT_EQ(mel_source_refcount(b), (u32)1);
    MEL_ASSERT(mel_source_render_list(b) == &list);

    mel_source_destroy(b);
    mel_render_list_shutdown(&list);
}

MEL_TEST(render_source_gpu_buffer_wrappers_are_stable_and_refcounted, .tags = "render")
{
    Mel_Gpu_Buffer buffer = {0};

    Mel_Source_Handle a = mel_source_from_gpu_buffer(&buffer, MEL_SCHEMA_MESHLET_DB);
    Mel_Source_Handle b = mel_source_from_gpu_buffer(&buffer, MEL_SCHEMA_MESHLET_DB);

    MEL_ASSERT(a.handle.index == b.handle.index);
    MEL_ASSERT(a.handle.generation == b.handle.generation);
    MEL_ASSERT_EQ(mel_source_refcount(a), (u32)2);
    MEL_ASSERT(mel_source_gpu_buffer(a) == &buffer);

    mel_source_destroy(a);
    MEL_ASSERT_EQ(mel_source_refcount(b), (u32)1);
    MEL_ASSERT(mel_source_gpu_buffer(b) == &buffer);

    mel_source_destroy(b);
}

MEL_TEST(render_source_material_table_wrappers_are_stable_and_refcounted, .tags = "render")
{
    Mel_Gpu_Device fake_dev = {0};
    Mel_Material_Table table = {
        .dev = &fake_dev,
        .buffer = {0},
    };

    Mel_Source_Handle a = mel_source_from_material_table(&table);
    Mel_Source_Handle b = mel_source_from_material_table(&table);

    MEL_ASSERT(a.handle.index == b.handle.index);
    MEL_ASSERT(a.handle.generation == b.handle.generation);
    MEL_ASSERT_EQ(mel_source_refcount(a), (u32)2);
    MEL_ASSERT(mel_source_gpu_buffer(a) == &table.buffer);
    MEL_ASSERT(mel_source_user(a) == &table);

    mel_source_destroy(a);
    MEL_ASSERT_EQ(mel_source_refcount(b), (u32)1);
    MEL_ASSERT(mel_source_gpu_buffer(b) == &table.buffer);

    mel_source_destroy(b);
}
