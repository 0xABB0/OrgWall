#include <log/log.h>
#include <core/compiler.h>

#if !MEL_LOG_DISABLED

#include <log.sink/sink.h>
#include <log.sink.console/sink.console.h>
#include <allocator/allocator.h>
#include <allocator.heap/heap.h>
#include <string/str8.h>
#include <thread/thread.h>
#include <thread/mutex.h>
#include <thread/cond.h>
#include <thread/rwlock.h>
#include <time/nano.h>

#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

typedef struct {
    str8 stack[MEL_LOG_MAX_CONTEXT_DEPTH];
    u32  depth;
    u64  global_frame;
    u64  sim_frame;
    u32  fixed_tick;
    bool in_log;
} Mel__Log_TLS;

static _Thread_local Mel__Log_TLS tls;

typedef struct {
    u32 value;
    str8 name;
} Mel__Log_Level_Entry;

typedef struct {
    Mel_Log_Sink* sink;
    u32 id;
    u32 gen;
} Mel__Log_Sink_Slot;

typedef struct {
    _Alignas(64) _Atomic(u64) write_cursor;
    _Alignas(64) _Atomic(u64) read_cursor;
    _Atomic(u64) dropped;
    u8* buf;
    u64 capacity;
} Mel__Log_Ring;

static Mel__Log_Ring ring;

static Mel__Log_Sink_Slot* sinks;
static u32 sink_count;
static u32 sink_capacity;
static u32 sink_next_id;
static Mel_RWLock sink_lock;

static Mel__Log_Level_Entry* levels;
static u32 level_count;
static u32 level_capacity;
static Mel_Mutex level_lock;

static Mel_Thread writer_thread;
static _Atomic(bool) writer_running;
static _Atomic(bool) writer_shutdown;
static _Atomic(bool) flush_requested;
static Mel_Mutex flush_mutex;
static Mel_Cond flush_cond;
static _Atomic(bool) flush_done;

static u64 mel__log_timestamp(void)
{
    return mel_nanos_since_unspecified_epoch();
}

static u32 mel__log_thread_id(void)
{
    return (u32)mel_thread_current_id();
}

static void mel__ring_init(Mel__Log_Ring* r, u64 capacity)
{
    const Mel_Alloc* heap = mel_alloc_heap();
    r->buf = mel_alloc(heap, capacity);
    assert(r->buf);
    r->capacity = capacity;
    atomic_store_explicit(&r->write_cursor, 0, memory_order_relaxed);
    atomic_store_explicit(&r->read_cursor, 0, memory_order_relaxed);
    atomic_store_explicit(&r->dropped, 0, memory_order_relaxed);
}

static void mel__ring_free(Mel__Log_Ring* r)
{
    mel_dealloc(mel_alloc_heap(), r->buf);
    r->buf = NULL;
    r->capacity = 0;
}

#define MEL__LOG_ENTRY_HEADER_SIZE ( \
    sizeof(u32) + \
    sizeof(u32) + \
    sizeof(u64) + \
    sizeof(u32) + \
    sizeof(u32) + \
    sizeof(u64) + \
    sizeof(u64) + \
    sizeof(u32) + \
    sizeof(u32) + \
    sizeof(u16) + \
    sizeof(u16) + \
    sizeof(u16) + \
    sizeof(u16)   \
)

static bool mel__ring_reserve(Mel__Log_Ring* r, u32 total_size, u64* out_pos)
{
    u64 pos = atomic_load_explicit(&r->write_cursor, memory_order_relaxed);

    for (;;)
    {
        u64 read = atomic_load_explicit(&r->read_cursor, memory_order_acquire);
        u64 used = pos - read;

        if (used + total_size > r->capacity)
        {
#if MEL_LOG_OVERFLOW_POLICY == 0
            atomic_fetch_add_explicit(&r->dropped, 1, memory_order_relaxed);
            return false;
#else
            pos = atomic_load_explicit(&r->write_cursor, memory_order_relaxed);
            continue;
#endif
        }

        if (atomic_compare_exchange_weak_explicit(&r->write_cursor, &pos, pos + total_size,
                                                   memory_order_acq_rel,
                                                   memory_order_relaxed))
        {
            *out_pos = pos;
            return true;
        }
    }
}

