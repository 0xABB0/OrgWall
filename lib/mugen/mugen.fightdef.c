#include "mugen.fightdef.h"
#include "string.str8.h"
#include "allocator.h"
#include <string.h>
#include <stdlib.h>

static str8 fightdef_trim(str8 s)
{
    size start = 0;
    while (start < s.len && (s.data[start] == ' ' || s.data[start] == '\t')) start++;
    size end = s.len;
    while (end > start && (s.data[end - 1] == ' ' || s.data[end - 1] == '\t')) end--;
    return str8_from_parts(s.data + start, end - start);
}

static str8 fightdef_strip_comment(str8 line)
{
    for (size i = 0; i < line.len; i++)
        if (line.data[i] == ';') return str8_from_parts(line.data, i);
    return line;
}

static bool fightdef_ieq(str8 a, const char* b)
{
    size blen = (size)strlen(b);
    if (a.len != blen) return false;
    for (size i = 0; i < blen; i++)
    {
        u8 ac = a.data[i]; if (ac >= 'A' && ac <= 'Z') ac += 32;
        u8 bc = (u8)b[i]; if (bc >= 'A' && bc <= 'Z') bc += 32;
        if (ac != bc) return false;
    }
    return true;
}

static bool fightdef_starts_with_i(str8 s, const char* prefix)
{
    size plen = (size)strlen(prefix);
    if (s.len < plen) return false;
    for (size i = 0; i < plen; i++)
    {
        u8 a = s.data[i]; if (a >= 'A' && a <= 'Z') a += 32;
        u8 b = (u8)prefix[i]; if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    return true;
}

static str8 fightdef_before_eq(str8 line)
{
    for (size i = 0; i < line.len; i++)
        if (line.data[i] == '=') return fightdef_trim(str8_from_parts(line.data, i));
    return fightdef_trim(line);
}

static str8 fightdef_after_eq(str8 line)
{
    for (size i = 0; i < line.len; i++)
        if (line.data[i] == '=') return fightdef_trim(str8_from_parts(line.data + i + 1, line.len - i - 1));
    return (str8){0};
}

static i32 fightdef_parse_int(str8 s)
{
    char buf[64];
    size len = s.len < 63 ? s.len : 63;
    memcpy(buf, s.data, (size_t)len);
    buf[len] = 0;
    return (i32)atoi(buf);
}

static f32 fightdef_parse_float(str8 s)
{
    char buf[64];
    size len = s.len < 63 ? s.len : 63;
    memcpy(buf, s.data, (size_t)len);
    buf[len] = 0;
    return (f32)atof(buf);
}

static void fightdef_parse_pair_i32(str8 val, i32* a, i32* b)
{
    for (size i = 0; i < val.len; i++)
    {
        if (val.data[i] == ',')
        {
            *a = fightdef_parse_int(fightdef_trim(str8_from_parts(val.data, i)));
            *b = fightdef_parse_int(fightdef_trim(str8_from_parts(val.data + i + 1, val.len - i - 1)));
            return;
        }
    }
    *a = fightdef_parse_int(val);
    *b = *a;
}

static Mugen_Spr_Ref fightdef_parse_spr_ref(str8 val)
{
    Mugen_Spr_Ref ref = {0};
    i32 g, n;
    fightdef_parse_pair_i32(val, &g, &n);
    ref.group = (u16)g;
    ref.number = (u16)n;
    return ref;
}

static Mugen_Snd_Ref fightdef_parse_snd_ref(str8 val)
{
    Mugen_Snd_Ref ref = {0};
    i32 g, n;
    fightdef_parse_pair_i32(val, &g, &n);
    ref.group = (u16)g;
    ref.number = (u16)n;
    return ref;
}

static Mugen_Font_Ref fightdef_parse_font_ref(str8 val)
{
    Mugen_Font_Ref ref = {0};
    i32 parts[3] = {0};
    i32 count = 0;
    size start = 0;

    for (size i = 0; i <= val.len && count < 3; i++)
    {
        if (i == val.len || val.data[i] == ',')
        {
            str8 tok = fightdef_trim(str8_from_parts(val.data + start, i - start));
            if (tok.len > 0)
                parts[count] = fightdef_parse_int(tok);
            count++;
            start = i + 1;
        }
    }

    ref.fontno = (u8)parts[0];
    ref.bank = (u8)parts[1];
    ref.alignment = (i8)parts[2];
    return ref;
}

static Mugen_Fightdef_Bar_Player fightdef_bar_defaults(void)
{
    return (Mugen_Fightdef_Bar_Player){
        .bg1_facing = 1,
        .mid_facing = 1,
        .front_facing = 1,
    };
}

static Mugen_Fightdef_Face_Player fightdef_face_defaults(void)
{
    return (Mugen_Fightdef_Face_Player){
        .bg_facing = 1,
        .face_facing = 1,
        .face_scale_x = 1.0f,
        .face_scale_y = 1.0f,
    };
}

#define SEC_NONE      0
#define SEC_FILES     1
#define SEC_FIGHTFX   2
#define SEC_LIFEBAR   3
#define SEC_POWERBAR  4
#define SEC_FACE      5
#define SEC_NAME      6
#define SEC_TIME      7
#define SEC_COMBO     8
#define SEC_ROUND     9
#define SEC_WINICON   10
#define SEC_SKIP      11

static void parse_bar_key(Mugen_Fightdef_Bar_Player* bar, str8 key, str8 val)
{
    if (fightdef_ieq(key, "bg0.anim"))          bar->bg0_anim = (u32)fightdef_parse_int(val);
    else if (fightdef_ieq(key, "bg0.facing"))    { /* bg0 is anim, facing handled by anim */ }
    else if (fightdef_ieq(key, "bg1.spr"))       bar->bg1_spr = fightdef_parse_spr_ref(val);
    else if (fightdef_ieq(key, "bg1.facing"))    bar->bg1_facing = (i8)fightdef_parse_int(val);
    else if (fightdef_ieq(key, "mid.spr"))       bar->mid_spr = fightdef_parse_spr_ref(val);
    else if (fightdef_ieq(key, "mid.facing"))    bar->mid_facing = (i8)fightdef_parse_int(val);
    else if (fightdef_ieq(key, "mid.offset"))    fightdef_parse_pair_i32(val, &bar->mid_offset_x, &bar->mid_offset_y);
    else if (fightdef_ieq(key, "front.spr"))     bar->front_spr = fightdef_parse_spr_ref(val);
    else if (fightdef_ieq(key, "front.facing"))  bar->front_facing = (i8)fightdef_parse_int(val);
    else if (fightdef_ieq(key, "front.offset"))  { /* same struct could be extended */ }
    else if (fightdef_ieq(key, "range.x"))       fightdef_parse_pair_i32(val, &bar->range_x_start, &bar->range_x_end);
}

static void parse_lifebar_line(Mugen_Fightdef_Lifebar* lb, str8 key, str8 val)
{
    if (fightdef_starts_with_i(key, "p1."))
    {
        str8 sub = str8_from_parts(key.data + 3, key.len - 3);
        if (fightdef_ieq(sub, "pos"))
            fightdef_parse_pair_i32(val, &lb->p1.pos_x, &lb->p1.pos_y);
        else
            parse_bar_key(&lb->p1, sub, val);
    }
    else if (fightdef_starts_with_i(key, "p2."))
    {
        str8 sub = str8_from_parts(key.data + 3, key.len - 3);
        if (fightdef_ieq(sub, "pos"))
            fightdef_parse_pair_i32(val, &lb->p2.pos_x, &lb->p2.pos_y);
        else
            parse_bar_key(&lb->p2, sub, val);
    }
}

static void parse_powerbar_line(Mugen_Fightdef_Powerbar* pb, str8 key, str8 val)
{
    if (fightdef_starts_with_i(key, "p1."))
    {
        str8 sub = str8_from_parts(key.data + 3, key.len - 3);
        if (fightdef_ieq(sub, "pos"))
            fightdef_parse_pair_i32(val, &pb->p1.pos_x, &pb->p1.pos_y);
        else if (fightdef_ieq(sub, "counter.offset"))
            fightdef_parse_pair_i32(val, &pb->p1_counter_offset_x, &pb->p1_counter_offset_y);
        else if (fightdef_ieq(sub, "counter.font"))
            pb->p1_counter_font = fightdef_parse_font_ref(val);
        else
            parse_bar_key(&pb->p1, sub, val);
    }
    else if (fightdef_starts_with_i(key, "p2."))
    {
        str8 sub = str8_from_parts(key.data + 3, key.len - 3);
        if (fightdef_ieq(sub, "pos"))
            fightdef_parse_pair_i32(val, &pb->p2.pos_x, &pb->p2.pos_y);
        else if (fightdef_ieq(sub, "counter.offset"))
            fightdef_parse_pair_i32(val, &pb->p2_counter_offset_x, &pb->p2_counter_offset_y);
        else if (fightdef_ieq(sub, "counter.font"))
            pb->p2_counter_font = fightdef_parse_font_ref(val);
        else
            parse_bar_key(&pb->p2, sub, val);
    }
    else if (fightdef_ieq(key, "level1.snd"))
        pb->level1_snd = fightdef_parse_snd_ref(val);
    else if (fightdef_ieq(key, "level2.snd"))
        pb->level2_snd = fightdef_parse_snd_ref(val);
    else if (fightdef_ieq(key, "level3.snd"))
        pb->level3_snd = fightdef_parse_snd_ref(val);
}

static void parse_face_player(Mugen_Fightdef_Face_Player* fp, str8 key, str8 val)
{
    if (fightdef_ieq(key, "pos"))
        fightdef_parse_pair_i32(val, &fp->pos_x, &fp->pos_y);
    else if (fightdef_ieq(key, "bg.spr"))
        fp->bg_spr = fightdef_parse_spr_ref(val);
    else if (fightdef_ieq(key, "bg.facing"))
        fp->bg_facing = (i8)fightdef_parse_int(val);
    else if (fightdef_ieq(key, "face.spr"))
        fp->face_spr = fightdef_parse_spr_ref(val);
    else if (fightdef_ieq(key, "face.facing"))
        fp->face_facing = (i8)fightdef_parse_int(val);
    else if (fightdef_ieq(key, "face.offset"))
        fightdef_parse_pair_i32(val, &fp->face_offset_x, &fp->face_offset_y);
    else if (fightdef_ieq(key, "face.scale"))
    {
        str8 sx_str = val, sy_str = val;
        for (size i = 0; i < val.len; i++)
        {
            if (val.data[i] == ',')
            {
                sx_str = fightdef_trim(str8_from_parts(val.data, i));
                sy_str = fightdef_trim(str8_from_parts(val.data + i + 1, val.len - i - 1));
                break;
            }
        }
        fp->face_scale_x = fightdef_parse_float(sx_str);
        fp->face_scale_y = fightdef_parse_float(sy_str);
    }
}

static void parse_face_line(Mugen_Fightdef_Face* face, str8 key, str8 val)
{
    if (fightdef_starts_with_i(key, "p1."))
        parse_face_player(&face->p1, str8_from_parts(key.data + 3, key.len - 3), val);
    else if (fightdef_starts_with_i(key, "p2."))
        parse_face_player(&face->p2, str8_from_parts(key.data + 3, key.len - 3), val);
}

static void parse_name_line(Mugen_Fightdef_Name* name, str8 key, str8 val)
{
    if (fightdef_starts_with_i(key, "p1."))
    {
        str8 sub = str8_from_parts(key.data + 3, key.len - 3);
        if (fightdef_ieq(sub, "pos"))
            fightdef_parse_pair_i32(val, &name->p1.pos_x, &name->p1.pos_y);
        else if (fightdef_ieq(sub, "name.font"))
            name->p1.name_font = fightdef_parse_font_ref(val);
    }
    else if (fightdef_starts_with_i(key, "p2."))
    {
        str8 sub = str8_from_parts(key.data + 3, key.len - 3);
        if (fightdef_ieq(sub, "pos"))
            fightdef_parse_pair_i32(val, &name->p2.pos_x, &name->p2.pos_y);
        else if (fightdef_ieq(sub, "name.font"))
            name->p2.name_font = fightdef_parse_font_ref(val);
    }
}

static void parse_time_line(Mugen_Fightdef_Time* t, str8 key, str8 val)
{
    if (fightdef_ieq(key, "pos"))
        fightdef_parse_pair_i32(val, &t->pos_x, &t->pos_y);
    else if (fightdef_ieq(key, "counter.offset"))
        fightdef_parse_pair_i32(val, &t->counter_offset_x, &t->counter_offset_y);
    else if (fightdef_ieq(key, "counter.font"))
        t->counter_font = fightdef_parse_font_ref(val);
    else if (fightdef_ieq(key, "framespercount"))
        t->framespercount = fightdef_parse_int(val);
}

static void parse_combo_team(Mugen_Fightdef_Combo_Team* ct, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (fightdef_ieq(key, "pos"))
        fightdef_parse_pair_i32(val, &ct->pos_x, &ct->pos_y);
    else if (fightdef_ieq(key, "start.x"))
        ct->start_x = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "counter.font"))
        ct->counter_font = fightdef_parse_font_ref(val);
    else if (fightdef_ieq(key, "counter.shake"))
        ct->counter_shake = fightdef_parse_int(val) != 0;
    else if (fightdef_ieq(key, "text.text"))
    {
        if (alloc)
            ct->text_text = str8_dup_alloc(val, alloc);
    }
    else if (fightdef_ieq(key, "text.font"))
        ct->text_font = fightdef_parse_font_ref(val);
    else if (fightdef_ieq(key, "text.offset"))
        fightdef_parse_pair_i32(val, &ct->text_offset_x, &ct->text_offset_y);
    else if (fightdef_ieq(key, "displaytime"))
        ct->displaytime = fightdef_parse_int(val);
}

