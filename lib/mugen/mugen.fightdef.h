#pragma once

#include "core.types.h"
#include "string.str8.fwd.h"
#include "allocator.fwd.h"
#include "mugen.air.h"

typedef struct { u16 group, number; } Mugen_Spr_Ref;
typedef struct { u8 fontno, bank; i8 alignment; } Mugen_Font_Ref;
typedef struct { u16 group, number; } Mugen_Snd_Ref;

typedef struct {
    str8 sff;
    str8 snd;
    str8 font[10];
    str8 fightfx_sff;
    str8 fightfx_air;
    str8 common_snd;
} Mugen_Fightdef_Files;

typedef struct {
    i32 pos_x, pos_y;
    u32 bg0_anim;
    Mugen_Spr_Ref bg1_spr;
    i8 bg1_facing;
    Mugen_Spr_Ref mid_spr;
    i8 mid_facing;
    i32 mid_offset_x, mid_offset_y;
    Mugen_Spr_Ref front_spr;
    i8 front_facing;
    i32 range_x_start, range_x_end;
} Mugen_Fightdef_Bar_Player;

typedef struct {
    Mugen_Fightdef_Bar_Player p1;
    Mugen_Fightdef_Bar_Player p2;
} Mugen_Fightdef_Lifebar;

typedef struct {
    Mugen_Fightdef_Bar_Player p1;
    Mugen_Fightdef_Bar_Player p2;
    i32 p1_counter_offset_x, p1_counter_offset_y;
    Mugen_Font_Ref p1_counter_font;
    i32 p2_counter_offset_x, p2_counter_offset_y;
    Mugen_Font_Ref p2_counter_font;
    Mugen_Snd_Ref level1_snd;
    Mugen_Snd_Ref level2_snd;
    Mugen_Snd_Ref level3_snd;
} Mugen_Fightdef_Powerbar;

typedef struct {
    i32 pos_x, pos_y;
    Mugen_Spr_Ref bg_spr;
    i8 bg_facing;
    Mugen_Spr_Ref face_spr;
    i8 face_facing;
    i32 face_offset_x, face_offset_y;
    f32 face_scale_x, face_scale_y;
} Mugen_Fightdef_Face_Player;

typedef struct {
    Mugen_Fightdef_Face_Player p1;
    Mugen_Fightdef_Face_Player p2;
} Mugen_Fightdef_Face;

typedef struct {
    i32 pos_x, pos_y;
    Mugen_Font_Ref name_font;
} Mugen_Fightdef_Name_Player;

typedef struct {
    Mugen_Fightdef_Name_Player p1;
    Mugen_Fightdef_Name_Player p2;
} Mugen_Fightdef_Name;

typedef struct {
    i32 pos_x, pos_y;
    i32 counter_offset_x, counter_offset_y;
    Mugen_Font_Ref counter_font;
    i32 framespercount;
} Mugen_Fightdef_Time;

typedef struct {
    i32 pos_x, pos_y;
    i32 start_x;
    Mugen_Font_Ref counter_font;
    bool counter_shake;
    str8 text_text;
    Mugen_Font_Ref text_font;
    i32 text_offset_x, text_offset_y;
    i32 displaytime;
} Mugen_Fightdef_Combo_Team;

typedef struct {
    Mugen_Fightdef_Combo_Team team1;
    Mugen_Fightdef_Combo_Team team2;
} Mugen_Fightdef_Combo;

typedef struct {
    i32 match_wins;
    i32 match_maxdrawgames;
    i32 start_waittime;

    i32 pos_x, pos_y;

    i32 round_time;
    i32 round_default_offset_x, round_default_offset_y;
    Mugen_Font_Ref round_default_font;
    str8 round_default_text;
    i32 round_default_displaytime;

    Mugen_Snd_Ref round_snd[9];
    i32 round_sndtime;

    i32 fight_time;
    i32 fight_offset_x, fight_offset_y;
    u32 fight_anim;
    Mugen_Font_Ref fight_font;
    str8 fight_text;
    i32 fight_displaytime;
    Mugen_Snd_Ref fight_snd;
    i32 fight_sndtime;

    i32 ctrl_time;

    i32 ko_time;
    i32 ko_offset_x, ko_offset_y;
    u32 ko_anim;
    Mugen_Font_Ref ko_font;
    str8 ko_text;
    i32 ko_displaytime;
    Mugen_Snd_Ref ko_snd;

    i32 dko_offset_x, dko_offset_y;
    Mugen_Font_Ref dko_font;
    str8 dko_text;
    i32 dko_displaytime;
    Mugen_Snd_Ref dko_snd;

    i32 to_offset_x, to_offset_y;
    Mugen_Font_Ref to_font;
    str8 to_text;
    i32 to_displaytime;
    Mugen_Snd_Ref to_snd;

    i32 ko_sndtime;
    i32 slow_time;
    i32 over_waittime;
    i32 over_hittime;
    i32 over_wintime;
    i32 over_time;

    i32 win_time;
    i32 win_offset_x, win_offset_y;
    Mugen_Font_Ref win_font;
    str8 win_text;
    i32 win_displaytime;

    i32 win2_offset_x, win2_offset_y;
    Mugen_Font_Ref win2_font;
    str8 win2_text;
    i32 win2_displaytime;

    i32 draw_offset_x, draw_offset_y;
    Mugen_Font_Ref draw_font;
    str8 draw_text;
    i32 draw_displaytime;
} Mugen_Fightdef_Round;

typedef struct {
    i32 pos_x, pos_y;
    i32 iconoffset_x, iconoffset_y;
    i32 counter_offset_x, counter_offset_y;
    Mugen_Font_Ref counter_font;
    Mugen_Spr_Ref n_spr;
    Mugen_Spr_Ref s_spr;
    Mugen_Spr_Ref h_spr;
    Mugen_Spr_Ref throw_spr;
    Mugen_Spr_Ref c_spr;
    Mugen_Spr_Ref t_spr;
    Mugen_Spr_Ref suicide_spr;
    Mugen_Spr_Ref teammate_spr;
    Mugen_Spr_Ref perfect_spr;
} Mugen_Fightdef_Winicon_Player;

typedef struct {
    Mugen_Fightdef_Winicon_Player p1;
    Mugen_Fightdef_Winicon_Player p2;
    i32 useiconupto;
} Mugen_Fightdef_Winicon;

typedef struct {
    Mugen_Fightdef_Files files;
    f32 fightfx_scale;
    Mugen_Fightdef_Lifebar lifebar;
    Mugen_Fightdef_Powerbar powerbar;
    Mugen_Fightdef_Face face;
    Mugen_Fightdef_Name name;
    Mugen_Fightdef_Time time;
    Mugen_Fightdef_Combo combo;
    Mugen_Fightdef_Round round;
    Mugen_Fightdef_Winicon winicon;
    Mugen_Air actions;
} Mugen_Fightdef;

bool mugen_fightdef_load(Mugen_Fightdef* out, str8 data, const Mel_Alloc* alloc);
void mugen_fightdef_shutdown(Mugen_Fightdef* fd, const Mel_Alloc* alloc);