static void mel__ring_write(Mel__Log_Ring* r, u64 pos, const void* data, u32 size)
{
    u64 mask_pos = pos % r->capacity;
    u64 first = r->capacity - mask_pos;

    if (first >= size)
    {
        memcpy(r->buf + mask_pos, data, size);
    }
    else
    {
        memcpy(r->buf + mask_pos, data, (usize)first);
        memcpy(r->buf, (const u8*)data + first, size - (u32)first);
    }
}

static void mel__ring_read(Mel__Log_Ring* r, u64 pos, void* data, u32 size)
{
    u64 mask_pos = pos % r->capacity;
    u64 first = r->capacity - mask_pos;

    if (first >= size)
    {
        memcpy(data, r->buf + mask_pos, size);
    }
    else
    {
        memcpy(data, r->buf + mask_pos, (usize)first);
        memcpy((u8*)data + first, r->buf, size - (u32)first);
    }
}

static void mel__ring_commit(Mel__Log_Ring* r, u64 pos)
{
    u64 mask_pos = (pos + sizeof(u32)) % r->capacity;
    u32 flag = 1;

    if (mask_pos + sizeof(u32) <= r->capacity)
    {
        atomic_store_explicit((_Atomic(u32)*)(r->buf + mask_pos), flag, memory_order_release);
    }
    else
    {
        u8 tmp[4];
        memcpy(tmp, &flag, sizeof(u32));
        mel__ring_write(r, pos + sizeof(u32), tmp, sizeof(u32));
        atomic_thread_fence(memory_order_release);
    }
}

static bool mel__ring_is_committed(Mel__Log_Ring* r, u64 pos)
{
    u64 mask_pos = (pos + sizeof(u32)) % r->capacity;

    if (mask_pos + sizeof(u32) <= r->capacity)
    {
        u32 flag = atomic_load_explicit((_Atomic(u32)*)(r->buf + mask_pos), memory_order_acquire);
        return flag == 1;
    }
    else
    {
        atomic_thread_fence(memory_order_acquire);
        u8 tmp[4];
        mel__ring_read(r, pos + sizeof(u32), tmp, sizeof(u32));
        u32 flag;
        memcpy(&flag, tmp, sizeof(u32));
        return flag == 1;
    }
}

static void mel__ring_clear_committed(Mel__Log_Ring* r, u64 pos)
{
    u64 mask_pos = (pos + sizeof(u32)) % r->capacity;

    if (mask_pos + sizeof(u32) <= r->capacity)
    {
        atomic_store_explicit((_Atomic(u32)*)(r->buf + mask_pos), 0, memory_order_relaxed);
    }
    else
    {
        u32 zero = 0;
        mel__ring_write(r, pos + sizeof(u32), &zero, sizeof(u32));
    }
}

static str8 mel__serialize_context(char* buf, u32 buf_size)
{
    if (tls.depth == 0)
        return (str8){0};

    u32 written = 0;
    for (u32 i = 0; i < tls.depth; i++)
    {
        if (i > 0 && written < buf_size)
            buf[written++] = '/';

        u32 avail = buf_size - written;
        u32 copy = (u32)tls.stack[i].len < avail ? (u32)tls.stack[i].len : avail;
        memcpy(buf + written, tls.stack[i].data, copy);
        written += copy;
    }

    return (str8){(u8*)buf, written};
}

