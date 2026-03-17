#include "../melody/test.harness.h"
#include "../melody/gpu.indirect.h"
#include "../melody/allocator.heap.h"

#include <string.h>

static Mel_Indirect_Draw make_test_indirect(usize stride)
{
    Mel_Indirect_Draw ind = {0};
    ind.stride = stride;
    mel_array_init(&ind.commands, mel_alloc_heap());
    mel_array_reserve(&ind.commands, 64 * stride);
    return ind;
}

static void free_test_indirect(Mel_Indirect_Draw* ind)
{
    mel_array_free(&ind->commands);
    *ind = (Mel_Indirect_Draw){0};
}

MEL_TEST(indirect_empty_count_is_zero, .tags = "gpu")
{
    Mel_Indirect_Draw ind = make_test_indirect(sizeof(Mel_Draw_Indexed_Indirect_Cmd));

    MEL_ASSERT_EQ(mel_indirect_count(&ind), (u32)0);

    free_test_indirect(&ind);
}

MEL_TEST(indirect_append_increments_count, .tags = "gpu")
{
    Mel_Indirect_Draw ind = make_test_indirect(sizeof(Mel_Draw_Indexed_Indirect_Cmd));

    Mel_Draw_Indexed_Indirect_Cmd cmd = {
        .indexCount = 6,
        .instanceCount = 1,
        .firstIndex = 0,
        .vertexOffset = 0,
        .firstInstance = 0,
    };

    mel_indirect_append(&ind, &cmd);
    MEL_ASSERT_EQ(mel_indirect_count(&ind), (u32)1);

    mel_indirect_append(&ind, &cmd);
    MEL_ASSERT_EQ(mel_indirect_count(&ind), (u32)2);

    free_test_indirect(&ind);
}

MEL_TEST(indirect_clear_resets_count, .tags = "gpu")
{
    Mel_Indirect_Draw ind = make_test_indirect(sizeof(Mel_Draw_Indexed_Indirect_Cmd));

    Mel_Draw_Indexed_Indirect_Cmd cmd = { .indexCount = 3, .instanceCount = 1 };

    mel_indirect_append(&ind, &cmd);
    mel_indirect_append(&ind, &cmd);
    mel_indirect_append(&ind, &cmd);
    MEL_ASSERT_EQ(mel_indirect_count(&ind), (u32)3);

    mel_indirect_clear(&ind);
    MEL_ASSERT_EQ(mel_indirect_count(&ind), (u32)0);
    MEL_ASSERT_EQ(ind.commands.count, (usize)0);

    free_test_indirect(&ind);
}

MEL_TEST(indirect_append_preserves_data, .tags = "gpu")
{
    Mel_Indirect_Draw ind = make_test_indirect(sizeof(Mel_Draw_Indexed_Indirect_Cmd));

    Mel_Draw_Indexed_Indirect_Cmd cmd1 = {
        .indexCount = 36,
        .instanceCount = 10,
        .firstIndex = 100,
        .vertexOffset = -5,
        .firstInstance = 42,
    };

    Mel_Draw_Indexed_Indirect_Cmd cmd2 = {
        .indexCount = 6,
        .instanceCount = 1,
        .firstIndex = 0,
        .vertexOffset = 0,
        .firstInstance = 99,
    };

    mel_indirect_append(&ind, &cmd1);
    mel_indirect_append(&ind, &cmd2);

    Mel_Draw_Indexed_Indirect_Cmd read1;
    memcpy(&read1, ind.commands.items, sizeof(Mel_Draw_Indexed_Indirect_Cmd));
    MEL_ASSERT_EQ(read1.indexCount, (u32)36);
    MEL_ASSERT_EQ(read1.instanceCount, (u32)10);
    MEL_ASSERT_EQ(read1.firstIndex, (u32)100);
    MEL_ASSERT_EQ(read1.vertexOffset, (i32)-5);
    MEL_ASSERT_EQ(read1.firstInstance, (u32)42);

    Mel_Draw_Indexed_Indirect_Cmd read2;
    memcpy(&read2, ind.commands.items + sizeof(Mel_Draw_Indexed_Indirect_Cmd), sizeof(Mel_Draw_Indexed_Indirect_Cmd));
    MEL_ASSERT_EQ(read2.indexCount, (u32)6);
    MEL_ASSERT_EQ(read2.firstInstance, (u32)99);

    free_test_indirect(&ind);
}

