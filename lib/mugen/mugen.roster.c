#include "mugen.roster.h"
#include "mugen.char.h"
#include "collection.slotmap.h"
#include "string.str8.h"
#include "allocator.h"
// ASYNC_V2: VFS removed
// #include "vfs.h"
// ASYNC_V2: removed, needs migration
// #include "async.task.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

void mugen_roster_init_opt(Mugen_Roster* r, Mugen_Roster_Init_Opt opt)
{
    memset(r, 0, sizeof(*r));
    r->vfs = opt.vfs;
    r->task_ctx = opt.task_ctx;
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

// ASYNC_V2: VFS removed — roster_find_def_cb, roster_scan_dirs_cb, roster_load_step all used VFS+Task APIs

// ASYNC_V2: removed, needs migration
// mugen_roster_load_opt deleted (returned Mel_Task_Handle, used VFS)

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
