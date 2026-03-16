#include "test.harness.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "mugen.air.h"
#include "mugen.cns.h"

#include <stdio.h>
#include <string.h>

#if 0 //these tests disabled for now

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

MEL_TEST(mugen_anim_tick_basic, .tags = "mugen")
{
    const Mel_Alloc* heap = mel_alloc_heap();

    Mugen_Air air;
    mugen_air_load(&air, str8_from_cstr(AIR_SIMPLE), heap);

    Mugen_Char_State st = {0};
    st.air = &air;
    mugen_state_anim_play(&st, &air, 200);

    MEL_ASSERT_EQ(st.anim_frame_index, 0);
    MEL_ASSERT_EQ(st.anim_total_ticks, 15);

    for (u32 i = 0; i < 3; i++) mugen_state_anim_tick(&st);
    MEL_ASSERT_EQ(st.anim_frame_index, 1);

    Mugen_Air_Frame* frame = mugen_state_anim_frame(&st);
    MEL_ASSERT_NOT_NULL(frame);
    MEL_ASSERT_EQ(frame->clsn1_count, 1);
    MEL_ASSERT_EQ(frame->clsn1[0].x1, 18);

    for (u32 i = 0; i < 4; i++) mugen_state_anim_tick(&st);
    MEL_ASSERT_EQ(st.anim_frame_index, 2);

    mugen_air_shutdown(&air, heap);
}

MEL_TEST(mugen_anim_loop, .tags = "mugen")
{
    const Mel_Alloc* heap = mel_alloc_heap();

    Mugen_Air air;
    mugen_air_load(&air, str8_from_cstr(AIR_SIMPLE), heap);

    Mugen_Char_State st = {0};
    st.air = &air;
    mugen_state_anim_play(&st, &air, 20);

    MEL_ASSERT_EQ(st.anim_action->loop_start, 1);

    for (u32 i = 0; i < 1; i++) mugen_state_anim_tick(&st);
    MEL_ASSERT_EQ(st.anim_frame_index, 1);

    for (u32 i = 0; i < 8; i++) mugen_state_anim_tick(&st);
    MEL_ASSERT_EQ(st.anim_frame_index, 2);

    for (u32 i = 0; i < 8; i++) mugen_state_anim_tick(&st);
    MEL_ASSERT_EQ(st.anim_frame_index, 3);

    for (u32 i = 0; i < 8; i++) mugen_state_anim_tick(&st);
    MEL_ASSERT(st.anim_looped);
    MEL_ASSERT_EQ(st.anim_frame_index, 1);

    mugen_air_shutdown(&air, heap);
}

MEL_TEST(mugen_anim_hold_forever, .tags = "mugen")
{
    const Mel_Alloc* heap = mel_alloc_heap();

    const char* air_str =
        "[Begin Action 99]\n"
        "0, 0, 0, 0, 5\n"
        "0, 1, 0, 0, -1\n";

    Mugen_Air air;
    mugen_air_load(&air, str8_from_cstr(air_str), heap);

    Mugen_Char_State st = {0};
    st.air = &air;
    mugen_state_anim_play(&st, &air, 99);

    MEL_ASSERT_EQ(st.anim_total_ticks, 5);

    for (u32 i = 0; i < 5; i++) mugen_state_anim_tick(&st);
    MEL_ASSERT_EQ(st.anim_frame_index, 1);

    for (u32 i = 0; i < 100; i++) mugen_state_anim_tick(&st);
    MEL_ASSERT_EQ(st.anim_frame_index, 1);
    MEL_ASSERT(!st.anim_looped);

    mugen_air_shutdown(&air, heap);
}

MEL_TEST(mugen_air_load_real_file, .tags = "mugen")
{
    const Mel_Alloc* heap = mel_alloc_heap();

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

    Mugen_Char_State st = {0};
    st.air = &air;
    mugen_state_anim_play(&st, &air, 20);
    MEL_ASSERT(st.anim_total_ticks > 0);
    MEL_ASSERT_NOT_NULL(st.anim_action);

    mugen_air_shutdown(&air, heap);
    mel_dealloc(heap, data.data);
}

#endif
