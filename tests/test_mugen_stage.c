#include "test.harness.h"
#include "mugen.stage.h"
#include "string.str8.h"
#include "allocator.heap.h"

#if 0

MEL_TEST(stage_defaults_when_empty, .tags = "mugen")
{
    Mugen_Stage s;
    mugen_stage_load(&s, (str8){0}, NULL);

    MEL_ASSERT_FLOAT_EQ(s.p1startx, -70.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.p2startx, 70.0f, 0.01f);
    MEL_ASSERT_EQ(s.p1facing, 1);
    MEL_ASSERT_EQ(s.p2facing, -1);
    MEL_ASSERT_FLOAT_EQ(s.left_bound, -150.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.right_bound, 150.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.top_bound, -25.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.bottom_bound, 0.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.zoffset, 200.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.screenleft, 15.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.screenright, 15.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.tension, 50.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.verticalfollow, 0.2f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.floortension, 0.0f, 0.01f);
    MEL_ASSERT_EQ(s.localcoord_w, 320);
    MEL_ASSERT_EQ(s.localcoord_h, 240);
    MEL_ASSERT_EQ(s.bg_count, (u32)0);
    MEL_ASSERT_NULL(s.bgs);
}

MEL_TEST(stage_parse_playerinfo, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[PlayerInfo]\n"
        "p1startx = -100\n"
        "p2startx = 100\n"
        "p1facing = -1\n"
        "p2facing = 1\n"
    );
    mugen_stage_load(&s, data, NULL);

    MEL_ASSERT_FLOAT_EQ(s.p1startx, -100.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.p2startx, 100.0f, 0.01f);
    MEL_ASSERT_EQ(s.p1facing, -1);
    MEL_ASSERT_EQ(s.p2facing, 1);
}

MEL_TEST(stage_parse_bound, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[Bound]\n"
        "screenleft = 20\n"
        "screenright = 25\n"
    );
    mugen_stage_load(&s, data, NULL);

    MEL_ASSERT_FLOAT_EQ(s.screenleft, 20.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.screenright, 25.0f, 0.01f);
}

MEL_TEST(stage_parse_camera, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[Camera]\n"
        "boundleft = -200\n"
        "boundright = 200\n"
    );
    mugen_stage_load(&s, data, NULL);

    MEL_ASSERT_FLOAT_EQ(s.left_bound, -200.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.right_bound, 200.0f, 0.01f);
}

MEL_TEST(stage_parse_stageinfo, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[StageInfo]\n"
        "zoffset = 240\n"
    );
    mugen_stage_load(&s, data, NULL);

    MEL_ASSERT_FLOAT_EQ(s.zoffset, 240.0f, 0.01f);
}

MEL_TEST(stage_ignores_comments, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[PlayerInfo]\n"
        ";p1startx = -999\n"
        "p1startx = -50 ; starting position\n"
    );
    mugen_stage_load(&s, data, NULL);

    MEL_ASSERT_FLOAT_EQ(s.p1startx, -50.0f, 0.01f);
}

MEL_TEST(stage_case_insensitive, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[PLAYERINFO]\n"
        "P1StartX = -80\n"
        "P2STARTX = 80\n"
    );
    mugen_stage_load(&s, data, NULL);

    MEL_ASSERT_FLOAT_EQ(s.p1startx, -80.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.p2startx, 80.0f, 0.01f);
}

MEL_TEST(stage_unknown_sections_ignored, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[Shadow]\n"
        "intensity = 128\n"
        "\n"
        "[PlayerInfo]\n"
        "p1startx = -90\n"
    );
    mugen_stage_load(&s, data, NULL);

    MEL_ASSERT_FLOAT_EQ(s.p1startx, -90.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.p2startx, 70.0f, 0.01f);
}

MEL_TEST(stage_multiple_sections, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[PlayerInfo]\n"
        "p1startx = -60\n"
        "p2startx = 60\n"
        "p1facing = 1\n"
        "p2facing = -1\n"
        "\n"
        "[Bound]\n"
        "screenleft = 10\n"
        "screenright = 10\n"
        "\n"
        "[Camera]\n"
        "boundleft = -180\n"
        "boundright = 180\n"
        "\n"
        "[StageInfo]\n"
        "zoffset = 210\n"
    );
    mugen_stage_load(&s, data, NULL);

    MEL_ASSERT_FLOAT_EQ(s.p1startx, -60.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.p2startx, 60.0f, 0.01f);
    MEL_ASSERT_EQ(s.p1facing, 1);
    MEL_ASSERT_EQ(s.p2facing, -1);
    MEL_ASSERT_FLOAT_EQ(s.screenleft, 10.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.screenright, 10.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.left_bound, -180.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.right_bound, 180.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.zoffset, 210.0f, 0.01f);
}

MEL_TEST(stage_parse_bgdef_spr, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[BGdef]\n"
        "spr = stage0.sff\n"
    );
    mugen_stage_load(&s, data, mel_alloc_heap());

    MEL_ASSERT(str8_equals(s.spr_path, S8("stage0.sff")));

    mugen_stage_shutdown(&s, mel_alloc_heap());
}

MEL_TEST(stage_parse_bg_normal, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[BG 0]\n"
        "type = normal\n"
        "spriteno = 0, 0\n"
        "start = 0, 0\n"
        "delta = 1, 1\n"
        "tile = 1, 0\n"
        "mask = 1\n"
    );
    mugen_stage_load(&s, data, mel_alloc_heap());

    MEL_ASSERT_EQ(s.bg_count, (u32)1);
    MEL_ASSERT_NOT_NULL(s.bgs);
    MEL_ASSERT_EQ(s.bgs[0].type, MUGEN_BG_NORMAL);
    MEL_ASSERT_EQ(s.bgs[0].sprite_group, (u16)0);
    MEL_ASSERT_EQ(s.bgs[0].sprite_number, (u16)0);
    MEL_ASSERT_FLOAT_EQ(s.bgs[0].delta_x, 1.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.bgs[0].delta_y, 1.0f, 0.01f);
    MEL_ASSERT_EQ(s.bgs[0].tile_x, 1);
    MEL_ASSERT_EQ(s.bgs[0].tile_y, 0);
    MEL_ASSERT(s.bgs[0].mask);

    mugen_stage_shutdown(&s, mel_alloc_heap());
}

