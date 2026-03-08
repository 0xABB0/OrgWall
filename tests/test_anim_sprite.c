#include "../melody/test.harness.h"
#include "../melody/anim.registry.h"
#include "../melody/anim.sprite.h"
#include "../melody/anim.clip.h"
#include "../melody/anim.pose.h"
#include "../melody/anim.pipeline.h"
#include "../melody/anim.player.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.slotmap.h"
#include "../melody/hash.xxh.h"

#include <string.h>

MEL_TEST(sprite_anim_def_push_and_compile, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 name = mel_xxh3_64("idle", 4);

    Mel_Sprite_Anim_Def def;
    mel_sprite_anim_def_init(&def, name, name, heap);
    def.is_looping = true;

    mel_sprite_anim_def_push_frame(&def, 0, 0.1f);
    mel_sprite_anim_def_push_frame(&def, 1, 0.1f);
    mel_sprite_anim_def_push_frame(&def, 2, 0.15f);

    MEL_ASSERT_EQ(def.frame_count, 3);

    Mel_Anim_Clip clip = mel_sprite_anim_def_compile(&def, heap);

    MEL_ASSERT(clip.is_looping);
    MEL_ASSERT_EQ(clip.group_count, 1);
    MEL_ASSERT_FLOAT_EQ(clip.duration, 0.35f, 0.001f);
    MEL_ASSERT_EQ(clip.groups[0].track_count, 1);

    u32 kf = clip.groups[0].keyframe_counts[0];
    MEL_ASSERT_EQ(kf, 3);

    MEL_ASSERT_FLOAT_EQ(clip.groups[0].flat_times[0], 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(clip.groups[0].flat_times[1], 0.1f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(clip.groups[0].flat_times[2], 0.2f, 0.001f);

    MEL_ASSERT_FLOAT_EQ(((f32*)clip.groups[0].flat_values)[0], 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(((f32*)clip.groups[0].flat_values)[1], 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(((f32*)clip.groups[0].flat_values)[2], 2.0f, 0.001f);

    Mel_Local_Pose pose;
    mel_pose_allocate(&pose, &clip, heap);
    u32 cursors[1] = {0};
    mel_anim_sample(&clip, 0.05f, cursors, heap, &pose);
    f32 frame;
    mel_pose_extract_float(&pose, name, &frame);
    MEL_ASSERT_FLOAT_EQ(frame, 0.0f, 0.001f);

    cursors[0] = 0;
    mel_anim_sample(&clip, 0.15f, cursors, heap, &pose);
    mel_pose_extract_float(&pose, name, &frame);
    MEL_ASSERT_FLOAT_EQ(frame, 1.0f, 0.001f);

    cursors[0] = 0;
    mel_anim_sample(&clip, 0.25f, cursors, heap, &pose);
    mel_pose_extract_float(&pose, name, &frame);
    MEL_ASSERT_FLOAT_EQ(frame, 2.0f, 0.001f);

    mel_anim_clip_destroy(&clip, heap);
    mel_sprite_anim_def_destroy(&def);
}

MEL_TEST(sprite_anim_def_insert_remove, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();

    Mel_Sprite_Anim_Def def;
    u64 walk_hash = mel_xxh3_64("walk", 4);
    mel_sprite_anim_def_init(&def, walk_hash, walk_hash, heap);

    mel_sprite_anim_def_push_frame(&def, 0, 0.1f);
    mel_sprite_anim_def_push_frame(&def, 1, 0.1f);
    mel_sprite_anim_def_push_frame(&def, 2, 0.1f);

    mel_sprite_anim_def_insert_frame(&def, 1, 5, 0.2f);
    MEL_ASSERT_EQ(def.frame_count, 4);
    MEL_ASSERT_EQ(def.frames[0].frame_index, 0);
    MEL_ASSERT_EQ(def.frames[1].frame_index, 5);
    MEL_ASSERT_EQ(def.frames[2].frame_index, 1);
    MEL_ASSERT_EQ(def.frames[3].frame_index, 2);

    mel_sprite_anim_def_remove_frame(&def, 2);
    MEL_ASSERT_EQ(def.frame_count, 3);
    MEL_ASSERT_EQ(def.frames[0].frame_index, 0);
    MEL_ASSERT_EQ(def.frames[1].frame_index, 5);
    MEL_ASSERT_EQ(def.frames[2].frame_index, 2);

    mel_sprite_anim_def_set_frame(&def, 1, 7, 0.3f);
    MEL_ASSERT_EQ(def.frames[1].frame_index, 7);
    MEL_ASSERT_FLOAT_EQ(def.frames[1].duration, 0.3f, 0.001f);

    mel_sprite_anim_def_destroy(&def);
}

MEL_TEST(sprite_anim_def_events, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 name = mel_xxh3_64("attack", 6);
    u64 hit_hash = mel_xxh3_64("hit", 3);
    u64 sfx_channel = mel_xxh3_64("sfx", 3);

    Mel_Sprite_Anim_Def def;
    mel_sprite_anim_def_init(&def, name, name, heap);

    mel_sprite_anim_def_push_frame(&def, 0, 0.1f);
    mel_sprite_anim_def_push_frame(&def, 1, 0.1f);
    mel_sprite_anim_def_push_frame(&def, 2, 0.15f);

    mel_sprite_anim_def_add_event(&def, 1, hit_hash, sfx_channel);

    Mel_Anim_Clip clip = mel_sprite_anim_def_compile(&def, heap);

    MEL_ASSERT_EQ(clip.event_group_count, 1);
    MEL_ASSERT_EQ(clip.event_groups[0].track_count, 1);
    MEL_ASSERT(clip.event_groups[0].property_ids[0] == sfx_channel);
    MEL_ASSERT_EQ(clip.event_groups[0].keyframe_counts[0], 1);

    MEL_ASSERT_FLOAT_EQ(clip.event_groups[0].flat_times[0], 0.2f, 0.001f);
    MEL_ASSERT(clip.event_groups[0].flat_event_hashes[0] == hit_hash);

    Mel_SlotMap pool;
    mel_slotmap_init(&pool, heap, .item_size = sizeof(Mel_Anim_Clip));
    Mel_Anim_Clip_Handle handle = mel_slotmap_insert(&pool, &clip);

    Mel_Anim_Player player;
    mel_anim_player_init(&player, heap, &pool);
    mel_anim_player_play(&player, handle);
    mel_anim_player_update(&player, 0.25f);

    MEL_ASSERT_EQ(player.event_count, 1);
    MEL_ASSERT(player.pending_events[0].event_hash == hit_hash);
    MEL_ASSERT(player.pending_events[0].property_id == sfx_channel);

    mel_anim_player_destroy(&player);
    mel_slotmap_free(&pool);
    mel_sprite_anim_def_destroy(&def);
}

MEL_TEST(sprite_anim_def_multiple_events_same_frame, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 name = mel_xxh3_64("attack", 6);
    u64 hit_hash = mel_xxh3_64("hit", 3);
    u64 sfx_hash = mel_xxh3_64("slash_sfx", 9);
    u64 combat_channel = mel_xxh3_64("combat", 6);

    Mel_Sprite_Anim_Def def;
    mel_sprite_anim_def_init(&def, name, name, heap);

    mel_sprite_anim_def_push_frame(&def, 0, 0.1f);
    mel_sprite_anim_def_push_frame(&def, 1, 0.1f);
    mel_sprite_anim_def_push_frame(&def, 2, 0.1f);

    mel_sprite_anim_def_add_event(&def, 1, hit_hash, combat_channel);
    mel_sprite_anim_def_add_event(&def, 1, sfx_hash, combat_channel);

    MEL_ASSERT_EQ(def.event_count, 2);

    Mel_Anim_Clip clip = mel_sprite_anim_def_compile(&def, heap);

    MEL_ASSERT_EQ(clip.event_group_count, 1);
    MEL_ASSERT_EQ(clip.event_groups[0].keyframe_counts[0], 2);

    MEL_ASSERT_FLOAT_EQ(clip.event_groups[0].flat_times[0], 0.2f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(clip.event_groups[0].flat_times[1], 0.2f, 0.001f);

    mel_anim_clip_destroy(&clip, heap);
    mel_sprite_anim_def_destroy(&def);
}

MEL_TEST(sprite_anim_def_events_multiple_channels, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 name = mel_xxh3_64("attack", 6);
    u64 hit_hash = mel_xxh3_64("hit", 3);
    u64 sfx_hash = mel_xxh3_64("whoosh", 6);
    u64 combat_channel = mel_xxh3_64("combat", 6);
    u64 sfx_channel = mel_xxh3_64("sfx", 3);

    Mel_Sprite_Anim_Def def;
    mel_sprite_anim_def_init(&def, name, name, heap);

    mel_sprite_anim_def_push_frame(&def, 0, 0.1f);
    mel_sprite_anim_def_push_frame(&def, 1, 0.1f);
    mel_sprite_anim_def_push_frame(&def, 2, 0.1f);

    mel_sprite_anim_def_add_event(&def, 1, hit_hash, combat_channel);
    mel_sprite_anim_def_add_event(&def, 0, sfx_hash, sfx_channel);

    Mel_Anim_Clip clip = mel_sprite_anim_def_compile(&def, heap);

    MEL_ASSERT_EQ(clip.event_group_count, 1);
    MEL_ASSERT_EQ(clip.event_groups[0].track_count, 2);

    mel_anim_clip_destroy(&clip, heap);
    mel_sprite_anim_def_destroy(&def);
}

MEL_TEST(sprite_anim_def_no_events_compiles_clean, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 name = mel_xxh3_64("idle", 4);

    Mel_Sprite_Anim_Def def;
    mel_sprite_anim_def_init(&def, name, name, heap);

    mel_sprite_anim_def_push_frame(&def, 0, 0.1f);
    mel_sprite_anim_def_push_frame(&def, 1, 0.1f);

    Mel_Anim_Clip clip = mel_sprite_anim_def_compile(&def, heap);

    MEL_ASSERT_EQ(clip.event_group_count, 0);
    MEL_ASSERT_NULL(clip.event_groups);
    MEL_ASSERT_FLOAT_EQ(clip.duration, 0.2f, 0.001f);

    mel_anim_clip_destroy(&clip, heap);
    mel_sprite_anim_def_destroy(&def);
}

MEL_TEST(sprite_anim_def_compile_non_looping, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 name = mel_xxh3_64("death", 5);

    Mel_Sprite_Anim_Def def;
    mel_sprite_anim_def_init(&def, name, name, heap);
    def.is_looping = false;

    mel_sprite_anim_def_push_frame(&def, 0, 0.2f);
    mel_sprite_anim_def_push_frame(&def, 1, 0.3f);

    Mel_Anim_Clip clip = mel_sprite_anim_def_compile(&def, heap);

    MEL_ASSERT(!clip.is_looping);
    MEL_ASSERT_FLOAT_EQ(clip.duration, 0.5f, 0.001f);

    mel_anim_clip_destroy(&clip, heap);
    mel_sprite_anim_def_destroy(&def);
}

MEL_TEST(sprite_anim_compile_uses_property_hash_not_name, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 name = mel_xxh3_64("idle", 4);
    u64 prop = mel_xxh3_64("frame", 5);

    Mel_Sprite_Anim_Def def;
    mel_sprite_anim_def_init(&def, name, prop, heap);

    mel_sprite_anim_def_push_frame(&def, 0, 0.1f);
    mel_sprite_anim_def_push_frame(&def, 1, 0.1f);

    Mel_Anim_Clip clip = mel_sprite_anim_def_compile(&def, heap);

    MEL_ASSERT(clip.name_hash == name);
    MEL_ASSERT(clip.groups[0].property_ids[0] == prop);
    MEL_ASSERT(clip.groups[0].property_ids[0] != name);

    mel_anim_clip_destroy(&clip, heap);
    mel_sprite_anim_def_destroy(&def);
}

MEL_TEST(sprite_anim_crossfade_shared_property, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 frame_prop = mel_xxh3_64("frame", 5);

    Mel_Sprite_Anim_Def idle_def;
    mel_sprite_anim_def_init(&idle_def, mel_xxh3_64("idle", 4), frame_prop, heap);
    idle_def.is_looping = true;
    mel_sprite_anim_def_push_frame(&idle_def, 0, 0.1f);
    mel_sprite_anim_def_push_frame(&idle_def, 1, 0.1f);

    Mel_Sprite_Anim_Def walk_def;
    mel_sprite_anim_def_init(&walk_def, mel_xxh3_64("walk", 4), frame_prop, heap);
    walk_def.is_looping = true;
    mel_sprite_anim_def_push_frame(&walk_def, 4, 0.1f);
    mel_sprite_anim_def_push_frame(&walk_def, 5, 0.1f);

    Mel_Anim_Clip idle_clip = mel_sprite_anim_def_compile(&idle_def, heap);
    Mel_Anim_Clip walk_clip = mel_sprite_anim_def_compile(&walk_def, heap);

    Mel_SlotMap pool;
    mel_slotmap_init(&pool, heap, .item_size = sizeof(Mel_Anim_Clip));

    Mel_Anim_Clip_Handle idle_h = mel_slotmap_insert(&pool, &idle_clip);
    Mel_Anim_Clip_Handle walk_h = mel_slotmap_insert(&pool, &walk_clip);

    Mel_Anim_Player player;
    mel_anim_player_init(&player, heap, &pool);
    mel_anim_player_play(&player, idle_h);
    mel_anim_player_update(&player, 0.0f);

    mel_anim_player_play(&player, walk_h, .crossfade = 0.5f);
    mel_anim_player_update(&player, 0.25f);

    f32 frame;
    mel_anim_player_get_float(&player, frame_prop, heap, &frame);
    MEL_ASSERT_GE(frame, 0.0f);
    MEL_ASSERT_LE(frame, 5.0f);

    mel_anim_player_update(&player, 0.5f);
    MEL_ASSERT_EQ(player.chain_count, 1);
    mel_anim_player_get_float(&player, frame_prop, heap, &frame);
    MEL_ASSERT_GE(frame, 4.0f);
    MEL_ASSERT_LE(frame, 5.0f);

    mel_anim_player_destroy(&player);
    mel_slotmap_free(&pool);
    mel_sprite_anim_def_destroy(&idle_def);
    mel_sprite_anim_def_destroy(&walk_def);
}

MEL_TEST(sprite_anim_remove_event, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();

    u64 name = mel_xxh3_64("test", 4);
    u64 hit = mel_xxh3_64("hit", 3);
    u64 sfx = mel_xxh3_64("sfx", 3);
    u64 channel = mel_xxh3_64("ch", 2);

    Mel_Sprite_Anim_Def def;
    mel_sprite_anim_def_init(&def, name, name, heap);

    mel_sprite_anim_def_push_frame(&def, 0, 0.1f);
    mel_sprite_anim_def_push_frame(&def, 1, 0.1f);

    mel_sprite_anim_def_add_event(&def, 0, hit, channel);
    mel_sprite_anim_def_add_event(&def, 1, sfx, channel);
    MEL_ASSERT_EQ(def.event_count, 2);

    mel_sprite_anim_def_remove_event(&def, 0);
    MEL_ASSERT_EQ(def.event_count, 1);
    MEL_ASSERT(def.events[0].event_hash == sfx);

    mel_sprite_anim_def_destroy(&def);
}