static void parse_combo_line(Mugen_Fightdef_Combo* combo, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (fightdef_starts_with_i(key, "team1."))
        parse_combo_team(&combo->team1, str8_from_parts(key.data + 6, key.len - 6), val, alloc);
    else if (fightdef_starts_with_i(key, "team2."))
        parse_combo_team(&combo->team2, str8_from_parts(key.data + 6, key.len - 6), val, alloc);
}

static void parse_round_line(Mugen_Fightdef_Round* r, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (fightdef_ieq(key, "match.wins"))              r->match_wins = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "match.maxdrawgames")) r->match_maxdrawgames = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "start.waittime"))     r->start_waittime = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "pos"))                fightdef_parse_pair_i32(val, &r->pos_x, &r->pos_y);

    else if (fightdef_ieq(key, "round.time"))              r->round_time = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "round.default.offset"))    fightdef_parse_pair_i32(val, &r->round_default_offset_x, &r->round_default_offset_y);
    else if (fightdef_ieq(key, "round.default.font"))      r->round_default_font = fightdef_parse_font_ref(val);
    else if (fightdef_ieq(key, "round.default.text"))      { if (alloc) r->round_default_text = str8_dup_alloc(val, alloc); }
    else if (fightdef_ieq(key, "round.default.displaytime")) r->round_default_displaytime = fightdef_parse_int(val);

    else if (fightdef_ieq(key, "round1.snd"))  r->round_snd[0] = fightdef_parse_snd_ref(val);
    else if (fightdef_ieq(key, "round2.snd"))  r->round_snd[1] = fightdef_parse_snd_ref(val);
    else if (fightdef_ieq(key, "round3.snd"))  r->round_snd[2] = fightdef_parse_snd_ref(val);
    else if (fightdef_ieq(key, "round4.snd"))  r->round_snd[3] = fightdef_parse_snd_ref(val);
    else if (fightdef_ieq(key, "round5.snd"))  r->round_snd[4] = fightdef_parse_snd_ref(val);
    else if (fightdef_ieq(key, "round6.snd"))  r->round_snd[5] = fightdef_parse_snd_ref(val);
    else if (fightdef_ieq(key, "round7.snd"))  r->round_snd[6] = fightdef_parse_snd_ref(val);
    else if (fightdef_ieq(key, "round8.snd"))  r->round_snd[7] = fightdef_parse_snd_ref(val);
    else if (fightdef_ieq(key, "round9.snd"))  r->round_snd[8] = fightdef_parse_snd_ref(val);
    else if (fightdef_ieq(key, "round.sndtime")) r->round_sndtime = fightdef_parse_int(val);

    else if (fightdef_ieq(key, "fight.time"))     r->fight_time = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "fight.offset"))   fightdef_parse_pair_i32(val, &r->fight_offset_x, &r->fight_offset_y);
    else if (fightdef_ieq(key, "fight.anim"))     r->fight_anim = (u32)fightdef_parse_int(val);
    else if (fightdef_ieq(key, "fight.font"))     r->fight_font = fightdef_parse_font_ref(val);
    else if (fightdef_ieq(key, "fight.text"))     { if (alloc) r->fight_text = str8_dup_alloc(val, alloc); }
    else if (fightdef_ieq(key, "fight.displaytime")) r->fight_displaytime = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "fight.snd"))      r->fight_snd = fightdef_parse_snd_ref(val);
    else if (fightdef_ieq(key, "fight.sndtime"))  r->fight_sndtime = fightdef_parse_int(val);

    else if (fightdef_ieq(key, "ctrl.time"))      r->ctrl_time = fightdef_parse_int(val);

    else if (fightdef_ieq(key, "ko.time"))        r->ko_time = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "ko.offset"))      fightdef_parse_pair_i32(val, &r->ko_offset_x, &r->ko_offset_y);
    else if (fightdef_ieq(key, "ko.anim"))        r->ko_anim = (u32)fightdef_parse_int(val);
    else if (fightdef_ieq(key, "ko.font"))        r->ko_font = fightdef_parse_font_ref(val);
    else if (fightdef_ieq(key, "ko.text"))        { if (alloc) r->ko_text = str8_dup_alloc(val, alloc); }
    else if (fightdef_ieq(key, "ko.displaytime")) r->ko_displaytime = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "ko.snd"))         r->ko_snd = fightdef_parse_snd_ref(val);

    else if (fightdef_ieq(key, "dko.offset"))      fightdef_parse_pair_i32(val, &r->dko_offset_x, &r->dko_offset_y);
    else if (fightdef_ieq(key, "dko.font"))        r->dko_font = fightdef_parse_font_ref(val);
    else if (fightdef_ieq(key, "dko.text"))        { if (alloc) r->dko_text = str8_dup_alloc(val, alloc); }
    else if (fightdef_ieq(key, "dko.displaytime")) r->dko_displaytime = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "dko.snd"))         r->dko_snd = fightdef_parse_snd_ref(val);

    else if (fightdef_ieq(key, "to.offset"))       fightdef_parse_pair_i32(val, &r->to_offset_x, &r->to_offset_y);
    else if (fightdef_ieq(key, "to.font"))         r->to_font = fightdef_parse_font_ref(val);
    else if (fightdef_ieq(key, "to.text"))         { if (alloc) r->to_text = str8_dup_alloc(val, alloc); }
    else if (fightdef_ieq(key, "to.displaytime"))  r->to_displaytime = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "to.snd"))          r->to_snd = fightdef_parse_snd_ref(val);

    else if (fightdef_ieq(key, "ko.sndtime"))      r->ko_sndtime = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "slow.time"))       r->slow_time = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "over.waittime"))   r->over_waittime = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "over.hittime"))    r->over_hittime = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "over.wintime"))    r->over_wintime = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "over.time"))       r->over_time = fightdef_parse_int(val);

    else if (fightdef_ieq(key, "win.time"))        r->win_time = fightdef_parse_int(val);
    else if (fightdef_ieq(key, "win.offset"))      fightdef_parse_pair_i32(val, &r->win_offset_x, &r->win_offset_y);
    else if (fightdef_ieq(key, "win.font"))        r->win_font = fightdef_parse_font_ref(val);
    else if (fightdef_ieq(key, "win.text"))        { if (alloc) r->win_text = str8_dup_alloc(val, alloc); }
    else if (fightdef_ieq(key, "win.displaytime")) r->win_displaytime = fightdef_parse_int(val);

    else if (fightdef_ieq(key, "win2.offset"))      fightdef_parse_pair_i32(val, &r->win2_offset_x, &r->win2_offset_y);
    else if (fightdef_ieq(key, "win2.font"))        r->win2_font = fightdef_parse_font_ref(val);
    else if (fightdef_ieq(key, "win2.text"))        { if (alloc) r->win2_text = str8_dup_alloc(val, alloc); }
    else if (fightdef_ieq(key, "win2.displaytime")) r->win2_displaytime = fightdef_parse_int(val);

    else if (fightdef_ieq(key, "draw.offset"))      fightdef_parse_pair_i32(val, &r->draw_offset_x, &r->draw_offset_y);
    else if (fightdef_ieq(key, "draw.font"))        r->draw_font = fightdef_parse_font_ref(val);
    else if (fightdef_ieq(key, "draw.text"))        { if (alloc) r->draw_text = str8_dup_alloc(val, alloc); }
    else if (fightdef_ieq(key, "draw.displaytime")) r->draw_displaytime = fightdef_parse_int(val);
}

