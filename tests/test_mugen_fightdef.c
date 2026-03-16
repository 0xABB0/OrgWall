#include "test.harness.h"
#include "mugen.fightdef.h"
#include "string.str8.h"
#include "allocator.heap.h"

#if 0

MEL_TEST(fightdef_defaults_when_empty, .tags = "mugen")
{
    Mugen_Fightdef fd;
    mugen_fightdef_load(&fd, (str8){0}, NULL);

    MEL_ASSERT_FLOAT_EQ(fd.fightfx_scale, 1.0f, 0.01f);
    MEL_ASSERT_EQ(fd.lifebar.p1.bg1_facing, (i8)1);
    MEL_ASSERT_EQ(fd.lifebar.p1.mid_facing, (i8)1);
    MEL_ASSERT_EQ(fd.lifebar.p1.front_facing, (i8)1);
    MEL_ASSERT_EQ(fd.lifebar.p2.bg1_facing, (i8)1);
    MEL_ASSERT_FLOAT_EQ(fd.face.p1.face_scale_x, 1.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(fd.face.p1.face_scale_y, 1.0f, 0.01f);
    MEL_ASSERT_EQ(fd.face.p1.face_facing, (i8)1);
    MEL_ASSERT_EQ(fd.time.framespercount, 60);
    MEL_ASSERT_EQ(fd.round.match_wins, 2);
    MEL_ASSERT_EQ(fd.round.match_maxdrawgames, 1);
    MEL_ASSERT_EQ(fd.round.start_waittime, 30);
    MEL_ASSERT_EQ(fd.round.ctrl_time, 30);
    MEL_ASSERT_EQ(fd.round.slow_time, 60);
    MEL_ASSERT_EQ(fd.round.over_waittime, 45);
    MEL_ASSERT_EQ(fd.round.over_hittime, 10);
    MEL_ASSERT_EQ(fd.round.over_wintime, 45);
    MEL_ASSERT_EQ(fd.round.over_time, 210);
    MEL_ASSERT_EQ(fd.round.win_time, 60);
    MEL_ASSERT_EQ(fd.round.win_displaytime, 540);
    MEL_ASSERT_EQ(fd.round.dko_displaytime, 60);
    MEL_ASSERT_EQ(fd.round.to_displaytime, 60);
    MEL_ASSERT_EQ(fd.round.draw_displaytime, 540);
    MEL_ASSERT_EQ(fd.combo.team1.displaytime, 90);
    MEL_ASSERT_EQ(fd.combo.team2.displaytime, 90);
    MEL_ASSERT_EQ(fd.winicon.useiconupto, 4);
}

MEL_TEST(fightdef_parse_files, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[Files]\n"
        "sff = fight.sff\n"
        "snd = fight.snd\n"
        "font1 = jg.fnt\n"
        "font2 = num1.fnt\n"
        "font3 = name1.fnt\n"
        "fightfx.sff = fightfx.sff\n"
        "fightfx.air = fightfx.air\n"
        "common.snd = common.snd\n"
    );
    mugen_fightdef_load(&fd, data, mel_alloc_heap());

    MEL_ASSERT(str8_equals(fd.files.sff, S8("fight.sff")));
    MEL_ASSERT(str8_equals(fd.files.snd, S8("fight.snd")));
    MEL_ASSERT(str8_equals(fd.files.font[0], S8("jg.fnt")));
    MEL_ASSERT(str8_equals(fd.files.font[1], S8("num1.fnt")));
    MEL_ASSERT(str8_equals(fd.files.font[2], S8("name1.fnt")));
    MEL_ASSERT(str8_equals(fd.files.fightfx_sff, S8("fightfx.sff")));
    MEL_ASSERT(str8_equals(fd.files.fightfx_air, S8("fightfx.air")));
    MEL_ASSERT(str8_equals(fd.files.common_snd, S8("common.snd")));

    mugen_fightdef_shutdown(&fd, mel_alloc_heap());
}

