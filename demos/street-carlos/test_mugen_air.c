#include "../../melody/test.harness.h"
#include "../../melody/allocator.h"
#include "../../melody/allocator.heap.h"
#include "../../melody/anim.registry.h"
#include "../../melody/anim.clip.h"
#include "../../melody/anim.pose.h"
#include "../../melody/anim.pipeline.h"
#include "../../melody/anim.player.h"
#include "../../melody/collection.slotmap.h"
#include "../../melody/hash.xxh.h"
#include "../../melody/string.str8.h"
#include "mugen.air.h"

#include <stdio.h>

static str8 load_file(const char* path, const Mel_Alloc* alloc)
{
    FILE* f = fopen(path, "rb");
    if (!f) return STR8_EMPTY;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* buf = mel_alloc(alloc, (usize)sz);
    fread(buf, 1, (size_t)sz, f);
    fclose(f);
    return str8_from_parts(buf, (size)sz);
}

static const char* AIR_SIMPLE =
    "[Begin Action 0]\n"
    "Clsn2: 1\n"
    " Clsn2[0] = -15,-94, 15, 2\n"
    "0, 0, 0, 0, 7\n"
    "\n"
    "[Begin Action 20]\n"
    "Clsn2Default: 1\n"
    " Clsn2[0] = -15,-94, 15, 2\n"
    "0, 0, 0, 0, 1\n"
    "loopstart\n"
    "0, 1, 0, 0, 8\n"
    "0, 2, 0, 0, 8\n"
    "0, 3, 0, 0, 8\n"
    "\n"
    "[Begin Action 200]\n"
    "Clsn2Default: 1\n"
    " Clsn2[0] = -15,-94, 15, 2\n"
    "200, 0, 0, 0, 3\n"
    "Clsn1: 1\n"
    " Clsn1[0] = 18,-78, 68,-62\n"
    "200, 1, 0, 0, 4\n"
    "200, 2, 0, 0, 8\n";

MEL_TEST(mugen_air_parse_simple, .tags = "mugen")
{
    const Mel_Alloc* heap = mel_alloc_heap();

    Mugen_Air air;
    bool ok = mugen_air_load(&air, str8_from_cstr(AIR_SIMPLE), heap);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(air.action_count, 3);

    Mugen_Air_Action* act0 = mugen_air_find_action(&air, 0);
    MEL_ASSERT_NOT_NULL(act0);
    MEL_ASSERT_EQ(act0->frame_count, 1);
    MEL_ASSERT_EQ(act0->frames[0].group, 0);
    MEL_ASSERT_EQ(act0->frames[0].number, 0);
    MEL_ASSERT_EQ(act0->frames[0].time, 7);
    MEL_ASSERT_EQ(act0->frames[0].clsn2_count, 1);
    MEL_ASSERT_EQ(act0->frames[0].clsn2[0].x1, -15);
    MEL_ASSERT_EQ(act0->loop_start, MUGEN_AIR_NO_LOOP);

    Mugen_Air_Action* act20 = mugen_air_find_action(&air, 20);
    MEL_ASSERT_NOT_NULL(act20);
    MEL_ASSERT_EQ(act20->frame_count, 4);
    MEL_ASSERT_EQ(act20->loop_start, 1);
    MEL_ASSERT_EQ(act20->frames[1].group, 0);
    MEL_ASSERT_EQ(act20->frames[1].number, 1);
    MEL_ASSERT_EQ(act20->frames[1].time, 8);
    MEL_ASSERT_EQ(act20->frames[2].clsn2_count, 1);

    Mugen_Air_Action* act200 = mugen_air_find_action(&air, 200);
    MEL_ASSERT_NOT_NULL(act200);
    MEL_ASSERT_EQ(act200->frame_count, 3);
    MEL_ASSERT_EQ(act200->frames[0].clsn1_count, 0);
    MEL_ASSERT_EQ(act200->frames[0].clsn2_count, 1);
    MEL_ASSERT_EQ(act200->frames[1].clsn1_count, 1);
    MEL_ASSERT_EQ(act200->frames[1].clsn1[0].x1, 18);
    MEL_ASSERT_EQ(act200->frames[1].clsn1[0].y1, -78);
    MEL_ASSERT_EQ(act200->frames[2].clsn1_count, 0);
    MEL_ASSERT_EQ(act200->frames[2].clsn2_count, 1);

    mugen_air_shutdown(&air, heap);
}