static void parse_winicon_player(Mugen_Fightdef_Winicon_Player* wp, str8 key, str8 val)
{
    if (fightdef_ieq(key, "pos"))              fightdef_parse_pair_i32(val, &wp->pos_x, &wp->pos_y);
    else if (fightdef_ieq(key, "iconoffset"))  fightdef_parse_pair_i32(val, &wp->iconoffset_x, &wp->iconoffset_y);
    else if (fightdef_ieq(key, "counter.offset")) fightdef_parse_pair_i32(val, &wp->counter_offset_x, &wp->counter_offset_y);
    else if (fightdef_ieq(key, "counter.font"))   wp->counter_font = fightdef_parse_font_ref(val);
    else if (fightdef_ieq(key, "n.spr"))       wp->n_spr = fightdef_parse_spr_ref(val);
    else if (fightdef_ieq(key, "s.spr"))       wp->s_spr = fightdef_parse_spr_ref(val);
    else if (fightdef_ieq(key, "h.spr"))       wp->h_spr = fightdef_parse_spr_ref(val);
    else if (fightdef_ieq(key, "throw.spr"))   wp->throw_spr = fightdef_parse_spr_ref(val);
    else if (fightdef_ieq(key, "c.spr"))       wp->c_spr = fightdef_parse_spr_ref(val);
    else if (fightdef_ieq(key, "t.spr"))       wp->t_spr = fightdef_parse_spr_ref(val);
    else if (fightdef_ieq(key, "suicide.spr")) wp->suicide_spr = fightdef_parse_spr_ref(val);
    else if (fightdef_ieq(key, "teammate.spr")) wp->teammate_spr = fightdef_parse_spr_ref(val);
    else if (fightdef_ieq(key, "perfect.spr")) wp->perfect_spr = fightdef_parse_spr_ref(val);
}