MEL_TEST(fightdef_parse_lifebar, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[Lifebar]\n"
        "p1.pos = 140,12\n"
        "p1.bg0.anim = 10\n"
        "p1.bg1.spr = 11,0\n"
        "p1.mid.spr = 12,0\n"
        "p1.front.spr = 13,0\n"
        "p1.range.x = 0,-127\n"
        "p2.pos = 178,12\n"
        "p2.bg0.anim = 10\n"
        "p2.bg0.facing = -1\n"
        "p2.bg1.spr = 11,0\n"
        "p2.bg1.facing = -1\n"
        "p2.mid.spr = 12,0\n"
        "p2.mid.facing = -1\n"
        "p2.front.spr = 13,0\n"
        "p2.front.facing = -1\n"
        "p2.range.x = 0,127\n"
    );
    mugen_fightdef_load(&fd, data, NULL);

    MEL_ASSERT_EQ(fd.lifebar.p1.pos_x, 140);
    MEL_ASSERT_EQ(fd.lifebar.p1.pos_y, 12);
    MEL_ASSERT_EQ(fd.lifebar.p1.bg0_anim, (u32)10);
    MEL_ASSERT_EQ(fd.lifebar.p1.bg1_spr.group, (u16)11);
    MEL_ASSERT_EQ(fd.lifebar.p1.bg1_spr.number, (u16)0);
    MEL_ASSERT_EQ(fd.lifebar.p1.mid_spr.group, (u16)12);
    MEL_ASSERT_EQ(fd.lifebar.p1.front_spr.group, (u16)13);
    MEL_ASSERT_EQ(fd.lifebar.p1.range_x_start, 0);
    MEL_ASSERT_EQ(fd.lifebar.p1.range_x_end, -127);
    MEL_ASSERT_EQ(fd.lifebar.p1.bg1_facing, (i8)1);
    MEL_ASSERT_EQ(fd.lifebar.p1.mid_facing, (i8)1);
    MEL_ASSERT_EQ(fd.lifebar.p1.front_facing, (i8)1);

    MEL_ASSERT_EQ(fd.lifebar.p2.pos_x, 178);
    MEL_ASSERT_EQ(fd.lifebar.p2.pos_y, 12);
    MEL_ASSERT_EQ(fd.lifebar.p2.bg1_facing, (i8)-1);
    MEL_ASSERT_EQ(fd.lifebar.p2.mid_facing, (i8)-1);
    MEL_ASSERT_EQ(fd.lifebar.p2.front_facing, (i8)-1);
    MEL_ASSERT_EQ(fd.lifebar.p2.range_x_start, 0);
    MEL_ASSERT_EQ(fd.lifebar.p2.range_x_end, 127);
}

MEL_TEST(fightdef_parse_powerbar, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[Powerbar]\n"
        "p1.pos = 140,22\n"
        "p1.bg0.anim = 40\n"
        "p1.bg1.spr = 41,0\n"
        "p1.front.spr = 43,0\n"
        "p1.range.x = 0,-107\n"
        "p1.counter.offset = -108,6\n"
        "p1.counter.font = 1,0, 0\n"
        "p2.pos = 178,22\n"
        "p2.counter.offset = 109,6\n"
        "p2.counter.font = 1,0, 0\n"
        "level1.snd = 21,0\n"
    );
    mugen_fightdef_load(&fd, data, NULL);

    MEL_ASSERT_EQ(fd.powerbar.p1.pos_x, 140);
    MEL_ASSERT_EQ(fd.powerbar.p1.pos_y, 22);
    MEL_ASSERT_EQ(fd.powerbar.p1.bg0_anim, (u32)40);
    MEL_ASSERT_EQ(fd.powerbar.p1.bg1_spr.group, (u16)41);
    MEL_ASSERT_EQ(fd.powerbar.p1.front_spr.group, (u16)43);
    MEL_ASSERT_EQ(fd.powerbar.p1.range_x_start, 0);
    MEL_ASSERT_EQ(fd.powerbar.p1.range_x_end, -107);
    MEL_ASSERT_EQ(fd.powerbar.p1_counter_offset_x, -108);
    MEL_ASSERT_EQ(fd.powerbar.p1_counter_offset_y, 6);
    MEL_ASSERT_EQ(fd.powerbar.p1_counter_font.fontno, (u8)1);
    MEL_ASSERT_EQ(fd.powerbar.p1_counter_font.bank, (u8)0);
    MEL_ASSERT_EQ(fd.powerbar.p1_counter_font.alignment, (i8)0);

    MEL_ASSERT_EQ(fd.powerbar.p2.pos_x, 178);
    MEL_ASSERT_EQ(fd.powerbar.p2_counter_offset_x, 109);
    MEL_ASSERT_EQ(fd.powerbar.level1_snd.group, (u16)21);
    MEL_ASSERT_EQ(fd.powerbar.level1_snd.number, (u16)0);
}

