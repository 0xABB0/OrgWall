#include <log/log.h>

#if !MEL_LOG_DISABLED

#include <log.sink/sink.h>
#include <log.sink.sqlite/sink.sqlite.h>
#include <allocator/allocator.h>
#include <allocator.heap/heap.h>
#include <string/str8.h>
#include <core/platform.h>
#include <time/nano.h>

#include <sqlite3.h>
#include <stdio.h>
#include <time.h>
#include <inttypes.h>

typedef struct {
    Mel_Log_Sink    base;
    sqlite3*        db;
    sqlite3_stmt*   insert_stmt;
    u32             batch_size;
    u32             flush_interval_ms;
    u32             pending_count;
    bool            in_transaction;
    u64             last_commit_ns;
} Mel_Log_Sink_Sqlite;

static u64 mel__sqlite_sink_now_ns(void)
{
    return (u64)mel_nanos_since_unspecified_epoch();
}

static bool mel__sqlite_sink_exec(sqlite3* db, const char* sql)
{
    char* err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[mel_log_sink_sqlite] exec failed: %s (sql: %s)\n",
                err ? err : sqlite3_errmsg(db), sql);
        sqlite3_free(err);
        return false;
    }
    return true;
}

static void mel__sqlite_sink_begin(Mel_Log_Sink_Sqlite* s)
{
    if (s->in_transaction) return;
    if (mel__sqlite_sink_exec(s->db, "BEGIN")) {
        s->in_transaction = true;
    }
}

static void mel__sqlite_sink_commit(Mel_Log_Sink_Sqlite* s)
{
    if (!s->in_transaction) return;
    mel__sqlite_sink_exec(s->db, "COMMIT");
    s->in_transaction = false;
    s->pending_count = 0;
    s->last_commit_ns = mel__sqlite_sink_now_ns();
}

static void mel__sqlite_sink_write(Mel_Log_Sink* self, const Mel_Log_Entry* entry)
{
    Mel_Log_Sink_Sqlite* s = (Mel_Log_Sink_Sqlite*)self;

    if (!s->in_transaction) {
        mel__sqlite_sink_begin(s);
    }

    sqlite3_stmt* stmt = s->insert_stmt;

    str8 level_name = mel_log_level_name(entry->level);

    sqlite3_bind_int64(stmt, 1, (i64)entry->timestamp_ns);
    sqlite3_bind_int(stmt, 2, (int)entry->level);
    sqlite3_bind_text(stmt, 3, (const char*)level_name.data, (int)level_name.len, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, (const char*)entry->domain.data, (int)entry->domain.len, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, (const char*)entry->message.data, (int)entry->message.len, SQLITE_TRANSIENT);

    if (entry->file.len > 0) {
        sqlite3_bind_text(stmt, 6, (const char*)entry->file.data, (int)entry->file.len, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 6);
    }

    sqlite3_bind_int(stmt, 7, (int)entry->line);
    sqlite3_bind_int(stmt, 8, (int)entry->thread_id);
    sqlite3_bind_int64(stmt, 9, (i64)entry->global_frame);
    sqlite3_bind_int64(stmt, 10, (i64)entry->sim_frame);
    sqlite3_bind_int(stmt, 11, (int)entry->fixed_tick);

    if (entry->context.len > 0) {
        sqlite3_bind_text(stmt, 12, (const char*)entry->context.data, (int)entry->context.len, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 12);
    }

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[mel_log_sink_sqlite] insert failed: %s\n", sqlite3_errmsg(s->db));
    }
    sqlite3_reset(stmt);

    s->pending_count++;

    bool batch_full = s->pending_count >= s->batch_size;
    u64 now = mel__sqlite_sink_now_ns();
    u64 elapsed_ms = (now - s->last_commit_ns) / 1000000ULL;
    bool timer_expired = elapsed_ms >= s->flush_interval_ms;

    if (batch_full || timer_expired) {
        mel__sqlite_sink_commit(s);
    }
}

static void mel__sqlite_sink_flush(Mel_Log_Sink* self)
{
    Mel_Log_Sink_Sqlite* s = (Mel_Log_Sink_Sqlite*)self;
    if (s->pending_count > 0) {
        mel__sqlite_sink_commit(s);
    }
}

static void mel__sqlite_sink_destroy(Mel_Log_Sink* self)
{
    Mel_Log_Sink_Sqlite* s = (Mel_Log_Sink_Sqlite*)self;
    if (s->insert_stmt) {
        sqlite3_finalize(s->insert_stmt);
    }
    if (s->db) {
        sqlite3_close(s->db);
    }
    mel_dealloc(mel_alloc_heap(), s);
}

static const char* mel__sqlite_sink_platform_string(void)
{
#if MEL_PLATFORM_OSX && MEL_CPU_ARM
    return "macOS/arm64";
#elif MEL_PLATFORM_OSX && MEL_CPU_X86
    return "macOS/x86_64";
#elif MEL_PLATFORM_LINUX && MEL_CPU_ARM
    return "Linux/arm64";
#elif MEL_PLATFORM_LINUX && MEL_CPU_X86
    return "Linux/x86_64";
#elif MEL_PLATFORM_WINDOWS && MEL_CPU_ARM
    return "Windows/arm64";
#elif MEL_PLATFORM_WINDOWS && MEL_CPU_X86
    return "Windows/x86_64";
#else
    return "unknown";
#endif
}