static void parse_winicon_line(Mugen_Fightdef_Winicon* wi, str8 key, str8 val)
{
    if (fightdef_starts_with_i(key, "p1."))
        parse_winicon_player(&wi->p1, str8_from_parts(key.data + 3, key.len - 3), val);
    else if (fightdef_starts_with_i(key, "p2."))
        parse_winicon_player(&wi->p2, str8_from_parts(key.data + 3, key.len - 3), val);
    else if (fightdef_ieq(key, "useiconupto"))
        wi->useiconupto = fightdef_parse_int(val);
}

bool mugen_fightdef_load(Mugen_Fightdef* out, str8 data, const Mel_Alloc* alloc)
{
    *out = (Mugen_Fightdef){0};

    out->fightfx_scale = 1.0f;

    out->lifebar.p1 = fightdef_bar_defaults();
    out->lifebar.p2 = fightdef_bar_defaults();
    out->powerbar.p1 = fightdef_bar_defaults();
    out->powerbar.p2 = fightdef_bar_defaults();

    out->face.p1 = fightdef_face_defaults();
    out->face.p2 = fightdef_face_defaults();

    out->time.framespercount = 60;

    out->combo.team1.displaytime = 90;
    out->combo.team2.displaytime = 90;

    out->round.match_wins = 2;
    out->round.match_maxdrawgames = 1;
    out->round.start_waittime = 30;
    out->round.round_default_displaytime = 60;
    out->round.ctrl_time = 30;
    out->round.slow_time = 60;
    out->round.over_waittime = 45;
    out->round.over_hittime = 10;
    out->round.over_wintime = 45;
    out->round.over_time = 210;
    out->round.win_time = 60;
    out->round.win_displaytime = 540;
    out->round.win2_displaytime = 540;
    out->round.draw_displaytime = 540;
    out->round.dko_displaytime = 60;
    out->round.to_displaytime = 60;

    out->winicon.useiconupto = 4;

    if (alloc && data.len > 0)
        mugen_air_load(&out->actions, data, alloc);

    if (data.len == 0) return true;

    u32 section = SEC_NONE;
    usize pos = 0;

    while (pos < (usize)data.len)
    {
        usize start = pos;
        while (pos < (usize)data.len && data.data[pos] != '\n') pos++;
        usize end = pos;
        if (end > start && data.data[end - 1] == '\r') end--;
        if (pos < (usize)data.len) pos++;

        str8 line = fightdef_trim(fightdef_strip_comment(str8_from_parts(data.data + start, end - start)));
        if (line.len == 0) continue;

        if (line.data[0] == '[')
        {
            if (fightdef_starts_with_i(line, "[files]"))           section = SEC_FILES;
            else if (fightdef_starts_with_i(line, "[fightfx]"))    section = SEC_FIGHTFX;
            else if (fightdef_ieq(line, "[lifebar]"))              section = SEC_LIFEBAR;
            else if (fightdef_ieq(line, "[powerbar]"))             section = SEC_POWERBAR;
            else if (fightdef_ieq(line, "[face]"))                 section = SEC_FACE;
            else if (fightdef_ieq(line, "[name]"))                 section = SEC_NAME;
            else if (fightdef_ieq(line, "[time]"))                 section = SEC_TIME;
            else if (fightdef_ieq(line, "[combo]"))                section = SEC_COMBO;
            else if (fightdef_ieq(line, "[round]"))                section = SEC_ROUND;
            else if (fightdef_ieq(line, "[winicon]"))              section = SEC_WINICON;
            else if (fightdef_starts_with_i(line, "[begin action")) section = SEC_SKIP;
            else if (fightdef_starts_with_i(line, "[simul "))      section = SEC_SKIP;
            else if (fightdef_starts_with_i(line, "[turns "))      section = SEC_SKIP;
            else section = SEC_NONE;
            continue;
        }

        if (section == SEC_NONE || section == SEC_SKIP) continue;

        str8 key = fightdef_before_eq(line);
        str8 val = fightdef_after_eq(line);
        if (val.len == 0) continue;

        switch (section)
        {
        case SEC_FILES:
            if (fightdef_ieq(key, "sff"))            { if (alloc) out->files.sff = str8_dup_alloc(val, alloc); }
            else if (fightdef_ieq(key, "snd"))       { if (alloc) out->files.snd = str8_dup_alloc(val, alloc); }
            else if (fightdef_ieq(key, "fightfx.sff")) { if (alloc) out->files.fightfx_sff = str8_dup_alloc(val, alloc); }
            else if (fightdef_ieq(key, "fightfx.air")) { if (alloc) out->files.fightfx_air = str8_dup_alloc(val, alloc); }
            else if (fightdef_ieq(key, "common.snd"))  { if (alloc) out->files.common_snd = str8_dup_alloc(val, alloc); }
            else if (fightdef_starts_with_i(key, "font"))
            {
                str8 idx_str = str8_from_parts(key.data + 4, key.len - 4);
                i32 idx = fightdef_parse_int(idx_str);
                if (idx >= 1 && idx <= 10 && alloc)
                    out->files.font[idx - 1] = str8_dup_alloc(val, alloc);
            }
            break;

        case SEC_FIGHTFX:
            if (fightdef_ieq(key, "scale"))
                out->fightfx_scale = fightdef_parse_float(val);
            break;

        case SEC_LIFEBAR:  parse_lifebar_line(&out->lifebar, key, val); break;
        case SEC_POWERBAR: parse_powerbar_line(&out->powerbar, key, val); break;
        case SEC_FACE:     parse_face_line(&out->face, key, val); break;
        case SEC_NAME:     parse_name_line(&out->name, key, val); break;
        case SEC_TIME:     parse_time_line(&out->time, key, val); break;
        case SEC_COMBO:    parse_combo_line(&out->combo, key, val, alloc); break;
        case SEC_ROUND:    parse_round_line(&out->round, key, val, alloc); break;
        case SEC_WINICON:  parse_winicon_line(&out->winicon, key, val); break;
        }
    }

    return true;
}