MEL_TEST(fightdef_parse_face, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[Face]\n"
        "p1.pos = 2,12\n"
        "p1.bg.spr = 50,0\n"
        "p1.face.spr = 9000,0\n"
        "p1.face.facing = 1\n"
        "p1.face.offset = 0,10\n"
        "p2.pos = 316,12\n"
        "p2.bg.spr = 50,0\n"
        "p2.bg.facing = -1\n"
        "p2.face.spr = 9000,0\n"
        "p2.face.facing = -1\n"
        "p2.face.offset = 0,10\n"
    );
    mugen_fightdef_load(&fd, data, NULL);

    MEL_ASSERT_EQ(fd.face.p1.pos_x, 2);
    MEL_ASSERT_EQ(fd.face.p1.pos_y, 12);
    MEL_ASSERT_EQ(fd.face.p1.bg_spr.group, (u16)50);
    MEL_ASSERT_EQ(fd.face.p1.face_spr.group, (u16)9000);
    MEL_ASSERT_EQ(fd.face.p1.face_facing, (i8)1);
    MEL_ASSERT_EQ(fd.face.p1.face_offset_x, 0);
    MEL_ASSERT_EQ(fd.face.p1.face_offset_y, 10);
    MEL_ASSERT_EQ(fd.face.p1.bg_facing, (i8)1);

    MEL_ASSERT_EQ(fd.face.p2.pos_x, 316);
    MEL_ASSERT_EQ(fd.face.p2.bg_facing, (i8)-1);
    MEL_ASSERT_EQ(fd.face.p2.face_facing, (i8)-1);
    MEL_ASSERT_EQ(fd.face.p2.face_offset_y, 10);
}

MEL_TEST(fightdef_parse_name, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[Name]\n"
        "p1.pos = 14,10\n"
        "p1.name.font = 3,0, 1\n"
        "p2.pos = 305,10\n"
        "p2.name.font = 3,0, -1\n"
    );
    mugen_fightdef_load(&fd, data, NULL);

    MEL_ASSERT_EQ(fd.name.p1.pos_x, 14);
    MEL_ASSERT_EQ(fd.name.p1.pos_y, 10);
    MEL_ASSERT_EQ(fd.name.p1.name_font.fontno, (u8)3);
    MEL_ASSERT_EQ(fd.name.p1.name_font.bank, (u8)0);
    MEL_ASSERT_EQ(fd.name.p1.name_font.alignment, (i8)1);

    MEL_ASSERT_EQ(fd.name.p2.pos_x, 305);
    MEL_ASSERT_EQ(fd.name.p2.name_font.alignment, (i8)-1);
}

