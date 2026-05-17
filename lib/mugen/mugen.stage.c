#include "mugen.stage.h"
#include "str8.h"
#include "allocator.h"
#include "collection.array.h"
#include <string.h>
#include <stdlib.h>

static str8 stage_trim(str8 s)
{
    size start = 0;
    while (start < s.len && (s.data[start] == ' ' || s.data[start] == '\t')) start++;
    size end = s.len;
    while (end > start && (s.data[end - 1] == ' ' || s.data[end - 1] == '\t')) end--;
    return str8_from_parts(s.data + start, end - start);
}

static str8 stage_strip_comment(str8 line)
{
    for (size i = 0; i < line.len; i++)
        if (line.data[i] == ';') return str8_from_parts(line.data, i);
    return line;
}

static bool stage_ieq(str8 a, const char* b)
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

static bool stage_starts_with_i(str8 s, const char* prefix)
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

static str8 stage_before_eq(str8 line)
{
    for (size i = 0; i < line.len; i++)
        if (line.data[i] == '=') return stage_trim(str8_from_parts(line.data, i));
    return stage_trim(line);
}

static str8 stage_after_eq(str8 line)
{
    for (size i = 0; i < line.len; i++)
        if (line.data[i] == '=') return stage_trim(str8_from_parts(line.data + i + 1, line.len - i - 1));
    return (str8){0};
}

static f32 stage_parse_float(str8 s)
{
    char buf[64];
    size len = s.len < 63 ? s.len : 63;
    memcpy(buf, s.data, (size_t)len);
    buf[len] = 0;
    return (f32)atof(buf);
}

static i32 stage_parse_int(str8 s)
{
    char buf[64];
    size len = s.len < 63 ? s.len : 63;
    memcpy(buf, s.data, (size_t)len);
    buf[len] = 0;
    return (i32)atoi(buf);
}

static void stage_parse_pair_f32(str8 val, f32* a, f32* b)
{
    for (size i = 0; i < val.len; i++)
    {
        if (val.data[i] == ',')
        {
            *a = stage_parse_float(stage_trim(str8_from_parts(val.data, i)));
            *b = stage_parse_float(stage_trim(str8_from_parts(val.data + i + 1, val.len - i - 1)));
            return;
        }
    }
    *a = stage_parse_float(val);
    *b = *a;
}

static void stage_parse_pair_i32(str8 val, i32* a, i32* b)
{
    for (size i = 0; i < val.len; i++)
    {
        if (val.data[i] == ',')
        {
            *a = stage_parse_int(stage_trim(str8_from_parts(val.data, i)));
            *b = stage_parse_int(stage_trim(str8_from_parts(val.data + i + 1, val.len - i - 1)));
            return;
        }
    }
    *a = stage_parse_int(val);
    *b = *a;
}

static void stage_parse_pair_u16(str8 val, u16* a, u16* b)
{
    i32 ia, ib;
    stage_parse_pair_i32(val, &ia, &ib);
    *a = (u16)ia;
    *b = (u16)ib;
}

static u8 stage_parse_trans(str8 val)
{
    if (stage_ieq(val, "add"))      return MUGEN_TRANS_ADD;
    if (stage_ieq(val, "add1"))     return MUGEN_TRANS_ADD1;
    if (stage_ieq(val, "addalpha")) return MUGEN_TRANS_ADDALPHA;
    if (stage_ieq(val, "sub"))      return MUGEN_TRANS_SUB;
    return MUGEN_TRANS_NONE;
}

static u8 stage_parse_bg_type(str8 val)
{
    if (stage_ieq(val, "parallax")) return MUGEN_BG_PARALLAX;
    return MUGEN_BG_NORMAL;
}

static Mugen_Stage_BG stage_bg_defaults(void)
{
    return (Mugen_Stage_BG){
        .type         = MUGEN_BG_NORMAL,
        .layerno      = 0,
        .trans        = MUGEN_TRANS_NONE,
        .mask         = false,
        .sprite_group = 0,
        .sprite_number = 0,
        .start_x = 0, .start_y = 0,
        .delta_x = 1, .delta_y = 1,
        .velocity_x = 0, .velocity_y = 0,
        .tile_x = 0, .tile_y = 0,
        .tilespacing_x = 0, .tilespacing_y = 0,
        .alpha_src = 256, .alpha_dst = 0,
        .xscale_top = 1, .xscale_bot = 1,
        .yscalestart = 100, .yscaledelta = 0,
    };
}

