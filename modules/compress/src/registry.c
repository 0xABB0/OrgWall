#include <compress/registry.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <log/log.h>

typedef Mel_Array(const Mel_Compress_Codec*) Mel_Compress_Codec_List;

static struct
{
    Mel_Compress_Codec_List codecs;
    bool                    ready;
} g_registry;

void mel_compress_registry_init(const Mel_Alloc* alloc)
{
    if (g_registry.ready)
    {
        mel_log_warn("compress", "registry_init: already initialized; ignoring");
        return;
    }
    mel_array_init(&g_registry.codecs, alloc);
    g_registry.ready = true;
}

void mel_compress_registry_shutdown(void)
{
    if (!g_registry.ready)
        return;
    mel_array_free(&g_registry.codecs);
    g_registry.ready = false;
}

void mel_compress_register(const Mel_Compress_Codec* codec)
{
    if (!g_registry.ready)
    {
        mel_log_error("compress", "register: registry not initialized");
        return;
    }
    if (!codec)
    {
        mel_log_error("compress", "register: NULL codec");
        return;
    }
    for (usize i = 0; i < g_registry.codecs.count; i++)
    {
        if (str8_equals(g_registry.codecs.items[i]->id, codec->id))
        {
            mel_log_warn("compress", "register: codec '%.*s' already registered; ignoring", (int)codec->id.len, codec->id.data);
            return;
        }
    }
    mel_array_push(&g_registry.codecs, codec);
    mel_log_debug("compress", "registered codec '%.*s'", (int)codec->id.len, codec->id.data);
}

usize mel_compress_count(void) { return g_registry.ready ? g_registry.codecs.count : 0; }

const Mel_Compress_Codec* mel_compress_at(usize index)
{
    if (!g_registry.ready || index >= g_registry.codecs.count)
        return NULL;
    return g_registry.codecs.items[index];
}

const Mel_Compress_Codec* mel_compress_find(str8 id)
{
    if (!g_registry.ready)
        return NULL;
    for (usize i = 0; i < g_registry.codecs.count; i++)
        if (str8_equals(g_registry.codecs.items[i]->id, id))
            return g_registry.codecs.items[i];
    return NULL;
}

const Mel_Compress_Codec* mel_compress_sniff(str8 head)
{
    if (!g_registry.ready)
        return NULL;
    for (usize i = 0; i < g_registry.codecs.count; i++)
    {
        const Mel_Compress_Codec* c = g_registry.codecs.items[i];
        if (c->sniff && c->sniff(head))
            return c;
    }
    return NULL;
}

const Mel_Compress_Codec* mel_compress_for_ext(str8 ext)
{
    if (!g_registry.ready)
        return NULL;
    for (usize i = 0; i < g_registry.codecs.count; i++)
        if (str8_ieq(g_registry.codecs.items[i]->ext, ext))
            return g_registry.codecs.items[i];
    return NULL;
}