MEL_TEST(fightdef_parse_time, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[Time]\n"
        "pos = 160,23\n"
        "counter.offset = 0,0\n"
        "counter.font = 2,0, 0\n"
        "framespercount = 30\n"
    );
    mugen_fightdef_load(&fd, data, NULL);

    MEL_ASSERT_EQ(fd.time.pos_x, 160);
    MEL_ASSERT_EQ(fd.time.pos_y, 23);
    MEL_ASSERT_EQ(fd.time.counter_offset_x, 0);
    MEL_ASSERT_EQ(fd.time.counter_offset_y, 0);
    MEL_ASSERT_EQ(fd.time.counter_font.fontno, (u8)2);
    MEL_ASSERT_EQ(fd.time.counter_font.alignment, (i8)0);
    MEL_ASSERT_EQ(fd.time.framespercount, 30);
}

MEL_TEST(fightdef_parse_combo, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[Combo]\n"
        "team1.pos = 10,80\n"
        "team1.start.x = -40\n"
        "team1.counter.font = 2,4,1\n"
        "team1.counter.shake = 1\n"
        "team1.text.text = Rush!\n"
        "team1.text.font = 1,0,1\n"
        "team1.text.offset = 3,0\n"
        "team1.displaytime = 90\n"
        "team2.pos = 309,80\n"
        "team2.start.x = 359\n"
    );
    mugen_fightdef_load(&fd, data, mel_alloc_heap());

    MEL_ASSERT_EQ(fd.combo.team1.pos_x, 10);
    MEL_ASSERT_EQ(fd.combo.team1.pos_y, 80);
    MEL_ASSERT_EQ(fd.combo.team1.start_x, -40);
    MEL_ASSERT_EQ(fd.combo.team1.counter_font.fontno, (u8)2);
    MEL_ASSERT_EQ(fd.combo.team1.counter_font.bank, (u8)4);
    MEL_ASSERT_EQ(fd.combo.team1.counter_font.alignment, (i8)1);
    MEL_ASSERT(fd.combo.team1.counter_shake);
    MEL_ASSERT(str8_equals(fd.combo.team1.text_text, S8("Rush!")));
    MEL_ASSERT_EQ(fd.combo.team1.text_font.fontno, (u8)1);
    MEL_ASSERT_EQ(fd.combo.team1.text_offset_x, 3);
    MEL_ASSERT_EQ(fd.combo.team1.text_offset_y, 0);
    MEL_ASSERT_EQ(fd.combo.team1.displaytime, 90);

    MEL_ASSERT_EQ(fd.combo.team2.pos_x, 309);
    MEL_ASSERT_EQ(fd.combo.team2.start_x, 359);

    mugen_fightdef_shutdown(&fd, mel_alloc_heap());
}

MEL_TEST(fightdef_parse_round_basics, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[Round]\n"
        "match.wins = 3\n"
        "match.maxdrawgames = -1\n"
        "start.waittime = 40\n"
        "ctrl.time = 25\n"
        "slow.time = 45\n"
        "over.waittime = 50\n"
        "over.hittime = 15\n"
        "over.wintime = 40\n"
        "over.time = 180\n"
    );
    mugen_fightdef_load(&fd, data, NULL);

    MEL_ASSERT_EQ(fd.round.match_wins, 3);
    MEL_ASSERT_EQ(fd.round.match_maxdrawgames, -1);
    MEL_ASSERT_EQ(fd.round.start_waittime, 40);
    MEL_ASSERT_EQ(fd.round.ctrl_time, 25);
    MEL_ASSERT_EQ(fd.round.slow_time, 45);
    MEL_ASSERT_EQ(fd.round.over_waittime, 50);
    MEL_ASSERT_EQ(fd.round.over_hittime, 15);
    MEL_ASSERT_EQ(fd.round.over_wintime, 40);
    MEL_ASSERT_EQ(fd.round.over_time, 180);
}

