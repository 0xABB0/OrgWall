#include <log/log.h>

#if !MEL_LOG_DISABLED

#include <log.sink/sink.h>
#include <log.sink/sink.test.h>
#include <collection.array/array.h>
#include <allocator/heap.h>

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

typedef struct {
    Mel_Log_Entry entry;
    u8*           string_block;
} Mel_Log_Test_Captured;

typedef struct {
    Mel_Log_Sink base;
    Mel_Array(Mel_Log_Test_Captured) entries;
} Mel_Log_Sink_Test;

static str8 mel__log_test_copy_str8(u8* dst, str8 src)
{
    if (src.len <= 0) return (str8){0};
    memcpy(dst, src.data, (usize)src.len);
    return (str8){ .data = dst, .len = src.len };
}

static void mel__log_test_free_entries(Mel_Log_Sink_Test* test)
{
    const Mel_Alloc* heap = mel_alloc_heap();
    for (usize i = 0; i < test->entries.count; i++) {
        mel_dealloc(heap, test->entries.items[i].string_block);
    }
    mel_array_clear(&test->entries);
}

static void mel__log_sink_test_write(Mel_Log_Sink* self, const Mel_Log_Entry* entry)
{
    Mel_Log_Sink_Test* test = (Mel_Log_Sink_Test*)self;
    const Mel_Alloc* heap = mel_alloc_heap();

    usize total = (usize)entry->domain.len
                + (usize)entry->message.len
                + (usize)entry->file.len
                + (usize)entry->context.len;

    u8* block = NULL;
    if (total > 0) {
        block = mel_alloc(heap, total);
    }

    u8* cursor = block;
    Mel_Log_Test_Captured captured = {
        .entry = *entry,
        .string_block = block,
    };

    captured.entry.domain  = mel__log_test_copy_str8(cursor, entry->domain);
    cursor += (usize)entry->domain.len;

    captured.entry.message = mel__log_test_copy_str8(cursor, entry->message);
    cursor += (usize)entry->message.len;

    captured.entry.file    = mel__log_test_copy_str8(cursor, entry->file);
    cursor += (usize)entry->file.len;

    captured.entry.context = mel__log_test_copy_str8(cursor, entry->context);

    mel_array_push(&test->entries, captured);
}

static void mel__log_sink_test_flush(Mel_Log_Sink* self)
{
    (void)self;
}

static void mel__log_sink_test_destroy(Mel_Log_Sink* self)
{
    Mel_Log_Sink_Test* test = (Mel_Log_Sink_Test*)self;
    mel__log_test_free_entries(test);
    mel_array_free(&test->entries);
    mel_dealloc(mel_alloc_heap(), test);
}

Mel_Log_Sink* mel_log_sink_test_create(void)
{
    const Mel_Alloc* heap = mel_alloc_heap();
    Mel_Log_Sink_Test* test = mel_alloc_type(heap, Mel_Log_Sink_Test);
    test->base = (Mel_Log_Sink){
        .write           = mel__log_sink_test_write,
        .flush           = mel__log_sink_test_flush,
        .destroy         = mel__log_sink_test_destroy,
        .level_threshold = MEL_LOG_TRACE,
    };
    mel_array_init(&test->entries, heap);
    return &test->base;
}

void mel_log_test_clear(Mel_Log_Sink* sink)
{
    assert(sink);
    Mel_Log_Sink_Test* test = (Mel_Log_Sink_Test*)sink;
    mel__log_test_free_entries(test);
}

bool mel_log_test_has_entry(Mel_Log_Sink* sink, u32 level, str8 domain)
{
    assert(sink);
    Mel_Log_Sink_Test* test = (Mel_Log_Sink_Test*)sink;
    for (usize i = 0; i < test->entries.count; i++) {
        Mel_Log_Entry* e = &test->entries.items[i].entry;
        if (e->level == level && e->domain.len == domain.len
            && memcmp(e->domain.data, domain.data, (usize)domain.len) == 0) {
            return true;
        }
    }
    return false;
}

u32 mel_log_test_count(Mel_Log_Sink* sink, u32 level)
{
    assert(sink);
    Mel_Log_Sink_Test* test = (Mel_Log_Sink_Test*)sink;
    u32 n = 0;
    for (usize i = 0; i < test->entries.count; i++) {
        if (test->entries.items[i].entry.level == level) {
            n++;
        }
    }
    return n;
}

void mel_log_test_dump(Mel_Log_Sink* sink)
{
    assert(sink);
    Mel_Log_Sink_Test* test = (Mel_Log_Sink_Test*)sink;
    for (usize i = 0; i < test->entries.count; i++) {
        Mel_Log_Entry* e = &test->entries.items[i].entry;
        str8 level_name = mel_log_level_name(e->level);

        u64 secs = e->timestamp_ns / 1000000000ULL;
        u64 ms   = (e->timestamp_ns / 1000000ULL) % 1000ULL;

        if (e->context.len > 0) {
            fprintf(stderr,
                "[%"PRIu64".%03"PRIu64"] [%.*s] [%.*s] (%.*s) %.*s    (%.*s:%"PRIu32")\n",
                secs, ms,
                (int)level_name.len, level_name.data,
                (int)e->domain.len, e->domain.data,
                (int)e->context.len, e->context.data,
                (int)e->message.len, e->message.data,
                (int)e->file.len, e->file.data,
                e->line);
        } else {
            fprintf(stderr,
                "[%"PRIu64".%03"PRIu64"] [%.*s] [%.*s] %.*s    (%.*s:%"PRIu32")\n",
                secs, ms,
                (int)level_name.len, level_name.data,
                (int)e->domain.len, e->domain.data,
                (int)e->message.len, e->message.data,
                (int)e->file.len, e->file.data,
                e->line);
        }
    }
}

#endif