static void mel__push_entry(u32 level, str8 domain, const char* file, u32 line,
                            str8 message, str8 context, u64 timestamp,
                            u32 thread_id, u64 global_frame, u64 sim_frame, u32 fixed_tick)
{
    u16 domain_len = (u16)domain.len;
    u16 file_len = file ? (u16)strlen(file) : 0;
    u16 message_len = (u16)message.len;
    u16 context_len = (u16)context.len;
    u32 total_size = (u32)MEL__LOG_ENTRY_HEADER_SIZE + domain_len + file_len + message_len + context_len;
    total_size = (total_size + 3u) & ~3u;

    u64 pos;
    if (!mel__ring_reserve(&ring, total_size, &pos))
        return;

    u64 wp = pos;

    u32 committed = 0;
    mel__ring_write(&ring, wp, &total_size, sizeof(u32));    wp += sizeof(u32);
    mel__ring_write(&ring, wp, &committed, sizeof(u32));     wp += sizeof(u32);
    mel__ring_write(&ring, wp, &timestamp, sizeof(u64));     wp += sizeof(u64);
    mel__ring_write(&ring, wp, &level, sizeof(u32));         wp += sizeof(u32);
    mel__ring_write(&ring, wp, &thread_id, sizeof(u32));     wp += sizeof(u32);
    mel__ring_write(&ring, wp, &global_frame, sizeof(u64));  wp += sizeof(u64);
    mel__ring_write(&ring, wp, &sim_frame, sizeof(u64));     wp += sizeof(u64);
    mel__ring_write(&ring, wp, &fixed_tick, sizeof(u32));    wp += sizeof(u32);
    mel__ring_write(&ring, wp, &line, sizeof(u32));          wp += sizeof(u32);
    mel__ring_write(&ring, wp, &domain_len, sizeof(u16));    wp += sizeof(u16);
    mel__ring_write(&ring, wp, &file_len, sizeof(u16));      wp += sizeof(u16);
    mel__ring_write(&ring, wp, &message_len, sizeof(u16));   wp += sizeof(u16);
    mel__ring_write(&ring, wp, &context_len, sizeof(u16));   wp += sizeof(u16);

    if (domain_len)  { mel__ring_write(&ring, wp, domain.data, domain_len);       wp += domain_len; }
    if (file_len)    { mel__ring_write(&ring, wp, file, file_len);                wp += file_len; }
    if (message_len) { mel__ring_write(&ring, wp, message.data, message_len);     wp += message_len; }
    if (context_len) { mel__ring_write(&ring, wp, context.data, context_len);     wp += context_len; }

    mel__ring_commit(&ring, pos);
}

static bool mel__drain_one(void)
{
    u64 pos = atomic_load_explicit(&ring.read_cursor, memory_order_relaxed);
    u64 write = atomic_load_explicit(&ring.write_cursor, memory_order_acquire);

    if (pos == write)
        return false;

    if (!mel__ring_is_committed(&ring, pos))
        return false;

    u8 header[MEL__LOG_ENTRY_HEADER_SIZE];
    mel__ring_read(&ring, pos, header, (u32)MEL__LOG_ENTRY_HEADER_SIZE);

    u32 total_size;   memcpy(&total_size, header + 0, sizeof(u32));
    u64 timestamp;    memcpy(&timestamp, header + 8, sizeof(u64));
    u32 level;        memcpy(&level, header + 16, sizeof(u32));
    u32 thread_id;    memcpy(&thread_id, header + 20, sizeof(u32));
    u64 global_frame; memcpy(&global_frame, header + 24, sizeof(u64));
    u64 sim_frame;    memcpy(&sim_frame, header + 32, sizeof(u64));
    u32 fixed_tick;   memcpy(&fixed_tick, header + 40, sizeof(u32));
    u32 line;         memcpy(&line, header + 44, sizeof(u32));
    u16 domain_len;   memcpy(&domain_len, header + 48, sizeof(u16));
    u16 file_len;     memcpy(&file_len, header + 50, sizeof(u16));
    u16 message_len;  memcpy(&message_len, header + 52, sizeof(u16));
    u16 context_len;  memcpy(&context_len, header + 54, sizeof(u16));

    u32 payload_size = domain_len + file_len + message_len + context_len;
    u8 scratch[MEL_LOG_MAX_MESSAGE_SIZE * 2];
    assert(payload_size <= sizeof(scratch));

    mel__ring_read(&ring, pos + MEL__LOG_ENTRY_HEADER_SIZE, scratch, payload_size);

    u8* p = scratch;
    Mel_Log_Entry entry = {
        .timestamp_ns  = timestamp,
        .level         = level,
        .domain        = { p, domain_len },
        .message       = { p + domain_len + file_len, message_len },
        .file          = { p + domain_len, file_len },
        .line          = line,
        .thread_id     = thread_id,
        .global_frame  = global_frame,
        .sim_frame     = sim_frame,
        .fixed_tick    = fixed_tick,
        .context       = { p + domain_len + file_len + message_len, context_len },
    };

    mel__ring_clear_committed(&ring, pos);
    atomic_store_explicit(&ring.read_cursor, pos + total_size, memory_order_release);

    mel_rwlock_lock_shared(&sink_lock);
    for (u32 i = 0; i < sink_count; i++)
    {
        Mel__Log_Sink_Slot* slot = &sinks[i];
        if (slot->sink && entry.level <= slot->sink->level_threshold)
            slot->sink->write(slot->sink, &entry);
    }
    mel_rwlock_unlock_shared(&sink_lock);

    return true;
}