MEL_TEST(fightdef_parse_round_display, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[Round]\n"
        "fight.offset = 160, 110\n"
        "fight.anim = 80\n"
        "fight.snd = 1,0\n"
        "KO.offset = 160, 70\n"
        "KO.anim = 200\n"
        "KO.snd = 2,0\n"
        "DKO.offset = 160, 70\n"
        "DKO.font = 1,5\n"
        "DKO.text = Double K.O.\n"
        "TO.offset = 160, 70\n"
        "TO.font = 1,5\n"
        "TO.text = Time Over\n"
        "win.offset = 160, 70\n"
        "win.font = 1,0\n"
        "win.text = %s Wins\n"
        "draw.offset = 160, 70\n"
        "draw.font = 1,0\n"
        "draw.text = Draw Game\n"
    );
    mugen_fightdef_load(&fd, data, mel_alloc_heap());

    MEL_ASSERT_EQ(fd.round.fight_offset_x, 160);
    MEL_ASSERT_EQ(fd.round.fight_offset_y, 110);
    MEL_ASSERT_EQ(fd.round.fight_anim, (u32)80);
    MEL_ASSERT_EQ(fd.round.fight_snd.group, (u16)1);
    MEL_ASSERT_EQ(fd.round.fight_snd.number, (u16)0);

    MEL_ASSERT_EQ(fd.round.ko_offset_x, 160);
    MEL_ASSERT_EQ(fd.round.ko_offset_y, 70);
    MEL_ASSERT_EQ(fd.round.ko_anim, (u32)200);
    MEL_ASSERT_EQ(fd.round.ko_snd.group, (u16)2);

    MEL_ASSERT_EQ(fd.round.dko_offset_x, 160);
    MEL_ASSERT_EQ(fd.round.dko_font.fontno, (u8)1);
    MEL_ASSERT_EQ(fd.round.dko_font.bank, (u8)5);
    MEL_ASSERT(str8_equals(fd.round.dko_text, S8("Double K.O.")));

    MEL_ASSERT(str8_equals(fd.round.to_text, S8("Time Over")));
    MEL_ASSERT(str8_equals(fd.round.win_text, S8("%s Wins")));
    MEL_ASSERT(str8_equals(fd.round.draw_text, S8("Draw Game")));

    mugen_fightdef_shutdown(&fd, mel_alloc_heap());
}

MEL_TEST(fightdef_parse_winicon, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[WinIcon]\n"
        "p1.pos = 34,43\n"
        "p2.pos = 284,43\n"
        "p1.iconoffset = 12,0\n"
        "p2.iconoffset = -12,0\n"
        "p1.n.spr = 100,0\n"
        "p2.n.spr = 100,0\n"
        "p1.s.spr = 101,0\n"
        "p1.h.spr = 102,0\n"
        "p1.throw.spr = 103,0\n"
        "p1.c.spr = 104,0\n"
        "p1.t.spr = 105,0\n"
        "p1.suicide.spr = 106,0\n"
        "p1.teammate.spr = 107,0\n"
        "p1.perfect.spr = 110,0\n"
        "p1.counter.font = 2,1\n"
        "useiconupto = 8\n"
    );
    mugen_fightdef_load(&fd, data, NULL);

    MEL_ASSERT_EQ(fd.winicon.p1.pos_x, 34);
    MEL_ASSERT_EQ(fd.winicon.p1.pos_y, 43);
    MEL_ASSERT_EQ(fd.winicon.p2.pos_x, 284);
    MEL_ASSERT_EQ(fd.winicon.p1.iconoffset_x, 12);
    MEL_ASSERT_EQ(fd.winicon.p2.iconoffset_x, -12);
    MEL_ASSERT_EQ(fd.winicon.p1.n_spr.group, (u16)100);
    MEL_ASSERT_EQ(fd.winicon.p1.s_spr.group, (u16)101);
    MEL_ASSERT_EQ(fd.winicon.p1.h_spr.group, (u16)102);
    MEL_ASSERT_EQ(fd.winicon.p1.throw_spr.group, (u16)103);
    MEL_ASSERT_EQ(fd.winicon.p1.c_spr.group, (u16)104);
    MEL_ASSERT_EQ(fd.winicon.p1.t_spr.group, (u16)105);
    MEL_ASSERT_EQ(fd.winicon.p1.suicide_spr.group, (u16)106);
    MEL_ASSERT_EQ(fd.winicon.p1.teammate_spr.group, (u16)107);
    MEL_ASSERT_EQ(fd.winicon.p1.perfect_spr.group, (u16)110);
    MEL_ASSERT_EQ(fd.winicon.p1.counter_font.fontno, (u8)2);
    MEL_ASSERT_EQ(fd.winicon.p1.counter_font.bank, (u8)1);
    MEL_ASSERT_EQ(fd.winicon.useiconupto, 8);
}

