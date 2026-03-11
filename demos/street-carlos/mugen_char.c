#include "mugen_char.h"
#include "mugen.sff.h"
#include "mugen.air.h"
#include "mugen.cmd.h"
#include "mugen.cns.h"
#include "anim.clip.h"
#include "anim.registry.h"
#include "collection.slotmap.h"
#include "gpu.texture.h"
#include "gpu.pipeline.h"
#include "texture.pool.h"
#include "sprite.pass.h"
#include "vfs.h"
#include "allocator.h"
#include "string.str8.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <assert.h>

#define MUGEN_DEF_MAX_ST 10

typedef struct {
    str8 cmd;
    str8 cns;
    str8 sprite;
    str8 anim;
    str8 stcommon;
    str8 st[MUGEN_DEF_MAX_ST];
    u32 st_count;
} Mugen_Def;

static str8 def_trim(str8 s)
{
    size start = 0;
    while (start < s.len && (s.data[start] == ' ' || s.data[start] == '\t')) start++;
    size end = s.len;
    while (end > start && (s.data[end - 1] == ' ' || s.data[end - 1] == '\t')) end--;
    return str8_from_parts(s.data + start, end - start);
}

static str8 def_strip_comment(str8 line)
{
    for (size i = 0; i < line.len; i++)
        if (line.data[i] == ';') return str8_from_parts(line.data, i);
    return line;
}

static bool def_ieq(str8 a, const char* b)
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

static bool def_starts_with_i(str8 s, const char* prefix)
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

static str8 def_before_eq(str8 line)
{
    for (size i = 0; i < line.len; i++)
        if (line.data[i] == '=') return def_trim(str8_from_parts(line.data, i));
    return def_trim(line);
}

static str8 def_after_eq(str8 line)
{
    for (size i = 0; i < line.len; i++)
        if (line.data[i] == '=') return def_trim(str8_from_parts(line.data + i + 1, line.len - i - 1));
    return (str8){0};
}

static bool mugen_def_parse(Mugen_Def* def, str8 data)
{
    *def = (Mugen_Def){0};

    bool in_files = false;
    usize pos = 0;

    while (pos < (usize)data.len)
    {
        usize start = pos;
        while (pos < (usize)data.len && data.data[pos] != '\n') pos++;
        usize end = pos;
        if (end > start && data.data[end - 1] == '\r') end--;
        if (pos < (usize)data.len) pos++;

        str8 line = def_trim(def_strip_comment(str8_from_parts(data.data + start, end - start)));
        if (line.len == 0) continue;

        if (line.data[0] == '[')
        {
            in_files = def_starts_with_i(line, "[files]");
            continue;
        }

        if (!in_files) continue;

        str8 key = def_before_eq(line);
        str8 val = def_after_eq(line);
        if (val.len == 0) continue;

        if (def_ieq(key, "cmd"))          def->cmd = val;
        else if (def_ieq(key, "cns"))     def->cns = val;
        else if (def_ieq(key, "sprite"))  def->sprite = val;
        else if (def_ieq(key, "anim"))    def->anim = val;
        else if (def_ieq(key, "stcommon")) def->stcommon = val;
        else if (def_ieq(key, "st") || def_starts_with_i(key, "st"))
        {
            if (def_ieq(key, "stcommon") || def_ieq(key, "sprite")) continue;

            bool is_st = def_ieq(key, "st");
            bool is_stN = !is_st && key.len >= 3 && key.data[0] == 's' && key.data[1] == 't'
                          && key.data[2] >= '0' && key.data[2] <= '9';
            if (!is_st && !is_stN) continue;

            if (def->st_count < MUGEN_DEF_MAX_ST)
            {
                bool duplicate = false;
                for (u32 i = 0; i < def->st_count; i++)
                {
                    if (def->st[i].len == val.len && memcmp(def->st[i].data, val.data, (size_t)val.len) == 0)
                    { duplicate = true; break; }
                }
                if (!duplicate)
                    def->st[def->st_count++] = val;
            }
        }
    }

    return def->sprite.len > 0 && def->anim.len > 0;
}

static u8* track_file_data(Mugen_Char* mc, u8* data)
{
    if (data && mc->file_data_count < MUGEN_CHAR_MAX_FILES)
        mc->file_data[mc->file_data_count++] = data;
    return data;
}

