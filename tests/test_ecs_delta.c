#include "../melody/test.harness.h"
#include "../melody/render.ecs.delta.h"
#include "../melody/allocator.heap.h"

#include <flecs.h>

typedef struct { f32 x, y; } TestPosition;
typedef struct { f32 vx, vy; } TestVelocity;

MEL_TEST(ecs_delta_added, .tags = "render")
{
    ecs_world_t* world = ecs_init();
    ECS_COMPONENT(world, TestPosition);

    Mel_ECS_Delta delta;
    mel_ecs_delta_init(&delta, .world = world, .components = { ecs_id(TestPosition) });

    mel_ecs_delta_begin_frame(&delta);

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, TestPosition, {1.0f, 2.0f});

    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e2, TestPosition, {3.0f, 4.0f});

    MEL_ASSERT_EQ(mel_ecs_delta_added_count(&delta), (u32)2);
    MEL_ASSERT_EQ(mel_ecs_delta_removed_count(&delta), (u32)0);

    const ecs_entity_t* added = mel_ecs_delta_added(&delta);
    MEL_ASSERT(added[0] == e1 || added[1] == e1);
    MEL_ASSERT(added[0] == e2 || added[1] == e2);

    mel_ecs_delta_shutdown(&delta);
    ecs_fini(world);
}

MEL_TEST(ecs_delta_removed, .tags = "render")
{
    ecs_world_t* world = ecs_init();
    ECS_COMPONENT(world, TestPosition);

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, TestPosition, {1.0f, 2.0f});

    Mel_ECS_Delta delta;
    mel_ecs_delta_init(&delta, .world = world, .components = { ecs_id(TestPosition) });

    mel_ecs_delta_begin_frame(&delta);

    ecs_remove(world, e1, TestPosition);

    MEL_ASSERT_EQ(mel_ecs_delta_removed_count(&delta), (u32)1);
    MEL_ASSERT_EQ(mel_ecs_delta_removed(&delta)[0], e1);

    mel_ecs_delta_shutdown(&delta);
    ecs_fini(world);
}

MEL_TEST(ecs_delta_modified, .tags = "render")
{
    ecs_world_t* world = ecs_init();
    ECS_COMPONENT(world, TestPosition);

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, TestPosition, {1.0f, 2.0f});

    Mel_ECS_Delta delta;
    mel_ecs_delta_init(&delta, .world = world, .components = { ecs_id(TestPosition) });

    mel_ecs_delta_begin_frame(&delta);

    ecs_set(world, e1, TestPosition, {5.0f, 6.0f});

    MEL_ASSERT_EQ(mel_ecs_delta_added_count(&delta), (u32)0);
    MEL_ASSERT_EQ(mel_ecs_delta_modified_count(&delta), (u32)1);
    MEL_ASSERT_EQ(mel_ecs_delta_modified(&delta)[0], e1);

    mel_ecs_delta_shutdown(&delta);
    ecs_fini(world);
}

MEL_TEST(ecs_delta_add_does_not_duplicate_in_modified, .tags = "render")
{
    ecs_world_t* world = ecs_init();
    ECS_COMPONENT(world, TestPosition);

    Mel_ECS_Delta delta;
    mel_ecs_delta_init(&delta, .world = world, .components = { ecs_id(TestPosition) });

    mel_ecs_delta_begin_frame(&delta);

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, TestPosition, {1.0f, 2.0f});

    MEL_ASSERT_EQ(mel_ecs_delta_added_count(&delta), (u32)1);
    MEL_ASSERT_EQ(mel_ecs_delta_modified_count(&delta), (u32)0);

    mel_ecs_delta_shutdown(&delta);
    ecs_fini(world);
}

MEL_TEST(ecs_delta_begin_frame_clears, .tags = "render")
{
    ecs_world_t* world = ecs_init();
    ECS_COMPONENT(world, TestPosition);

    Mel_ECS_Delta delta;
    mel_ecs_delta_init(&delta, .world = world, .components = { ecs_id(TestPosition) });

    mel_ecs_delta_begin_frame(&delta);

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, TestPosition, {1.0f, 2.0f});
    MEL_ASSERT_EQ(mel_ecs_delta_added_count(&delta), (u32)1);

    mel_ecs_delta_begin_frame(&delta);
    MEL_ASSERT_EQ(mel_ecs_delta_added_count(&delta), (u32)0);
    MEL_ASSERT_EQ(mel_ecs_delta_removed_count(&delta), (u32)0);
    MEL_ASSERT_EQ(mel_ecs_delta_modified_count(&delta), (u32)0);

    mel_ecs_delta_shutdown(&delta);
    ecs_fini(world);
}

MEL_TEST(ecs_delta_full_lifecycle, .tags = "render")
{
    ecs_world_t* world = ecs_init();
    ECS_COMPONENT(world, TestPosition);

    Mel_ECS_Delta delta;
    mel_ecs_delta_init(&delta, .world = world, .components = { ecs_id(TestPosition) });

    mel_ecs_delta_begin_frame(&delta);
    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, TestPosition, {0, 0});
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e2, TestPosition, {1, 1});
    MEL_ASSERT_EQ(mel_ecs_delta_added_count(&delta), (u32)2);

    mel_ecs_delta_begin_frame(&delta);
    ecs_set(world, e1, TestPosition, {2, 2});
    MEL_ASSERT_EQ(mel_ecs_delta_added_count(&delta), (u32)0);
    MEL_ASSERT_EQ(mel_ecs_delta_modified_count(&delta), (u32)1);

    mel_ecs_delta_begin_frame(&delta);
    ecs_remove(world, e2, TestPosition);
    MEL_ASSERT_EQ(mel_ecs_delta_removed_count(&delta), (u32)1);
    MEL_ASSERT_EQ(mel_ecs_delta_removed(&delta)[0], e2);

    mel_ecs_delta_shutdown(&delta);
    ecs_fini(world);
}