#define SECTION_NONE       0
#define SECTION_PLAYERINFO 1
#define SECTION_BOUND      2
#define SECTION_CAMERA     3
#define SECTION_STAGEINFO  4
#define SECTION_BGDEF      5
#define SECTION_BG         6

bool mugen_stage_load(Mugen_Stage* out, str8 data, const Mel_Alloc* alloc)
{
    *out = (Mugen_Stage){
        .p1startx     = -70.0f,
        .p2startx     =  70.0f,
        .p1facing     =  1,
        .p2facing     = -1,
        .left_bound   = -150.0f,
        .right_bound  =  150.0f,
        .top_bound    = -25.0f,
        .bottom_bound =  0.0f,
        .zoffset      =  200.0f,
        .screenleft   =  15.0f,
        .screenright  =  15.0f,
        .camera_startx = 0.0f,
        .camera_starty = 0.0f,
        .tension      =  50.0f,
        .verticalfollow = 0.2f,
        .floortension =  0.0f,
        .localcoord_w =  320,
        .localcoord_h =  240,
        .spr_path     = (str8){0},
        .bgs          = NULL,
        .bg_count     = 0,
    };

    Mel_Array(Mugen_Stage_BG) bg_arr;
    mel_array_init(&bg_arr, alloc);

    u32 section = SECTION_NONE;
    Mugen_Stage_BG cur_bg = stage_bg_defaults();
    bool in_bg = false;
    usize pos = 0;

    while (pos < (usize)data.len)
    {
        usize start = pos;
        while (pos < (usize)data.len && data.data[pos] != '\n') pos++;
        usize end = pos;
        if (end > start && data.data[end - 1] == '\r') end--;
        if (pos < (usize)data.len) pos++;

        str8 line = stage_trim(stage_strip_comment(str8_from_parts(data.data + start, end - start)));
        if (line.len == 0) continue;

        if (line.data[0] == '[')
        {
            if (in_bg)
            {
                mel_array_push(&bg_arr, cur_bg);
                in_bg = false;
            }

            if (stage_starts_with_i(line, "[playerinfo]"))
                section = SECTION_PLAYERINFO;
            else if (stage_starts_with_i(line, "[bound]"))
                section = SECTION_BOUND;
            else if (stage_starts_with_i(line, "[camera]"))
                section = SECTION_CAMERA;
            else if (stage_starts_with_i(line, "[stageinfo]"))
                section = SECTION_STAGEINFO;
            else if (stage_starts_with_i(line, "[bgdef]"))
                section = SECTION_BGDEF;
            else if (stage_starts_with_i(line, "[bg ") || stage_ieq(line, "[bg]"))
            {
                section = SECTION_BG;
                cur_bg = stage_bg_defaults();
                in_bg = true;
            }
            else
                section = SECTION_NONE;
            continue;
        }

        if (section == SECTION_NONE) continue;

        str8 key = stage_before_eq(line);
        str8 val = stage_after_eq(line);
        if (val.len == 0) continue;

        switch (section)
        {
        case SECTION_PLAYERINFO:
            if (stage_ieq(key, "p1startx"))       out->p1startx = stage_parse_float(val);
            else if (stage_ieq(key, "p2startx"))   out->p2startx = stage_parse_float(val);
            else if (stage_ieq(key, "p1facing"))   out->p1facing = (i8)stage_parse_int(val);
            else if (stage_ieq(key, "p2facing"))   out->p2facing = (i8)stage_parse_int(val);
            break;

        case SECTION_BOUND:
            if (stage_ieq(key, "screenleft"))       out->screenleft = stage_parse_float(val);
            else if (stage_ieq(key, "screenright")) out->screenright = stage_parse_float(val);
            break;

        case SECTION_CAMERA:
            if (stage_ieq(key, "boundleft"))           out->left_bound = stage_parse_float(val);
            else if (stage_ieq(key, "boundright"))     out->right_bound = stage_parse_float(val);
            else if (stage_ieq(key, "boundhigh"))      out->top_bound = stage_parse_float(val);
            else if (stage_ieq(key, "boundlow"))       out->bottom_bound = stage_parse_float(val);
            else if (stage_ieq(key, "startx"))         out->camera_startx = stage_parse_float(val);
            else if (stage_ieq(key, "starty"))         out->camera_starty = stage_parse_float(val);
            else if (stage_ieq(key, "tension"))        out->tension = stage_parse_float(val);
            else if (stage_ieq(key, "verticalfollow")) out->verticalfollow = stage_parse_float(val);
            else if (stage_ieq(key, "floortension"))   out->floortension = stage_parse_float(val);
            break;

        case SECTION_STAGEINFO:
            if (stage_ieq(key, "zoffset"))         out->zoffset = stage_parse_float(val);
            else if (stage_ieq(key, "localcoord"))
            {
                i32 w, h;
                stage_parse_pair_i32(val, &w, &h);
                out->localcoord_w = w;
                out->localcoord_h = h;
            }
            break;

        case SECTION_BGDEF:
            if (stage_ieq(key, "spr"))
            {
                if (alloc)
                    out->spr_path = str8_dup_alloc(val, alloc);
            }
            break;

        case SECTION_BG:
            if (stage_ieq(key, "type"))              cur_bg.type = stage_parse_bg_type(val);
            else if (stage_ieq(key, "layerno"))      cur_bg.layerno = (u8)stage_parse_int(val);
            else if (stage_ieq(key, "trans"))        cur_bg.trans = stage_parse_trans(val);
            else if (stage_ieq(key, "mask"))         cur_bg.mask = stage_parse_int(val) != 0;
            else if (stage_ieq(key, "spriteno"))     stage_parse_pair_u16(val, &cur_bg.sprite_group, &cur_bg.sprite_number);
            else if (stage_ieq(key, "start"))        stage_parse_pair_f32(val, &cur_bg.start_x, &cur_bg.start_y);
            else if (stage_ieq(key, "delta"))        stage_parse_pair_f32(val, &cur_bg.delta_x, &cur_bg.delta_y);
            else if (stage_ieq(key, "velocity"))     stage_parse_pair_f32(val, &cur_bg.velocity_x, &cur_bg.velocity_y);
            else if (stage_ieq(key, "tile"))
            {
                i32 tx, ty;
                stage_parse_pair_i32(val, &tx, &ty);
                cur_bg.tile_x = tx;
                cur_bg.tile_y = ty;
            }
            else if (stage_ieq(key, "tilespacing"))
            {
                i32 tsx, tsy;
                stage_parse_pair_i32(val, &tsx, &tsy);
                cur_bg.tilespacing_x = tsx;
                cur_bg.tilespacing_y = tsy;
            }
            else if (stage_ieq(key, "alpha"))        stage_parse_pair_u16(val, &cur_bg.alpha_src, &cur_bg.alpha_dst);
            else if (stage_ieq(key, "xscale"))       stage_parse_pair_f32(val, &cur_bg.xscale_top, &cur_bg.xscale_bot);
            else if (stage_ieq(key, "yscalestart"))  cur_bg.yscalestart = stage_parse_float(val);
            else if (stage_ieq(key, "yscaledelta"))  cur_bg.yscaledelta = stage_parse_float(val);
            break;
        }
    }

    if (in_bg)
        mel_array_push(&bg_arr, cur_bg);

    out->bgs = bg_arr.items;
    out->bg_count = (u32)bg_arr.count;

    return true;
}

void mugen_stage_shutdown(Mugen_Stage* s, const Mel_Alloc* alloc)
{
    if (alloc && s->bgs)
        mel_dealloc(alloc, s->bgs);
    if (alloc && s->spr_path.data)
        mel_dealloc(alloc, s->spr_path.data);
    *s = (Mugen_Stage){0};
}