static str8 resolve_path(str8 char_dir, str8 filename, char* buf, size buf_size)
{
    if (char_dir.len + 1 + filename.len >= buf_size) return (str8){0};
    memcpy(buf, char_dir.data, (size_t)char_dir.len);
    buf[char_dir.len] = '/';
    memcpy(buf + char_dir.len + 1, filename.data, (size_t)filename.len);
    size total = char_dir.len + 1 + filename.len;
    buf[total] = 0;
    return str8_from_parts((u8*)buf, total);
}

static str8 dir_of(str8 path)
{
    size last_slash = -1;
    for (size i = path.len - 1; i >= 0; i--)
    {
        if (path.data[i] == '/')
        { last_slash = i; break; }
    }
    if (last_slash < 0) return S8(".");
    return str8_from_parts(path.data, last_slash);
}

bool mugen_char_load_opt(Mugen_Char* mc, Mugen_Char_Load_Opt opt)
{
    *mc = (Mugen_Char){0};

    usize def_size = 0;
    u8* def_data = mel_vfs_read_file_alloc(opt.vfs, opt.def_path, &def_size, opt.alloc);
    if (!def_data) return false;

    Mugen_Def def;
    str8 def_str = str8_from_parts(def_data, (size)def_size);
    if (!mugen_def_parse(&def, def_str))
    {
        mel_dealloc(opt.alloc, def_data);
        SDL_Log("MUGEN char: failed to parse def file");
        return false;
    }

    str8 char_dir = dir_of(opt.def_path);
    char path_buf[512];

    str8 sff_path = resolve_path(char_dir, def.sprite, path_buf, sizeof(path_buf));
    if (!mugen_sff_load(&mc->sff, opt.vfs, sff_path, opt.alloc))
    {
        mel_dealloc(opt.alloc, def_data);
        return false;
    }

    str8 air_path = resolve_path(char_dir, def.anim, path_buf, sizeof(path_buf));
    usize air_size = 0;
    u8* air_data = mel_vfs_read_file_alloc(opt.vfs, air_path, &air_size, opt.alloc);
    if (!air_data)
    {
        mugen_sff_shutdown(&mc->sff, opt.alloc);
        mel_dealloc(opt.alloc, def_data);
        return false;
    }

    str8 air_str = str8_from_parts(air_data, (size)air_size);
    if (!mugen_air_load(&mc->air, air_str, opt.alloc))
    {
        mel_dealloc(opt.alloc, air_data);
        mugen_sff_shutdown(&mc->sff, opt.alloc);
        mel_dealloc(opt.alloc, def_data);
        return false;
    }
    mel_dealloc(opt.alloc, air_data);

    if (def.cmd.len > 0)
    {
        str8 cmd_path = resolve_path(char_dir, def.cmd, path_buf, sizeof(path_buf));
        usize cmd_size = 0;
        u8* cmd_data = track_file_data(mc, mel_vfs_read_file_alloc(opt.vfs, cmd_path, &cmd_size, opt.alloc));
        if (cmd_data)
        {
            str8 cmd_str = str8_from_parts(cmd_data, (size)cmd_size);
            mugen_cmd_load(&mc->cmd, cmd_str, opt.alloc);

            const char* needle = "[Statedef -1]";
            size needle_len = (size)strlen(needle);
            for (size i = 0; i + needle_len <= cmd_str.len; i++)
            {
                bool match = true;
                for (size j = 0; j < needle_len; j++)
                {
                    u8 a = cmd_str.data[i + j];
                    u8 b = (u8)needle[j];
                    if (a >= 'A' && a <= 'Z') a += 32;
                    if (b >= 'A' && b <= 'Z') b += 32;
                    if (a != b) { match = false; break; }
                }
                if (match)
                {
                    str8 cmd_cns_str = str8_from_parts(cmd_str.data + i, cmd_str.len - i);
                    mugen_cns_load(&mc->cmd_cns, cmd_cns_str, opt.alloc);
                    break;
                }
            }
        }
    }

    if (def.cns.len > 0)
    {
        str8 cns_path = resolve_path(char_dir, def.cns, path_buf, sizeof(path_buf));
        usize cns_size = 0;
        u8* cns_data = track_file_data(mc, mel_vfs_read_file_alloc(opt.vfs, cns_path, &cns_size, opt.alloc));
        if (cns_data)
        {
            str8 cns_str = str8_from_parts(cns_data, (size)cns_size);
            mugen_cns_load(&mc->cns, cns_str, opt.alloc);
            mc->cns_loaded = true;
        }
    }

    for (u32 i = 0; i < def.st_count; i++)
    {
        if (def.cns.len > 0 && def.st[i].len == def.cns.len
            && memcmp(def.st[i].data, def.cns.data, (size_t)def.cns.len) == 0)
            continue;

        str8 st_path = resolve_path(char_dir, def.st[i], path_buf, sizeof(path_buf));
        usize st_size = 0;
        u8* st_data = track_file_data(mc, mel_vfs_read_file_alloc(opt.vfs, st_path, &st_size, opt.alloc));
        if (st_data)
        {
            Mugen_Cns extra = {0};
            str8 st_str = str8_from_parts(st_data, (size)st_size);
            if (mugen_cns_load(&extra, st_str, opt.alloc))
            {
                if (!mc->cns_loaded)
                {
                    mc->cns = extra;
                    mc->cns_loaded = true;
                }
                else
                {
                    mugen_cns_merge(&mc->cns, &extra, opt.alloc);
                }
            }
        }
    }

    str8 stcommon_path = opt.stcommon_path;
    if (stcommon_path.len == 0 && def.stcommon.len > 0)
        stcommon_path = resolve_path(char_dir, def.stcommon, path_buf, sizeof(path_buf));

    if (stcommon_path.len > 0)
    {
        usize common_size = 0;
        u8* common_data = track_file_data(mc, mel_vfs_read_file_alloc(opt.vfs, stcommon_path, &common_size, opt.alloc));
        if (common_data)
        {
            str8 common_str = str8_from_parts(common_data, (size)common_size);
            mugen_cns_load(&mc->common_cns, common_str, opt.alloc);
        }
    }

    mel_anim_registry_init(opt.alloc);
    mel_slotmap_init(&mc->clip_pool, opt.alloc, .item_size = sizeof(Mel_Anim_Clip));

    mc->action_map = mel_alloc(opt.alloc, mc->air.action_count * sizeof(Fighter_Action_Map));
    mc->action_map_count = 0;

    for (u32 i = 0; i < mc->air.action_count; i++)
    {
        Mugen_Air_Action* action = &mc->air.actions[i];
        if (action->frame_count == 0) continue;

        Mel_Anim_Clip clip = mugen_air_compile(action, opt.alloc);
        Mel_Anim_Clip_Handle handle = mel_slotmap_insert(&mc->clip_pool, &clip);

        mc->action_map[mc->action_map_count] = (Fighter_Action_Map){
            .action_number = action->action_number,
            .clip = handle,
        };
        mc->action_map_count++;
    }

    mel_gpu_texture_init(&mc->tex, opt.dev,
        .pixels = mc->sff.atlas_pixels,
        .width = mc->sff.atlas_width,
        .height = mc->sff.atlas_height,
        .nearest_filter = true);
    mc->tex.descriptor = mel_gpu_pipeline_alloc_descriptor(&opt.sprite_pass->pipeline, opt.dev);
    mel_gpu_pipeline_write_texture(&opt.sprite_pass->pipeline, opt.dev,
        mc->tex.descriptor, mc->tex.image.view, mc->tex.sampler);
    mc->tex_handle = mel_texture_pool_register(opt.tex_pool, &mc->tex);

    mel_dealloc(opt.alloc, def_data);
    mc->loaded = true;
    SDL_Log("MUGEN char loaded: %u actions compiled, %u char states, %u common states",
        mc->action_map_count, mc->cns.statedef_count, mc->common_cns.statedef_count);
    return true;
}

void mugen_char_shutdown(Mugen_Char* mc, Mel_Gpu_Device* dev, const Mel_Alloc* alloc)
{
    if (!mc->loaded) return;

    mel_gpu_texture_shutdown(&mc->tex, dev);

    for (u32 i = 0; i < mc->action_map_count; i++)
    {
        Mel_Anim_Clip* clip = (Mel_Anim_Clip*)mel_slotmap_get(&mc->clip_pool, mc->action_map[i].clip);
        if (clip) mel_anim_clip_destroy(clip, alloc);
    }
    mel_slotmap_free(&mc->clip_pool);

    if (mc->action_map)
        mel_dealloc(alloc, mc->action_map);

    mugen_cmd_shutdown(&mc->cmd, alloc);

    for (u32 i = 0; i < mc->file_data_count; i++)
        mel_dealloc(alloc, mc->file_data[i]);

    mugen_air_shutdown(&mc->air, alloc);
    mugen_sff_shutdown(&mc->sff, alloc);

    mc->loaded = false;
}