static void fightdef_free_str8(str8* s, const Mel_Alloc* alloc)
{
    if (s->data)
    {
        mel_dealloc(alloc, s->data);
        *s = (str8){0};
    }
}

void mugen_fightdef_shutdown(Mugen_Fightdef* fd, const Mel_Alloc* alloc)
{
    if (!alloc) return;

    fightdef_free_str8(&fd->files.sff, alloc);
    fightdef_free_str8(&fd->files.snd, alloc);
    fightdef_free_str8(&fd->files.fightfx_sff, alloc);
    fightdef_free_str8(&fd->files.fightfx_air, alloc);
    fightdef_free_str8(&fd->files.common_snd, alloc);
    for (i32 i = 0; i < 10; i++)
        fightdef_free_str8(&fd->files.font[i], alloc);

    fightdef_free_str8(&fd->combo.team1.text_text, alloc);
    fightdef_free_str8(&fd->combo.team2.text_text, alloc);

    fightdef_free_str8(&fd->round.round_default_text, alloc);
    fightdef_free_str8(&fd->round.fight_text, alloc);
    fightdef_free_str8(&fd->round.ko_text, alloc);
    fightdef_free_str8(&fd->round.dko_text, alloc);
    fightdef_free_str8(&fd->round.to_text, alloc);
    fightdef_free_str8(&fd->round.win_text, alloc);
    fightdef_free_str8(&fd->round.win2_text, alloc);
    fightdef_free_str8(&fd->round.draw_text, alloc);

    mugen_air_shutdown(&fd->actions, alloc);

    *fd = (Mugen_Fightdef){0};
}