MEL_TEST(stage_parse_bg_parallax, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[BG Floor]\n"
        "type = parallax\n"
        "spriteno = 10, 0\n"
        "start = 0, 181\n"
        "delta = .78, .75\n"
        "xscale = 1, 1.75\n"
        "yscalestart = 100\n"
        "yscaledelta = 1.2\n"
    );
    mugen_stage_load(&s, data, mel_alloc_heap());

    MEL_ASSERT_EQ(s.bg_count, (u32)1);
    MEL_ASSERT_EQ(s.bgs[0].type, MUGEN_BG_PARALLAX);
    MEL_ASSERT_EQ(s.bgs[0].sprite_group, (u16)10);
    MEL_ASSERT_EQ(s.bgs[0].sprite_number, (u16)0);
    MEL_ASSERT_FLOAT_EQ(s.bgs[0].start_y, 181.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.bgs[0].delta_x, 0.78f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.bgs[0].xscale_top, 1.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.bgs[0].xscale_bot, 1.75f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.bgs[0].yscalestart, 100.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.bgs[0].yscaledelta, 1.2f, 0.01f);

    mugen_stage_shutdown(&s, mel_alloc_heap());
}

MEL_TEST(stage_parse_bg_addalpha, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[BG reflection]\n"
        "type = normal\n"
        "spriteno = 1, 1\n"
        "trans = addalpha\n"
        "alpha = 128, 128\n"
        "start = 0, 239\n"
        "delta = .8, .75\n"
        "mask = 1\n"
        "tile = 1, 0\n"
    );
    mugen_stage_load(&s, data, mel_alloc_heap());

    MEL_ASSERT_EQ(s.bg_count, (u32)1);
    MEL_ASSERT_EQ(s.bgs[0].trans, MUGEN_TRANS_ADDALPHA);
    MEL_ASSERT_EQ(s.bgs[0].alpha_src, (u16)128);
    MEL_ASSERT_EQ(s.bgs[0].alpha_dst, (u16)128);

    mugen_stage_shutdown(&s, mel_alloc_heap());
}

MEL_TEST(stage_parse_multiple_bgs, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[BG sky]\n"
        "type = normal\n"
        "spriteno = 0, 0\n"
        "delta = .5, .5\n"
        "\n"
        "[BG floor]\n"
        "type = parallax\n"
        "spriteno = 10, 0\n"
        "delta = 1, 1\n"
        "\n"
        "[BG wall]\n"
        "type = normal\n"
        "spriteno = 1, 0\n"
        "delta = .8, .75\n"
        "layerno = 1\n"
    );
    mugen_stage_load(&s, data, mel_alloc_heap());

    MEL_ASSERT_EQ(s.bg_count, (u32)3);
    MEL_ASSERT_EQ(s.bgs[0].sprite_group, (u16)0);
    MEL_ASSERT_FLOAT_EQ(s.bgs[0].delta_x, 0.5f, 0.01f);
    MEL_ASSERT_EQ(s.bgs[1].type, MUGEN_BG_PARALLAX);
    MEL_ASSERT_EQ(s.bgs[1].sprite_group, (u16)10);
    MEL_ASSERT_EQ(s.bgs[2].sprite_group, (u16)1);
    MEL_ASSERT_EQ(s.bgs[2].layerno, (u8)1);

    mugen_stage_shutdown(&s, mel_alloc_heap());
}

MEL_TEST(stage_parse_camera_full, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[Camera]\n"
        "startx = 10\n"
        "starty = -5\n"
        "boundleft = -125\n"
        "boundright = 125\n"
        "boundhigh = -25\n"
        "boundlow = 0\n"
        "tension = 60\n"
        "verticalfollow = .8\n"
        "floortension = 20\n"
    );
    mugen_stage_load(&s, data, NULL);

    MEL_ASSERT_FLOAT_EQ(s.camera_startx, 10.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.camera_starty, -5.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.left_bound, -125.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.right_bound, 125.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.top_bound, -25.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.bottom_bound, 0.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.tension, 60.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.verticalfollow, 0.8f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.floortension, 20.0f, 0.01f);
}

MEL_TEST(stage_parse_localcoord, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[StageInfo]\n"
        "localcoord = 640, 480\n"
    );
    mugen_stage_load(&s, data, NULL);

    MEL_ASSERT_EQ(s.localcoord_w, 640);
    MEL_ASSERT_EQ(s.localcoord_h, 480);
}

MEL_TEST(stage_shutdown_frees, .tags = "mugen")
{
    Mugen_Stage s;
    str8 data = S8(
        "[BGdef]\n"
        "spr = test.sff\n"
        "\n"
        "[BG 0]\n"
        "type = normal\n"
        "spriteno = 0, 0\n"
    );
    mugen_stage_load(&s, data, mel_alloc_heap());

    MEL_ASSERT_NOT_NULL(s.bgs);
    MEL_ASSERT_NOT_NULL(s.spr_path.data);

    mugen_stage_shutdown(&s, mel_alloc_heap());

    MEL_ASSERT_NULL(s.bgs);
    MEL_ASSERT_EQ(s.bg_count, (u32)0);
}


#endif