static void mel__drain_all(void)
{
    while (mel__drain_one()) {}

    u64 dropped = atomic_exchange_explicit(&ring.dropped, 0, memory_order_relaxed);
    if (dropped > 0)
    {
        char tmp[128];
        int n = snprintf(tmp, sizeof(tmp), "log: %" PRIu64 " entries dropped (ring buffer full)", dropped);
        if (n < 0) n = 0;
        if ((usize)n >= sizeof(tmp)) n = (int)sizeof(tmp) - 1;

        Mel_Log_Entry drop_entry = {
            .timestamp_ns = mel__log_timestamp(),
            .level = MEL_LOG_WARN,
            .domain = S8("log"),
            .message = { (u8*)tmp, n },
            .file = STR8_EMPTY,
            .line = 0,
            .thread_id = mel__log_thread_id(),
            .global_frame = 0,
            .sim_frame = 0,
            .fixed_tick = 0,
            .context = STR8_EMPTY,
        };

        mel_rwlock_lock_shared(&sink_lock);
        for (u32 i = 0; i < sink_count; i++)
        {
            Mel__Log_Sink_Slot* slot = &sinks[i];
            if (slot->sink && drop_entry.level <= slot->sink->level_threshold)
                slot->sink->write(slot->sink, &drop_entry);
        }
        mel_rwlock_unlock_shared(&sink_lock);
    }
}

static void mel__flush_sinks(void)
{
    mel_rwlock_lock_shared(&sink_lock);
    for (u32 i = 0; i < sink_count; i++)
    {
        if (sinks[i].sink && sinks[i].sink->flush)
            sinks[i].sink->flush(sinks[i].sink);
    }
    mel_rwlock_unlock_shared(&sink_lock);
}

static int mel__writer_thread_fn(void* arg)
{
    (void)arg;

    while (!atomic_load_explicit(&writer_shutdown, memory_order_acquire))
    {
        mel__drain_all();

        if (atomic_exchange_explicit(&flush_requested, false, memory_order_acq_rel))
        {
            mel__flush_sinks();
            atomic_store_explicit(&flush_done, true, memory_order_release);
            mel_mutex_lock(&flush_mutex);
            mel_cond_signal(&flush_cond);
            mel_mutex_unlock(&flush_mutex);
        }

        mel_thread_sleep(1000000);
    }

    mel__drain_all();
    mel__flush_sinks();

    return 0;
}

static void mel__ensure_writer_thread(void)
{
    bool expected = false;
    if (atomic_compare_exchange_strong_explicit(&writer_running, &expected, true,
                                                memory_order_acq_rel,
                                                memory_order_relaxed))
    {
        atomic_store_explicit(&writer_shutdown, false, memory_order_relaxed);
        bool ok = mel_thread_spawn(&writer_thread, mel__writer_thread_fn, NULL, .name = "mel_log_writer");
        assert(ok);
        (void)ok;
    }
}

void mel__log(u32 level, str8 domain, const char* file, u32 line, const char* fmt, ...)
{
    assert(!tls.in_log && "mel__log called recursively");
#ifdef NDEBUG
    if (tls.in_log) return;
#endif
    tls.in_log = true;

    static _Thread_local char scratch[MEL_LOG_MAX_MESSAGE_SIZE];

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(scratch, sizeof(scratch), fmt, args);
    va_end(args);

    if (n < 0) n = 0;
    if ((usize)n >= sizeof(scratch))
    {
        n = (int)sizeof(scratch) - 1;
        scratch[n - 3] = '.';
        scratch[n - 2] = '.';
        scratch[n - 1] = '.';
    }

    char ctx_buf[1024];
    str8 context = mel__serialize_context(ctx_buf, sizeof(ctx_buf));

    mel__push_entry(
        level, domain, file, line,
        (str8){ (u8*)scratch, n },
        context,
        mel__log_timestamp(),
        mel__log_thread_id(),
        tls.global_frame,
        tls.sim_frame,
        tls.fixed_tick
    );

    tls.in_log = false;
}

void mel_log_context_push(str8 tag)
{
    assert(tls.depth < MEL_LOG_MAX_CONTEXT_DEPTH);
    tls.stack[tls.depth++] = tag;
}

void mel_log_context_pop(void)
{
    assert(tls.depth > 0);
    tls.depth--;
}

void mel__log_context_cleanup(str8* tag)
{
    (void)tag;
    mel_log_context_pop();
}

void mel_log_set_global_frame(u64 frame)
{
    tls.global_frame = frame;
}

void mel_log_set_sim_frame(u64 frame)
{
    tls.sim_frame = frame;
}