MEL_TEST(fightdef_parse_case_insensitive, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[LIFEBAR]\n"
        "P1.Pos = 100,20\n"
        "P1.BG0.Anim = 5\n"
    );
    mugen_fightdef_load(&fd, data, NULL);

    MEL_ASSERT_EQ(fd.lifebar.p1.pos_x, 100);
    MEL_ASSERT_EQ(fd.lifebar.p1.pos_y, 20);
    MEL_ASSERT_EQ(fd.lifebar.p1.bg0_anim, (u32)5);
}

MEL_TEST(fightdef_parse_comments_ignored, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[Time]\n"
        ";framespercount = 999\n"
        "framespercount = 30 ; ticks per second\n"
    );
    mugen_fightdef_load(&fd, data, NULL);

    MEL_ASSERT_EQ(fd.time.framespercount, 30);
}

MEL_TEST(fightdef_shutdown_frees, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[Files]\n"
        "sff = fight.sff\n"
        "snd = fight.snd\n"
        "font1 = jg.fnt\n"
        "[Combo]\n"
        "team1.text.text = Rush!\n"
        "[Round]\n"
        "win.text = %s Wins\n"
        "draw.text = Draw Game\n"
    );
    mugen_fightdef_load(&fd, data, mel_alloc_heap());

    MEL_ASSERT_NOT_NULL(fd.files.sff.data);
    MEL_ASSERT_NOT_NULL(fd.combo.team1.text_text.data);
    MEL_ASSERT_NOT_NULL(fd.round.win_text.data);

    mugen_fightdef_shutdown(&fd, mel_alloc_heap());

    MEL_ASSERT_NULL(fd.files.sff.data);
    MEL_ASSERT_NULL(fd.combo.team1.text_text.data);
    MEL_ASSERT_NULL(fd.round.win_text.data);
}

MEL_TEST(fightdef_skips_simul_turns_sections, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[Simul Lifebar]\n"
        "p1.pos = 999,999\n"
        "[Turns Lifebar]\n"
        "p1.pos = 888,888\n"
        "[Lifebar]\n"
        "p1.pos = 140,12\n"
    );
    mugen_fightdef_load(&fd, data, NULL);

    MEL_ASSERT_EQ(fd.lifebar.p1.pos_x, 140);
    MEL_ASSERT_EQ(fd.lifebar.p1.pos_y, 12);
}

MEL_TEST(fightdef_parse_fightfx_scale, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[FightFx]\n"
        "scale = 2.5\n"
    );
    mugen_fightdef_load(&fd, data, NULL);

    MEL_ASSERT_FLOAT_EQ(fd.fightfx_scale, 2.5f, 0.01f);
}

MEL_TEST(fightdef_parse_face_scale, .tags = "mugen")
{
    Mugen_Fightdef fd;
    str8 data = S8(
        "[Face]\n"
        "p1.face.scale = 0.6,0.6\n"
    );
    mugen_fightdef_load(&fd, data, NULL);

    MEL_ASSERT_FLOAT_EQ(fd.face.p1.face_scale_x, 0.6f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(fd.face.p1.face_scale_y, 0.6f, 0.01f);
}


#endif