MEL_TEST(ecs_delta_add_remove_same_frame, .tags = "render")
{
    ecs_world_t* world = ecs_init();
    ECS_COMPONENT(world, TestPosition);

    Mel_ECS_Delta delta;
    mel_ecs_delta_init(&delta, .world = world, .components = { ecs_id(TestPosition) });

    mel_ecs_delta_begin_frame(&delta);

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, TestPosition, {1.0f, 2.0f});
    MEL_ASSERT_EQ(mel_ecs_delta_added_count(&delta), (u32)1);

    ecs_remove(world, e1, TestPosition);
    MEL_ASSERT_EQ(mel_ecs_delta_removed_count(&delta), (u32)1);
    MEL_ASSERT_EQ(mel_ecs_delta_added_count(&delta), (u32)1);

    mel_ecs_delta_shutdown(&delta);
    ecs_fini(world);
}

MEL_TEST(ecs_delta_multiple_modifications_same_entity, .tags = "render")
{
    ecs_world_t* world = ecs_init();
    ECS_COMPONENT(world, TestPosition);

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, TestPosition, {0, 0});

    Mel_ECS_Delta delta;
    mel_ecs_delta_init(&delta, .world = world, .components = { ecs_id(TestPosition) });

    mel_ecs_delta_begin_frame(&delta);

    ecs_set(world, e1, TestPosition, {1.0f, 1.0f});
    ecs_set(world, e1, TestPosition, {2.0f, 2.0f});
    ecs_set(world, e1, TestPosition, {3.0f, 3.0f});

    MEL_ASSERT_EQ(mel_ecs_delta_modified_count(&delta), (u32)1);

    mel_ecs_delta_shutdown(&delta);
    ecs_fini(world);
}

MEL_TEST(ecs_delta_no_changes_in_frame, .tags = "render")
{
    ecs_world_t* world = ecs_init();
    ECS_COMPONENT(world, TestPosition);

    Mel_ECS_Delta delta;
    mel_ecs_delta_init(&delta, .world = world, .components = { ecs_id(TestPosition) });

    mel_ecs_delta_begin_frame(&delta);

    MEL_ASSERT_EQ(mel_ecs_delta_added_count(&delta), (u32)0);
    MEL_ASSERT_EQ(mel_ecs_delta_removed_count(&delta), (u32)0);
    MEL_ASSERT_EQ(mel_ecs_delta_modified_count(&delta), (u32)0);

    mel_ecs_delta_begin_frame(&delta);

    MEL_ASSERT_EQ(mel_ecs_delta_added_count(&delta), (u32)0);
    MEL_ASSERT_EQ(mel_ecs_delta_removed_count(&delta), (u32)0);
    MEL_ASSERT_EQ(mel_ecs_delta_modified_count(&delta), (u32)0);

    mel_ecs_delta_shutdown(&delta);
    ecs_fini(world);
}

MEL_TEST(ecs_delta_many_entities_in_one_frame, .tags = "render")
{
    ecs_world_t* world = ecs_init();
    ECS_COMPONENT(world, TestPosition);

    Mel_ECS_Delta delta;
    mel_ecs_delta_init(&delta, .world = world, .components = { ecs_id(TestPosition) });

    mel_ecs_delta_begin_frame(&delta);

    for (u32 i = 0; i < 100; i++)
    {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, TestPosition, {(f32)i, (f32)i});
    }

    MEL_ASSERT_EQ(mel_ecs_delta_added_count(&delta), (u32)100);
    MEL_ASSERT_EQ(mel_ecs_delta_removed_count(&delta), (u32)0);
    MEL_ASSERT_EQ(mel_ecs_delta_modified_count(&delta), (u32)0);

    mel_ecs_delta_shutdown(&delta);
    ecs_fini(world);
}

MEL_TEST(ecs_delta_modify_then_remove, .tags = "render")
{
    ecs_world_t* world = ecs_init();
    ECS_COMPONENT(world, TestPosition);

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, TestPosition, {0, 0});

    Mel_ECS_Delta delta;
    mel_ecs_delta_init(&delta, .world = world, .components = { ecs_id(TestPosition) });

    mel_ecs_delta_begin_frame(&delta);

    ecs_set(world, e1, TestPosition, {5.0f, 5.0f});
    MEL_ASSERT_EQ(mel_ecs_delta_modified_count(&delta), (u32)1);

    ecs_remove(world, e1, TestPosition);
    MEL_ASSERT_EQ(mel_ecs_delta_removed_count(&delta), (u32)1);

    mel_ecs_delta_shutdown(&delta);
    ecs_fini(world);
}

MEL_TEST(ecs_delta_multiple_begin_frames, .tags = "render")
{
    ecs_world_t* world = ecs_init();
    ECS_COMPONENT(world, TestPosition);

    Mel_ECS_Delta delta;
    mel_ecs_delta_init(&delta, .world = world, .components = { ecs_id(TestPosition) });

    for (u32 frame = 0; frame < 10; frame++)
    {
        mel_ecs_delta_begin_frame(&delta);
        MEL_ASSERT_EQ(mel_ecs_delta_added_count(&delta), (u32)0);
        MEL_ASSERT_EQ(mel_ecs_delta_removed_count(&delta), (u32)0);
        MEL_ASSERT_EQ(mel_ecs_delta_modified_count(&delta), (u32)0);
    }

    mel_ecs_delta_shutdown(&delta);
    ecs_fini(world);
}