void mel_log_set_fixed_tick(u32 tick)
{
    tls.fixed_tick = tick;
}

Mel_Log_Thread_State mel_log_thread_state_save(void)
{
    Mel_Log_Thread_State state = {
        .depth = tls.depth,
        .global_frame = tls.global_frame,
        .sim_frame = tls.sim_frame,
        .fixed_tick = tls.fixed_tick,
    };
    for (u32 i = 0; i < tls.depth; i++)
        state.stack[i] = tls.stack[i];
    return state;
}

void mel_log_thread_state_restore(Mel_Log_Thread_State state)
{
    tls.depth = state.depth;
    tls.global_frame = state.global_frame;
    tls.sim_frame = state.sim_frame;
    tls.fixed_tick = state.fixed_tick;
    for (u32 i = 0; i < state.depth; i++)
        tls.stack[i] = state.stack[i];
}

Mel_Log_Sink_Handle mel_log_sink_add(Mel_Log_Sink* sink)
{
    assert(sink);
    assert(sink->write);

    mel_rwlock_lock(&sink_lock);

    if (sink_count >= sink_capacity)
    {
        u32 new_cap = sink_capacity == 0 ? 4 : sink_capacity * 2;
        const Mel_Alloc* heap = mel_alloc_heap();
        Mel__Log_Sink_Slot* new_slots = mel_realloc(heap, sinks, sizeof(Mel__Log_Sink_Slot) * new_cap);
        assert(new_slots);
        sinks = new_slots;
        sink_capacity = new_cap;
    }

    u32 id = ++sink_next_id;
    u32 idx = sink_count++;
    sinks[idx] = (Mel__Log_Sink_Slot){
        .sink = sink,
        .id = id,
        .gen = 1,
    };

    Mel_Log_Sink_Handle handle = { .id = id, .gen = 1 };

    mel_rwlock_unlock(&sink_lock);

    mel__ensure_writer_thread();

    return handle;
}

void mel_log_sink_remove(Mel_Log_Sink_Handle handle)
{
    Mel_Log_Sink* removed = NULL;

    mel_rwlock_lock(&sink_lock);
    for (u32 i = 0; i < sink_count; i++)
    {
        if (sinks[i].id == handle.id && sinks[i].gen == handle.gen)
        {
            removed = sinks[i].sink;
            sinks[i] = sinks[--sink_count];
            break;
        }
    }
    mel_rwlock_unlock(&sink_lock);

    if (removed)
    {
        if (removed->flush)
            removed->flush(removed);
        if (removed->destroy)
            removed->destroy(removed);
    }
}

void mel_log_sink_flush_all(void)
{
    if (!atomic_load_explicit(&writer_running, memory_order_acquire))
        return;

    atomic_store_explicit(&flush_done, false, memory_order_release);
    atomic_store_explicit(&flush_requested, true, memory_order_release);

    mel_mutex_lock(&flush_mutex);
    while (!atomic_load_explicit(&flush_done, memory_order_acquire))
        mel_cond_wait(&flush_cond, &flush_mutex);
    mel_mutex_unlock(&flush_mutex);
}

void mel_log_level_register(u32 value, str8 name)
{
    mel_mutex_lock(&level_lock);

    for (u32 i = 0; i < level_count; i++)
    {
        if (levels[i].value == value)
        {
            levels[i].name = name;
            mel_mutex_unlock(&level_lock);
            return;
        }
    }

    if (level_count >= level_capacity)
    {
        u32 new_cap = level_capacity == 0 ? 8 : level_capacity * 2;
        const Mel_Alloc* heap = mel_alloc_heap();
        Mel__Log_Level_Entry* new_levels = mel_realloc(heap, levels, sizeof(Mel__Log_Level_Entry) * new_cap);
        assert(new_levels);
        levels = new_levels;
        level_capacity = new_cap;
    }

    levels[level_count++] = (Mel__Log_Level_Entry){ .value = value, .name = name };
    mel_mutex_unlock(&level_lock);
}

str8 mel_log_level_name(u32 value)
{
    mel_mutex_lock(&level_lock);
    for (u32 i = 0; i < level_count; i++)
    {
        if (levels[i].value == value)
        {
            str8 name = levels[i].name;
            mel_mutex_unlock(&level_lock);
            return name;
        }
    }
    mel_mutex_unlock(&level_lock);
    return S8("UNKNOWN");
}