static bool mel__sqlite_sink_init_schema(sqlite3* db)
{
    if (!mel__sqlite_sink_exec(db, "PRAGMA journal_mode=WAL")) return false;
    if (!mel__sqlite_sink_exec(db, "PRAGMA synchronous=NORMAL")) return false;

    if (!mel__sqlite_sink_exec(db,
        "CREATE TABLE IF NOT EXISTS entries ("
        "id              INTEGER PRIMARY KEY,"
        "timestamp_ns    INTEGER NOT NULL,"
        "level           INTEGER NOT NULL,"
        "level_name      TEXT NOT NULL,"
        "domain          TEXT NOT NULL,"
        "message         TEXT NOT NULL,"
        "file            TEXT,"
        "line            INTEGER,"
        "thread_id       INTEGER,"
        "global_frame    INTEGER,"
        "sim_frame       INTEGER,"
        "fixed_tick      INTEGER,"
        "context         TEXT"
        ")")) return false;

    if (!mel__sqlite_sink_exec(db, "CREATE INDEX IF NOT EXISTS idx_entries_level ON entries(level)")) return false;
    if (!mel__sqlite_sink_exec(db, "CREATE INDEX IF NOT EXISTS idx_entries_domain ON entries(domain)")) return false;
    if (!mel__sqlite_sink_exec(db, "CREATE INDEX IF NOT EXISTS idx_entries_timestamp ON entries(timestamp_ns)")) return false;
    if (!mel__sqlite_sink_exec(db, "CREATE INDEX IF NOT EXISTS idx_entries_global_frame ON entries(global_frame)")) return false;

    if (!mel__sqlite_sink_exec(db,
        "CREATE TABLE IF NOT EXISTS metadata ("
        "key   TEXT PRIMARY KEY,"
        "value TEXT NOT NULL"
        ")")) return false;

    return true;
}

static bool mel__sqlite_sink_insert_metadata(sqlite3* db)
{
    time_t now = time(NULL);
    struct tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &now);
#else
    gmtime_r(&now, &tm_buf);
#endif
    char iso_buf[64];
    strftime(iso_buf, sizeof(iso_buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);

    const char* platform = mel__sqlite_sink_platform_string();

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO metadata(key, value) VALUES (?1, ?2)",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[mel_log_sink_sqlite] metadata prepare failed: %s\n", sqlite3_errmsg(db));
        return false;
    }

    const char* keys[]   = { "run_start",  "platform", "engine_version" };
    const char* values[] = { iso_buf,       platform,   "melody-dev"     };

    for (int i = 0; i < 3; i++) {
        sqlite3_bind_text(stmt, 1, keys[i],   -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, values[i],  -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "[mel_log_sink_sqlite] metadata insert failed for '%s': %s\n",
                    keys[i], sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    return true;
}

Mel_Log_Sink* mel_log_sink_sqlite_create_opt(Mel_Log_Sink_Sqlite_Opt opt)
{
    u32 batch_size = opt.batch_size > 0 ? opt.batch_size : 100;
    u32 flush_interval_ms = opt.flush_interval_ms > 0 ? opt.flush_interval_ms : 1000;

    char path_buf[4096];
    str8_to_buf(opt.db_path, path_buf, sizeof(path_buf));

    sqlite3* db = NULL;
    int rc = sqlite3_open(path_buf, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[mel_log_sink_sqlite] failed to open database '%s': %s\n",
                path_buf, db ? sqlite3_errmsg(db) : "unknown error");
        if (db) sqlite3_close(db);
        return NULL;
    }

    if (!mel__sqlite_sink_init_schema(db)) {
        sqlite3_close(db);
        return NULL;
    }

    if (!mel__sqlite_sink_insert_metadata(db)) {
        sqlite3_close(db);
        return NULL;
    }

    sqlite3_stmt* insert_stmt = NULL;
    rc = sqlite3_prepare_v2(db,
        "INSERT INTO entries("
        "timestamp_ns, level, level_name, domain, message, "
        "file, line, thread_id, global_frame, sim_frame, fixed_tick, context"
        ") VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)",
        -1, &insert_stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[mel_log_sink_sqlite] failed to prepare insert: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return NULL;
    }

    Mel_Log_Sink_Sqlite* s = mel_alloc_type(mel_alloc_heap(), Mel_Log_Sink_Sqlite);
    *s = (Mel_Log_Sink_Sqlite){
        .base = {
            .write           = mel__sqlite_sink_write,
            .flush           = mel__sqlite_sink_flush,
            .destroy         = mel__sqlite_sink_destroy,
            .level_threshold = MEL_LOG_TRACE,
        },
        .db                = db,
        .insert_stmt       = insert_stmt,
        .batch_size        = batch_size,
        .flush_interval_ms = flush_interval_ms,
        .last_commit_ns    = mel__sqlite_sink_now_ns(),
    };

    return &s->base;
}

#endif