MEL_TEST(mugen_air_compile_basic, .tags = "mugen")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mugen_Air air;
    mugen_air_load(&air, str8_from_cstr(AIR_SIMPLE), heap);

    Mugen_Air_Action* act200 = mugen_air_find_action(&air, 200);
    Mel_Anim_Clip clip = mugen_air_compile(act200, heap);

    MEL_ASSERT_EQ(clip.group_count, 3);
    MEL_ASSERT(clip.is_looping);
    MEL_ASSERT_FLOAT_EQ(clip.loop_start_time, 0.0f, 0.001f);

    f32 expected_dur = (3.0f + 4.0f + 8.0f) / 60.0f;
    MEL_ASSERT_FLOAT_EQ(clip.duration, expected_dur, 0.001f);

    Mel_Local_Pose pose;
    mel_pose_allocate(&pose, &clip, heap);
    u32 cursors[3] = {0};

    mel_anim_sample(&clip, 0.0f, cursors, heap, &pose);
    f32 frame;
    mel__pose_extract(&pose, MEL_ANIM_TYPE_F32, mel_xxh3_64("frame", 5), &frame);
    MEL_ASSERT_FLOAT_EQ(frame, 0.0f, 0.001f);

    f32 hitbox[4];
    mel__pose_extract(&pose, MEL_ANIM_TYPE_VEC4, mel_xxh3_64("hitbox", 6), hitbox);
    MEL_ASSERT_FLOAT_EQ(hitbox[2], 0.0f, 0.001f);

    f32 time_frame1 = 3.0f / 60.0f + 0.001f;
    memset(cursors, 0, sizeof(cursors));
    mel_anim_sample(&clip, time_frame1, cursors, heap, &pose);
    mel__pose_extract(&pose, MEL_ANIM_TYPE_F32, mel_xxh3_64("frame", 5), &frame);
    MEL_ASSERT_FLOAT_EQ(frame, 1.0f, 0.001f);

    mel__pose_extract(&pose, MEL_ANIM_TYPE_VEC4, mel_xxh3_64("hitbox", 6), hitbox);
    MEL_ASSERT_FLOAT_EQ(hitbox[0], 18.0f, 0.001f);
    MEL_ASSERT(hitbox[2] > 0.0f);

    mel_anim_clip_destroy(&clip, heap);
    mugen_air_shutdown(&air, heap);
}

MEL_TEST(mugen_air_compile_looping, .tags = "mugen")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mugen_Air air;
    mugen_air_load(&air, str8_from_cstr(AIR_SIMPLE), heap);

    Mugen_Air_Action* act20 = mugen_air_find_action(&air, 20);
    Mel_Anim_Clip clip = mugen_air_compile(act20, heap);

    MEL_ASSERT(clip.is_looping);
    MEL_ASSERT_FLOAT_EQ(clip.loop_start_time, 1.0f / 60.0f, 0.001f);

    mel_anim_clip_destroy(&clip, heap);
    mugen_air_shutdown(&air, heap);
}

MEL_TEST(mugen_air_load_real_file, .tags = "mugen")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    str8 data = load_file("demos/street-carlos/chars/poi-son/poi-z.air", heap);
    if (data.len == 0)
    {
        data = load_file("chars/poi-son/poi-z.air", heap);
    }
    if (data.len == 0) return;

    Mugen_Air air;
    bool ok = mugen_air_load(&air, data, heap);
    MEL_ASSERT(ok);
    MEL_ASSERT(air.action_count > 10);

    Mugen_Air_Action* idle = mugen_air_find_action(&air, 0);
    MEL_ASSERT_NOT_NULL(idle);
    MEL_ASSERT(idle->frame_count >= 1);

    Mugen_Air_Action* walk = mugen_air_find_action(&air, 20);
    MEL_ASSERT_NOT_NULL(walk);
    MEL_ASSERT(walk->frame_count > 1);
    MEL_ASSERT_NEQ(walk->loop_start, MUGEN_AIR_NO_LOOP);

    Mugen_Air_Action* attack = mugen_air_find_action(&air, 200);
    MEL_ASSERT_NOT_NULL(attack);

    bool has_clsn1 = false;
    for (u32 i = 0; i < attack->frame_count; i++)
    {
        if (attack->frames[i].clsn1_count > 0) has_clsn1 = true;
    }
    MEL_ASSERT(has_clsn1);

    Mel_Anim_Clip clip = mugen_air_compile(walk, heap);
    MEL_ASSERT(clip.is_looping);
    MEL_ASSERT(clip.duration > 0.0f);
    MEL_ASSERT(clip.loop_start_time > 0.0f);

    mel_anim_clip_destroy(&clip, heap);
    mugen_air_shutdown(&air, heap);
    mel_dealloc(heap, data.data);
}