void mel__log_signal(u32 level, const char* static_message)
{
    u16 msg_len = 0;
    const char* p = static_message;
    while (p && *p) { msg_len++; p++; }

    u32 total_size = (u32)MEL__LOG_ENTRY_HEADER_SIZE + msg_len;
    total_size = (total_size + 3u) & ~3u;

    u64 pos;
    if (!mel__ring_reserve(&ring, total_size, &pos))
        return;

    u64 wp = pos;

    u32 committed = 0;
    u64 timestamp = 0;
    u32 thread_id = 0;
    u64 zero64 = 0;
    u32 zero32 = 0;
    u16 context_len = 0;
    u16 domain_len = 0;
    u16 file_len = 0;

    mel__ring_write(&ring, wp, &total_size, sizeof(u32));    wp += sizeof(u32);
    mel__ring_write(&ring, wp, &committed, sizeof(u32));     wp += sizeof(u32);
    mel__ring_write(&ring, wp, &timestamp, sizeof(u64));     wp += sizeof(u64);
    mel__ring_write(&ring, wp, &level, sizeof(u32));         wp += sizeof(u32);
    mel__ring_write(&ring, wp, &thread_id, sizeof(u32));     wp += sizeof(u32);
    mel__ring_write(&ring, wp, &zero64, sizeof(u64));        wp += sizeof(u64);
    mel__ring_write(&ring, wp, &zero64, sizeof(u64));        wp += sizeof(u64);
    mel__ring_write(&ring, wp, &zero32, sizeof(u32));        wp += sizeof(u32);
    mel__ring_write(&ring, wp, &zero32, sizeof(u32));        wp += sizeof(u32);
    mel__ring_write(&ring, wp, &domain_len, sizeof(u16));    wp += sizeof(u16);
    mel__ring_write(&ring, wp, &file_len, sizeof(u16));      wp += sizeof(u16);
    mel__ring_write(&ring, wp, &msg_len, sizeof(u16));       wp += sizeof(u16);
    mel__ring_write(&ring, wp, &context_len, sizeof(u16));   wp += sizeof(u16);

    if (msg_len)
        mel__ring_write(&ring, wp, static_message, msg_len);

    mel__ring_commit(&ring, pos);
}

MEL_CONSTRUCTOR_PRIO(101)
static void mel__log_init(void)
{
    mel__ring_init(&ring, MEL_LOG_RING_SIZE);

    mel_rwlock_init(&sink_lock);
    mel_mutex_init(&level_lock, MEL_MUTEX_PLAIN);
    mel_mutex_init(&flush_mutex, MEL_MUTEX_PLAIN);
    mel_cond_init(&flush_cond);

    mel_log_level_register(MEL_LOG_FATAL, S8("FATAL"));
    mel_log_level_register(MEL_LOG_ERROR, S8("ERROR"));
    mel_log_level_register(MEL_LOG_WARN,  S8("WARN"));
    mel_log_level_register(MEL_LOG_INFO,  S8("INFO"));
    mel_log_level_register(MEL_LOG_DEBUG, S8("DEBUG"));
    mel_log_level_register(MEL_LOG_TRACE, S8("TRACE"));

    mel_log_sink_add(mel_log_sink_console_create(.color = true));
}

MEL_DESTRUCTOR_PRIO(101)
static void mel__log_shutdown(void)
{
    if (atomic_load_explicit(&writer_running, memory_order_acquire))
    {
        atomic_store_explicit(&writer_shutdown, true, memory_order_release);
        mel_thread_join(&writer_thread, NULL);
        atomic_store_explicit(&writer_running, false, memory_order_relaxed);
    }

    for (u32 i = 0; i < sink_count; i++)
    {
        if (sinks[i].sink)
        {
            if (sinks[i].sink->flush)
                sinks[i].sink->flush(sinks[i].sink);
            if (sinks[i].sink->destroy)
                sinks[i].sink->destroy(sinks[i].sink);
        }
    }

    mel_dealloc(mel_alloc_heap(), sinks);
    sinks = NULL;
    sink_count = 0;
    sink_capacity = 0;

    mel_dealloc(mel_alloc_heap(), levels);
    levels = NULL;
    level_count = 0;
    level_capacity = 0;

    mel__ring_free(&ring);

    mel_cond_destroy(&flush_cond);
    mel_mutex_destroy(&flush_mutex);
    mel_mutex_destroy(&level_lock);
    mel_rwlock_destroy(&sink_lock);
}

#endif