MEL_TEST(indirect_many_appends_grow_array, .tags = "gpu")
{
    Mel_Indirect_Draw ind = make_test_indirect(sizeof(Mel_Draw_Indexed_Indirect_Cmd));

    for (u32 i = 0; i < 200; i++)
    {
        Mel_Draw_Indexed_Indirect_Cmd cmd = {
            .indexCount = i * 3,
            .instanceCount = 1,
            .firstIndex = i,
        };
        mel_indirect_append(&ind, &cmd);
    }

    MEL_ASSERT_EQ(mel_indirect_count(&ind), (u32)200);
    MEL_ASSERT_EQ(ind.commands.count, (usize)(200 * sizeof(Mel_Draw_Indexed_Indirect_Cmd)));

    Mel_Draw_Indexed_Indirect_Cmd last;
    usize offset = 199 * sizeof(Mel_Draw_Indexed_Indirect_Cmd);
    memcpy(&last, ind.commands.items + offset, sizeof(Mel_Draw_Indexed_Indirect_Cmd));
    MEL_ASSERT_EQ(last.indexCount, (u32)(199 * 3));
    MEL_ASSERT_EQ(last.firstIndex, (u32)199);

    free_test_indirect(&ind);
}

MEL_TEST(indirect_clear_then_reappend, .tags = "gpu")
{
    Mel_Indirect_Draw ind = make_test_indirect(sizeof(Mel_Draw_Indexed_Indirect_Cmd));

    Mel_Draw_Indexed_Indirect_Cmd cmd = { .indexCount = 12, .instanceCount = 1 };
    mel_indirect_append(&ind, &cmd);
    mel_indirect_append(&ind, &cmd);
    MEL_ASSERT_EQ(mel_indirect_count(&ind), (u32)2);

    mel_indirect_clear(&ind);
    MEL_ASSERT_EQ(mel_indirect_count(&ind), (u32)0);

    cmd.indexCount = 24;
    mel_indirect_append(&ind, &cmd);
    MEL_ASSERT_EQ(mel_indirect_count(&ind), (u32)1);

    Mel_Draw_Indexed_Indirect_Cmd read;
    memcpy(&read, ind.commands.items, sizeof(Mel_Draw_Indexed_Indirect_Cmd));
    MEL_ASSERT_EQ(read.indexCount, (u32)24);

    free_test_indirect(&ind);
}

MEL_TEST(indirect_mesh_tasks_cmd, .tags = "gpu")
{
    Mel_Indirect_Draw ind = make_test_indirect(sizeof(Mel_Draw_Mesh_Tasks_Indirect_Cmd));

    Mel_Draw_Mesh_Tasks_Indirect_Cmd cmd = {
        .groupCountX = 4,
        .groupCountY = 2,
        .groupCountZ = 1,
    };

    mel_indirect_append(&ind, &cmd);
    MEL_ASSERT_EQ(mel_indirect_count(&ind), (u32)1);

    Mel_Draw_Mesh_Tasks_Indirect_Cmd read;
    memcpy(&read, ind.commands.items, sizeof(Mel_Draw_Mesh_Tasks_Indirect_Cmd));
    MEL_ASSERT_EQ(read.groupCountX, (u32)4);
    MEL_ASSERT_EQ(read.groupCountY, (u32)2);
    MEL_ASSERT_EQ(read.groupCountZ, (u32)1);

    free_test_indirect(&ind);
}

MEL_TEST(indirect_byte_count_matches_stride, .tags = "gpu")
{
    Mel_Indirect_Draw ind = make_test_indirect(sizeof(Mel_Draw_Indexed_Indirect_Cmd));

    Mel_Draw_Indexed_Indirect_Cmd cmd = { .indexCount = 6, .instanceCount = 1 };
    mel_indirect_append(&ind, &cmd);
    mel_indirect_append(&ind, &cmd);
    mel_indirect_append(&ind, &cmd);

    MEL_ASSERT_EQ(ind.commands.count, (usize)(3 * sizeof(Mel_Draw_Indexed_Indirect_Cmd)));
    MEL_ASSERT_EQ(ind.commands.count, (usize)(3 * ind.stride));

    free_test_indirect(&ind);
}
