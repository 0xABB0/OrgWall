#include <font/font.desc.h>
#include <string/str8.h>
#include <hash/xxh.h>
#include <collection.slotmap/slotmap.h>
#include <collection.map.hashmap/hashmap.h>
#include <allocator/allocator.h>
#include <allocator.heap/allocator.heap.h>
#include "vfs.h"
#include <log/log.h>

static Mel_SlotMap  s_pool;
static Mel_HashMap  s_path_map;
static bool         s_initialized;

static u64 mel__font_desc_hash_key(const void* key)
{
    u64 val = (u64)(usize)key;
    return mel_xxh64(&val, sizeof(val), 0);
}

static bool mel__font_desc_eq_key(const void* a, const void* b)
{
    return (u64)(usize)a == (u64)(usize)b;
}

static void mel__font_desc_ensure_init(void)
{
    if (s_initialized) return;
    s_initialized = true;

    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_slotmap_init(&s_pool, alloc, .item_size = sizeof(Mel_Font_Desc), .initial_capacity = 8);
    mel_hashmap_init(&s_path_map, mel__font_desc_hash_key, mel__font_desc_eq_key, alloc);
}

Mel_Font_Desc_Handle mel_font_desc_load_ttf(str8 path)
{
    assert(!str8_is_empty(path));
    mel__font_desc_ensure_init();

    u64 hash = str8_hash(path);
    void* existing = mel_hashmap_get(&s_path_map, (void*)(usize)hash);
    if (existing)
        return (Mel_Font_Desc_Handle){ .handle = mel_slotmap_handle_from_ptr(existing) };

    const Mel_Alloc* alloc = mel_alloc_heap();
    i64 fsize = 0;
    u8* ttf_data = mel_vfs_read_file(path, &fsize, alloc);
    if (!ttf_data)
    {
        mel_log_error("font.desc", "failed to read '%.*s'", (int)path.len, path.data);
        return MEL_FONT_DESC_HANDLE_NULL;
    }

    Mel_Font_Desc desc = {0};
    desc.data = ttf_data;
    desc.data_size = fsize;

    if (!stbtt_InitFont(&desc.info, ttf_data, 0))
    {
        mel_log_error("font.desc", "failed to parse '%.*s'", (int)path.len, path.data);
        mel_dealloc(alloc, ttf_data);
        return MEL_FONT_DESC_HANDLE_NULL;
    }

    stbtt_GetFontVMetrics(&desc.info, &desc.ascent, &desc.descent, &desc.line_gap);

    const u8* head = desc.info.data + desc.info.head;
    desc.units_per_em = (i32)((u32)head[18] << 8 | (u32)head[19]);

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&s_pool, &desc);
    mel_hashmap_put(&s_path_map, (void*)(usize)hash, mel_slotmap_handle_to_ptr(sm_handle));

    mel_log_info("font.desc", "loaded '%.*s' (upem=%d, ascent=%d, descent=%d)",
        (int)path.len, path.data, desc.units_per_em, desc.ascent, desc.descent);

    return (Mel_Font_Desc_Handle){ .handle = sm_handle };
}

Mel_Font_Desc* mel_font_desc_get(Mel_Font_Desc_Handle handle)
{
    mel__font_desc_ensure_init();
    return mel_slotmap_get(&s_pool, handle.handle);
}
