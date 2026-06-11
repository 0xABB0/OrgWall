#include <compress/zip.h>

#include <allocator/allocator.h>
#include <collection/array.h>

#include <miniz.h>

#include <string.h>

typedef Mel_Array(Mel_Zip_Entry) Mel_Zip_Entry_List;

struct Mel_Zip_Reader
{
    const Mel_Alloc*   alloc;
    mz_zip_archive     za;
    Mel_Zip_Entry_List entries;
};

struct Mel_Zip_Writer
{
    const Mel_Alloc* alloc;
    mz_zip_archive   za;
    bool             live;
};

static void* zip_alloc_bridge(void* opaque, size_t items, size_t size) { return mel_alloc((const Mel_Alloc*)opaque, items * size); }

static void* zip_realloc_bridge(void* opaque, void* address, size_t items, size_t size)
{
    if (!address)
        return mel_alloc((const Mel_Alloc*)opaque, items * size);
    return mel_realloc((const Mel_Alloc*)opaque, address, items * size);
}

static void zip_free_bridge(void* opaque, void* address)
{
    if (address)
        mel_dealloc((const Mel_Alloc*)opaque, address);
}

static void zip_wire_alloc(mz_zip_archive* za, const Mel_Alloc* alloc)
{
    za->m_pAlloc = zip_alloc_bridge;
    za->m_pRealloc = zip_realloc_bridge;
    za->m_pFree = zip_free_bridge;
    za->m_pAlloc_opaque = (void*)alloc;
}

Mel_Zip_Reader* mel_zip_open(str8 bytes, const Mel_Alloc* alloc, Mel_Compress_Status* status)
{
    if (!alloc)
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
        return NULL;
    }
    Mel_Zip_Reader* r = mel_alloc_type(alloc, Mel_Zip_Reader);
    if (!r)
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
        return NULL;
    }
    memset(r, 0, sizeof *r);
    r->alloc = alloc;
    mel_array_init(&r->entries, alloc);
    zip_wire_alloc(&r->za, alloc);

    if (!mz_zip_reader_init_mem(&r->za, bytes.data, (size_t)bytes.len, 0))
    {
        mel_dealloc(alloc, r);
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_UNKNOWN_FORMAT;
        return NULL;
    }

    mz_uint n = mz_zip_reader_get_num_files(&r->za);
    for (mz_uint i = 0; i < n; i++)
    {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&r->za, i, &st))
        {
            mel_zip_close(r);
            *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_CORRUPT;
            return NULL;
        }
        Mel_Zip_Entry e = {
            .name = str8_dup_alloc(str8_from_cstr(st.m_filename), alloc),
            .size = st.m_uncomp_size,
            .csize = st.m_comp_size,
            .crc = st.m_crc32,
            .dir = mz_zip_reader_is_file_a_directory(&r->za, i) != 0,
        };
        mel_array_push(&r->entries, e);
    }

    *status = MEL_COMPRESS_OK;
    return r;
}

usize mel_zip_count(const Mel_Zip_Reader* r) { return r ? r->entries.count : 0; }

Mel_Zip_Entry mel_zip_entry(const Mel_Zip_Reader* r, usize index)
{
    if (!r || index >= r->entries.count)
        return (Mel_Zip_Entry){ 0 };
    return r->entries.items[index];
}

Mel_Compress_Result mel_zip_extract(Mel_Zip_Reader* r, usize index)
{
    Mel_Compress_Result res = { 0 };
    if (!r || index >= r->entries.count)
    {
        res.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
        return res;
    }
    size_t size = 0;
    void*  data = mz_zip_reader_extract_to_heap(&r->za, (mz_uint)index, &size, 0);
    if (!data)
    {
        res.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_CORRUPT;
        return res;
    }
    res.data = data;
    res.len = size;
    res.status = MEL_COMPRESS_OK;
    return res;
}

void mel_zip_close(Mel_Zip_Reader* r)
{
    if (!r)
        return;
    for (usize i = 0; i < r->entries.count; i++)
        if (r->entries.items[i].name.data)
            mel_dealloc(r->alloc, r->entries.items[i].name.data);
    mel_array_free(&r->entries);
    mz_zip_reader_end(&r->za);
    mel_dealloc(r->alloc, r);
}

Mel_Zip_Writer* mel_zip_writer_create(const Mel_Alloc* alloc, Mel_Compress_Status* status)
{
    if (!alloc)
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
        return NULL;
    }
    Mel_Zip_Writer* w = mel_alloc_type(alloc, Mel_Zip_Writer);
    if (!w)
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
        return NULL;
    }
    memset(w, 0, sizeof *w);
    w->alloc = alloc;
    zip_wire_alloc(&w->za, alloc);
    if (!mz_zip_writer_init_heap(&w->za, 0, 0))
    {
        mel_dealloc(alloc, w);
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
        return NULL;
    }
    w->live = true;
    *status = MEL_COMPRESS_OK;
    return w;
}

Mel_Compress_Status mel_zip_add(Mel_Zip_Writer* w, str8 name, str8 bytes, u32 level)
{
    if (!w || !w->live || str8_is_empty(name))
        return MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
    if (level > 9)
        return MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_LEVEL;
    const char* cname = str8_to_cstr_alloc(name, w->alloc);
    if (!cname)
        return MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
    mz_bool ok = mz_zip_writer_add_mem(&w->za, cname, bytes.data, (size_t)bytes.len, level);
    mel_dealloc(w->alloc, (void*)cname);
    if (!ok)
        return MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
    return MEL_COMPRESS_OK;
}

Mel_Compress_Result mel_zip_finish(Mel_Zip_Writer* w)
{
    Mel_Compress_Result res = { 0 };
    if (!w || !w->live)
    {
        res.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
        return res;
    }
    void*  buf = NULL;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&w->za, &buf, &size))
    {
        mz_zip_writer_end(&w->za);
        w->live = false;
        mel_dealloc(w->alloc, w);
        res.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
        return res;
    }
    mz_zip_writer_end(&w->za);
    w->live = false;
    const Mel_Alloc* alloc = w->alloc;
    mel_dealloc(alloc, w);
    res.data = buf;
    res.len = size;
    res.status = MEL_COMPRESS_OK;
    return res;
}
