#include "mugen.roster.h"
#include "mugen.char.h"
#include "collection.slotmap.h"
#include "str8.h"
#include "allocator.h"
#include "vfs.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

void mugen_roster_init_opt(Mugen_Roster* r, Mugen_Roster_Init_Opt opt)
{
    memset(r, 0, sizeof(*r));
    r->stcommon_path = opt.stcommon_path;
    r->alloc = opt.alloc;

    mel_slotmap_init(&r->entries, opt.alloc,
        .item_size = sizeof(Mugen_Roster_Entry));
}

void mugen_roster_shutdown(Mugen_Roster* r)
{
    Mugen_Roster_Entry* data = mel_slotmap_data(&r->entries);
    u32 count = mel_slotmap_count(&r->entries);
    for (u32 i = 0; i < count; i++)
    {
        if (data[i].ch.loaded)
            mugen_char_shutdown(&data[i].ch, r->alloc);
        if (data[i].name.data)
            mel_dealloc(r->alloc, data[i].name.data);
    }
    mel_slotmap_free(&r->entries);
}

static str8 roster_dup_str(str8 s, const Mel_Alloc* alloc)
{
    u8* buf = mel_alloc(alloc, (usize)s.len + 1);
    memcpy(buf, s.data, (size_t)s.len);
    buf[s.len] = 0;
    return str8_from_parts(buf, s.len);
}

static str8 roster_last_component(str8 path)
{
    size last_slash = -1;
    for (size i = path.len - 1; i >= 0; i--)
    {
        if (path.data[i] == '/')
        { last_slash = i; break; }
    }
    if (last_slash < 0) return path;
    return str8_from_parts(path.data + last_slash + 1, path.len - last_slash - 1);
}

typedef struct {
    Mugen_Roster* roster;
    str8 folder_path;
} Roster_Load_Ctx;

typedef struct {
    Mugen_Roster* roster;
    str8 folder_path;
} Roster_Enum_Ctx;

static bool roster_scan_char_dir(str8 path, const Mel_Vfs_Stat* stat, void* user)
{
    Roster_Enum_Ctx* ctx = user;
    if (!(stat->flags & MEL_VFS_STAT_IS_DIR)) return true;

    str8 name = roster_last_component(path);

    char def_buf[512];
    size total = path.len + 1 + name.len + 4;
    if (total >= (size)sizeof(def_buf)) return true;

    memcpy(def_buf, path.data, (size_t)path.len);
    def_buf[path.len] = '/';
    memcpy(def_buf + path.len + 1, name.data, (size_t)name.len);
    memcpy(def_buf + path.len + 1 + name.len, ".def", 4);
    def_buf[total] = 0;

    str8 def_path = str8_from_parts((u8*)def_buf, total);
    if (!mel_vfs_exists(def_path)) return true;

    Mugen_Char ch;
    bool ok = mugen_char_load(&ch,
        .def_path = def_path,
        .stcommon_path = ctx->roster->stcommon_path,
        .alloc = ctx->roster->alloc);

    if (ok)
    {
        Mugen_Roster_Entry entry = {0};
        entry.name = roster_dup_str(name, ctx->roster->alloc);
        entry.ch = ch;
        mel_slotmap_insert(&ctx->roster->entries, &entry);
    }

    return true;
}

void mugen_roster_load(Mugen_Roster* r, str8 folder_path)
{
    assert(r);
    Roster_Enum_Ctx ctx = { .roster = r, .folder_path = folder_path };
    mel_vfs_enumerate(folder_path, roster_scan_char_dir, &ctx);
}

Mugen_Char* mugen_roster_find(Mugen_Roster* r, str8 name)
{
    Mugen_Roster_Entry* data = mel_slotmap_data(&r->entries);
    u32 count = mel_slotmap_count(&r->entries);
    for (u32 i = 0; i < count; i++)
    {
        if (str8_equals(data[i].name, name))
            return &data[i].ch;
    }
    return NULL;
}

Mugen_Char* mugen_roster_at(Mugen_Roster* r, u32 index)
{
    Mugen_Roster_Entry* data = mel_slotmap_data(&r->entries);
    u32 count = mel_slotmap_count(&r->entries);
    if (index >= count) return NULL;
    return &data[index].ch;
}

str8 mugen_roster_name_at(Mugen_Roster* r, u32 index)
{
    Mugen_Roster_Entry* data = mel_slotmap_data(&r->entries);
    u32 count = mel_slotmap_count(&r->entries);
    if (index >= count) return (str8){0};
    return data[index].name;
}

Mugen_Char* mugen_roster_get(Mugen_Roster* r, Mugen_Char_Handle h)
{
    Mugen_Roster_Entry* entry = mel_slotmap_get(&r->entries, h);
    if (!entry) return NULL;
    return &entry->ch;
}

u32 mugen_roster_count(Mugen_Roster* r)
{
    return mel_slotmap_count(&r->entries);
}
