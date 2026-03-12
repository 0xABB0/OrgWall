#include "mugen.roster.h"
#include "mugen.char.h"
#include "collection.slotmap.h"
#include "string.str8.h"
#include "allocator.h"
#include "vfs.h"
#include "async.task.h"
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

typedef struct {
    str8 def_path;
    str8 name;
    bool found;
} Roster_Def_Scan;

static bool roster_find_def_cb(str8 virtual_path, const Mel_Vfs_Stat* stat, void* user)
{
    (void)stat;
    Roster_Def_Scan* scan = user;
    if (scan->found) return true;

    size len = virtual_path.len;
    if (len >= 4
        && virtual_path.data[len - 4] == '.'
        && virtual_path.data[len - 3] == 'd'
        && virtual_path.data[len - 2] == 'e'
        && virtual_path.data[len - 1] == 'f')
    {
        scan->def_path = virtual_path;
        scan->found = true;
    }
    return true;
}

static bool roster_scan_dirs_cb(str8 virtual_path, const Mel_Vfs_Stat* stat, void* user)
{
    if (!(stat->flags & MEL_VFS_STAT_IS_DIR)) return true;

    Roster_Load_Ctx* ctx = user;
    Mugen_Roster* r = ctx->roster;

    str8 dirname = roster_last_component(virtual_path);
    if (dirname.len == 0) return true;

    char def_buf[512];
    size def_len = virtual_path.len + 1 + dirname.len + 4;
    if (def_len >= (size)sizeof(def_buf)) return true;

    memcpy(def_buf, virtual_path.data, (size_t)virtual_path.len);
    def_buf[virtual_path.len] = '/';
    memcpy(def_buf + virtual_path.len + 1, dirname.data, (size_t)dirname.len);
    memcpy(def_buf + virtual_path.len + 1 + dirname.len, ".def", 4);
    def_buf[def_len] = 0;

    str8 def_path = str8_from_parts((u8*)def_buf, def_len);

    if (!mel_vfs_exists(r->vfs, def_path))
    {
        Roster_Def_Scan scan = {0};
        mel_vfs_enumerate(r->vfs, virtual_path, roster_find_def_cb, &scan);
        if (!scan.found) return true;
        def_path = scan.def_path;
    }

    Mugen_Roster_Entry* existing_data = mel_slotmap_data(&r->entries);
    u32 existing_count = mel_slotmap_count(&r->entries);
    for (u32 i = 0; i < existing_count; i++)
    {
        if (str8_equals(existing_data[i].name, dirname))
        {
            if (existing_data[i].ch.loaded)
                mugen_char_shutdown(&existing_data[i].ch, r->alloc);
            mugen_char_load(&existing_data[i].ch,
                .vfs = r->vfs,
                .def_path = def_path,
                .stcommon_path = r->stcommon_path,
                .alloc = r->alloc);
            return true;
        }
    }

    Mugen_Roster_Entry entry = {0};
    entry.name = roster_dup_str(dirname, r->alloc);
    mugen_char_load(&entry.ch,
        .vfs = r->vfs,
        .def_path = def_path,
        .stcommon_path = r->stcommon_path,
        .alloc = r->alloc);

    mel_slotmap_insert(&r->entries, &entry);

    printf("ROSTER: loaded '%.*s' from %.*s\n",
        (int)dirname.len, (char*)dirname.data,
        (int)def_path.len, (char*)def_path.data);

    return true;
}

static Mel_Task_Step_Result roster_load_step(Mel_Task_Ctx* ctx, void* user_data)
{
    (void)ctx;
    Roster_Load_Ctx* lctx = user_data;

    mel_vfs_enumerate(lctx->roster->vfs, lctx->folder_path,
        roster_scan_dirs_cb, lctx, .include_dirs = true);

    return (Mel_Task_Step_Result){
        .result = MEL_TASK_STEP_DONE,
    };
}

Mel_Task_Handle mugen_roster_load_opt(Mugen_Roster* r, Mugen_Roster_Load_Opt opt)
{
    assert(r->task_ctx);

    Roster_Load_Ctx* lctx = mel_alloc(r->alloc, sizeof(Roster_Load_Ctx));
    lctx->roster = r;
    lctx->folder_path = opt.folder_path;

    Mel_Task_Handle h = mel_task_begin(r->task_ctx,
        .on_complete = opt.on_complete,
        .on_complete_user = opt.on_complete_user);
    mel_task_add_step(r->task_ctx, h, (Mel_Task_Step_Desc){ .fn = roster_load_step, .user_data = lctx });
    mel_task_submit(r->task_ctx, h);

    return h;
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
