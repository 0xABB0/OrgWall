#include "../melody/test.harness.h"
#include "../melody/log.h"
#include "../melody/log.sink.h"
#include "../melody/log.sink.test.h"
#include "../melody/log.sink.console.h"
#include "../melody/log.sink.file.h"
#include "../melody/log.sink.sqlite.h"
#include "../melody/string.str8.h"

#include <sqlite3.h>
#include <unistd.h>

static Mel_Log_Entry mel__test_make_entry(u32 level, const char* domain, const char* message)
{
    return (Mel_Log_Entry){
        .timestamp_ns  = 1000000000ULL,
        .level         = level,
        .domain        = str8_from_cstr(domain),
        .message       = str8_from_cstr(message),
        .file          = S8("test_log.c"),
        .line          = 1,
        .thread_id     = 0,
        .global_frame  = 42,
        .sim_frame     = 10,
        .fixed_tick    = 3,
        .context       = STR8_EMPTY,
    };
}

MEL_TEST(log_basic_smoke, .tags = "log")
{
    mel_log_info("test", "hello %d", 42);
    mel_log_error("test", "oops");
    mel_log_trace("test", "trace msg");
    mel_log_sink_flush_all();
}

MEL_TEST(log_level_registry_roundtrip, .tags = "log")
{
    mel_log_level_register(999, S8("CUSTOM"));
    str8 name = mel_log_level_name(999);
    MEL_ASSERT(str8_equals(name, S8("CUSTOM")));
}

MEL_TEST(log_level_unknown_for_unregistered, .tags = "log")
{
    str8 name = mel_log_level_name(12345);
    MEL_ASSERT(str8_equals(name, S8("UNKNOWN")));
}

MEL_TEST(log_predefined_levels_exist, .tags = "log")
{
    MEL_ASSERT(str8_equals(mel_log_level_name(MEL_LOG_FATAL), S8("FATAL")));
    MEL_ASSERT(str8_equals(mel_log_level_name(MEL_LOG_ERROR), S8("ERROR")));
    MEL_ASSERT(str8_equals(mel_log_level_name(MEL_LOG_WARN),  S8("WARN")));
    MEL_ASSERT(str8_equals(mel_log_level_name(MEL_LOG_INFO),  S8("INFO")));
    MEL_ASSERT(str8_equals(mel_log_level_name(MEL_LOG_DEBUG), S8("DEBUG")));
    MEL_ASSERT(str8_equals(mel_log_level_name(MEL_LOG_TRACE), S8("TRACE")));
}

MEL_TEST(log_context_stack_via_test_sink, .tags = "log")
{
    Mel_Log_Sink* sink = mel_test_log_sink();
    MEL_ASSERT_NOT_NULL(sink);

    mel_log_context_push(S8("physics"));
    mel_log_info("test", "inside physics scope");
    mel_log_context_pop();

    mel_log_sink_flush_all();

    MEL_ASSERT(mel_log_test_has_entry(sink, MEL_LOG_INFO, S8("test")));
}

MEL_TEST(log_nested_scope_produces_slash_context, .tags = "log")
{
    Mel_Log_Sink* sink = mel_test_log_sink();
    MEL_ASSERT_NOT_NULL(sink);

    {
        MEL_LOG_SCOPE("outer");
        {
            MEL_LOG_SCOPE("inner");
            mel_log_info("test", "nested");
        }
    }

    mel_log_sink_flush_all();

    MEL_ASSERT(mel_log_test_has_entry(sink, MEL_LOG_INFO, S8("test")));
}

MEL_TEST(log_frame_counters_captured, .tags = "log")
{
    mel_log_set_global_frame(100);
    mel_log_set_sim_frame(50);
    mel_log_set_fixed_tick(10);

    mel_log_info("test", "frame test");
    mel_log_sink_flush_all();

    Mel_Log_Sink* sink = mel_test_log_sink();
    MEL_ASSERT_NOT_NULL(sink);
    MEL_ASSERT(mel_log_test_has_entry(sink, MEL_LOG_INFO, S8("test")));

    mel_log_set_global_frame(0);
    mel_log_set_sim_frame(0);
    mel_log_set_fixed_tick(0);
}

MEL_TEST(log_test_sink_count, .tags = "log")
{
    Mel_Log_Sink* sink = mel_test_log_sink();
    MEL_ASSERT_NOT_NULL(sink);

    mel_log_error("test", "err1");
    mel_log_error("test", "err2");
    mel_log_error("test", "err3");
    mel_log_info("test", "info1");
    mel_log_sink_flush_all();

    MEL_ASSERT_EQ(mel_log_test_count(sink, MEL_LOG_ERROR), 3u);
    MEL_ASSERT_EQ(mel_log_test_count(sink, MEL_LOG_INFO), 1u);
    MEL_ASSERT_EQ(mel_log_test_count(sink, MEL_LOG_FATAL), 0u);
}

MEL_TEST(log_test_sink_clear, .tags = "log")
{
    Mel_Log_Sink* sink = mel_test_log_sink();
    MEL_ASSERT_NOT_NULL(sink);

    mel_log_info("test", "before clear");
    mel_log_sink_flush_all();
    MEL_ASSERT_EQ(mel_log_test_count(sink, MEL_LOG_INFO), 1u);

    mel_log_test_clear(sink);
    MEL_ASSERT_EQ(mel_log_test_count(sink, MEL_LOG_INFO), 0u);
}

MEL_TEST(log_test_sink_has_entry, .tags = "log")
{
    Mel_Log_Sink* sink = mel_test_log_sink();
    MEL_ASSERT_NOT_NULL(sink);

    mel_log_warn("gpu", "device lost");
    mel_log_sink_flush_all();

    MEL_ASSERT(mel_log_test_has_entry(sink, MEL_LOG_WARN, S8("gpu")));
    MEL_ASSERT(!mel_log_test_has_entry(sink, MEL_LOG_ERROR, S8("gpu")));
    MEL_ASSERT(!mel_log_test_has_entry(sink, MEL_LOG_WARN, S8("render")));
}

MEL_TEST(log_signal_no_crash, .tags = "log")
{
    mel_log_signal(MEL_LOG_FATAL, "test signal message");
    mel_log_sink_flush_all();
}

MEL_TEST(log_stress, .tags = "log")
{
    for (u32 i = 0; i < 500; i++)
        mel_log_trace("stress", "entry %u", i);

    mel_log_sink_flush_all();

    Mel_Log_Sink* sink = mel_test_log_sink();
    MEL_ASSERT_NOT_NULL(sink);
    MEL_ASSERT_GE(mel_log_test_count(sink, MEL_LOG_TRACE), 1u);
}

MEL_TEST(log_sink_console_create_destroy, .tags = "log")
{
    Mel_Log_Sink* sink = mel_log_sink_console_create(.color = false);
    MEL_ASSERT_NOT_NULL(sink);
    MEL_ASSERT_NOT_NULL(sink->write);
    MEL_ASSERT_NOT_NULL(sink->flush);
    MEL_ASSERT_NOT_NULL(sink->destroy);
    MEL_ASSERT_EQ(sink->level_threshold, MEL_LOG_TRACE);
    sink->destroy(sink);
}

MEL_TEST(log_sink_console_write_no_crash, .tags = "log")
{
    Mel_Log_Sink* sink = mel_log_sink_console_create(.color = false);
    Mel_Log_Entry entry = mel__test_make_entry(MEL_LOG_INFO, "test", "console write test");
    sink->write(sink, &entry);
    sink->flush(sink);
    sink->destroy(sink);
}

MEL_TEST(log_sink_console_write_color_no_crash, .tags = "log")
{
    Mel_Log_Sink* sink = mel_log_sink_console_create(.color = true);
    Mel_Log_Entry entry = mel__test_make_entry(MEL_LOG_ERROR, "gpu", "error with color");
    sink->write(sink, &entry);

    entry.context = S8("render/pass");
    sink->write(sink, &entry);

    sink->flush(sink);
    sink->destroy(sink);
}

MEL_TEST(log_sink_console_all_levels_no_crash, .tags = "log")
{
    Mel_Log_Sink* sink = mel_log_sink_console_create(.color = true);
    u32 levels[] = { MEL_LOG_FATAL, MEL_LOG_ERROR, MEL_LOG_WARN, MEL_LOG_INFO, MEL_LOG_DEBUG, MEL_LOG_TRACE };

    for (u32 i = 0; i < 6; i++)
    {
        Mel_Log_Entry entry = mel__test_make_entry(levels[i], "test", "level test");
        sink->write(sink, &entry);
    }

    sink->flush(sink);
    sink->destroy(sink);
}

MEL_TEST(log_sink_file_write_and_verify, .tags = "log")
{
    const char* path = "/tmp/mel_test_log_sink_file.log";
    unlink(path);

    Mel_Log_Sink* sink = mel_log_sink_file_create(.file_path = str8_from_cstr(path));
    MEL_ASSERT_NOT_NULL(sink);

    Mel_Log_Entry entry = mel__test_make_entry(MEL_LOG_WARN, "alloc", "leak detected");
    sink->write(sink, &entry);
    sink->flush(sink);

    FILE* f = fopen(path, "r");
    MEL_ASSERT_NOT_NULL(f);
    char buf[1024] = {0};
    usize bytes_read = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    MEL_ASSERT_GT(bytes_read, 0u);
    MEL_ASSERT(strstr(buf, "WARN") != NULL);
    MEL_ASSERT(strstr(buf, "alloc") != NULL);
    MEL_ASSERT(strstr(buf, "leak detected") != NULL);

    sink->destroy(sink);
    unlink(path);
}

MEL_TEST(log_sink_file_multiple_entries, .tags = "log")
{
    const char* path = "/tmp/mel_test_log_sink_file_multi.log";
    unlink(path);

    Mel_Log_Sink* sink = mel_log_sink_file_create(.file_path = str8_from_cstr(path));
    MEL_ASSERT_NOT_NULL(sink);

    for (u32 i = 0; i < 10; i++)
    {
        Mel_Log_Entry entry = mel__test_make_entry(MEL_LOG_INFO, "test", "line");
        sink->write(sink, &entry);
    }
    sink->flush(sink);

    FILE* f = fopen(path, "r");
    MEL_ASSERT_NOT_NULL(f);

    u32 line_count = 0;
    char line_buf[1024];
    while (fgets(line_buf, sizeof(line_buf), f))
        line_count++;
    fclose(f);

    MEL_ASSERT_EQ(line_count, 10u);

    sink->destroy(sink);
    unlink(path);
}

MEL_TEST(log_sink_file_context_in_output, .tags = "log")
{
    const char* path = "/tmp/mel_test_log_sink_file_ctx.log";
    unlink(path);

    Mel_Log_Sink* sink = mel_log_sink_file_create(.file_path = str8_from_cstr(path));
    MEL_ASSERT_NOT_NULL(sink);

    Mel_Log_Entry entry = mel__test_make_entry(MEL_LOG_DEBUG, "physics", "step done");
    entry.context = S8("sim/cloth");
    sink->write(sink, &entry);
    sink->flush(sink);

    FILE* f = fopen(path, "r");
    MEL_ASSERT_NOT_NULL(f);
    char buf[1024] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    MEL_ASSERT(strstr(buf, "sim/cloth") != NULL);
    MEL_ASSERT(strstr(buf, "step done") != NULL);

    sink->destroy(sink);
    unlink(path);
}

MEL_TEST(log_sink_file_threshold_respected, .tags = "log")
{
    const char* path = "/tmp/mel_test_log_sink_file_thresh.log";
    unlink(path);

    Mel_Log_Sink* sink = mel_log_sink_file_create(.file_path = str8_from_cstr(path));
    MEL_ASSERT_NOT_NULL(sink);
    sink->level_threshold = MEL_LOG_WARN;

    Mel_Log_Entry warn_entry = mel__test_make_entry(MEL_LOG_WARN, "test", "should appear");
    Mel_Log_Entry info_entry = mel__test_make_entry(MEL_LOG_INFO, "test", "should not appear");

    if (warn_entry.level <= sink->level_threshold)
        sink->write(sink, &warn_entry);
    if (info_entry.level <= sink->level_threshold)
        sink->write(sink, &info_entry);

    sink->flush(sink);

    FILE* f = fopen(path, "r");
    MEL_ASSERT_NOT_NULL(f);
    char buf[1024] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    MEL_ASSERT(strstr(buf, "should appear") != NULL);
    MEL_ASSERT(strstr(buf, "should not appear") == NULL);

    sink->destroy(sink);
    unlink(path);
}

MEL_TEST(log_sink_sqlite_create_and_schema, .tags = "log")
{
    const char* path = "/tmp/mel_test_log_sink_sqlite.db";
    unlink(path);

    Mel_Log_Sink* sink = mel_log_sink_sqlite_create(.db_path = str8_from_cstr(path));
    MEL_ASSERT_NOT_NULL(sink);

    sink->destroy(sink);

    sqlite3* db = NULL;
    int rc = sqlite3_open(path, &db);
    MEL_ASSERT_EQ(rc, SQLITE_OK);

    sqlite3_stmt* stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name", -1, &stmt, NULL);
    MEL_ASSERT_EQ(rc, SQLITE_OK);

    bool has_entries = false;
    bool has_metadata = false;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* name = (const char*)sqlite3_column_text(stmt, 0);
        if (strcmp(name, "entries") == 0) has_entries = true;
        if (strcmp(name, "metadata") == 0) has_metadata = true;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    MEL_ASSERT(has_entries);
    MEL_ASSERT(has_metadata);

    unlink(path);
}

MEL_TEST(log_sink_sqlite_metadata_populated, .tags = "log")
{
    const char* path = "/tmp/mel_test_log_sink_sqlite_meta.db";
    unlink(path);

    Mel_Log_Sink* sink = mel_log_sink_sqlite_create(.db_path = str8_from_cstr(path));
    MEL_ASSERT_NOT_NULL(sink);
    sink->destroy(sink);

    sqlite3* db = NULL;
    sqlite3_open(path, &db);

    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM metadata", -1, &stmt, NULL);
    sqlite3_step(stmt);
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    MEL_ASSERT_EQ(count, 3);

    unlink(path);
}

MEL_TEST(log_sink_sqlite_write_and_query, .tags = "log")
{
    const char* path = "/tmp/mel_test_log_sink_sqlite_write.db";
    unlink(path);

    Mel_Log_Sink* sink = mel_log_sink_sqlite_create(
        .db_path = str8_from_cstr(path),
        .batch_size = 10,
    );
    MEL_ASSERT_NOT_NULL(sink);

    Mel_Log_Entry entry = mel__test_make_entry(MEL_LOG_ERROR, "gpu", "pipeline failed");
    sink->write(sink, &entry);
    sink->flush(sink);
    sink->destroy(sink);

    sqlite3* db = NULL;
    sqlite3_open(path, &db);

    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(db, "SELECT level, level_name, domain, message, global_frame FROM entries", -1, &stmt, NULL);

    MEL_ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    MEL_ASSERT_EQ(sqlite3_column_int(stmt, 0), (int)MEL_LOG_ERROR);
    MEL_ASSERT_STR_EQ((const char*)sqlite3_column_text(stmt, 1), "ERROR");
    MEL_ASSERT_STR_EQ((const char*)sqlite3_column_text(stmt, 2), "gpu");
    MEL_ASSERT_STR_EQ((const char*)sqlite3_column_text(stmt, 3), "pipeline failed");
    MEL_ASSERT_EQ(sqlite3_column_int64(stmt, 4), 42);

    MEL_ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    unlink(path);
}

MEL_TEST(log_sink_sqlite_batch_commit, .tags = "log")
{
    const char* path = "/tmp/mel_test_log_sink_sqlite_batch.db";
    unlink(path);

    Mel_Log_Sink* sink = mel_log_sink_sqlite_create(
        .db_path = str8_from_cstr(path),
        .batch_size = 5,
        .flush_interval_ms = 60000,
    );
    MEL_ASSERT_NOT_NULL(sink);

    for (u32 i = 0; i < 20; i++)
    {
        Mel_Log_Entry entry = mel__test_make_entry(MEL_LOG_INFO, "batch", "item");
        sink->write(sink, &entry);
    }
    sink->flush(sink);
    sink->destroy(sink);

    sqlite3* db = NULL;
    sqlite3_open(path, &db);

    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM entries", -1, &stmt, NULL);
    sqlite3_step(stmt);
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    MEL_ASSERT_EQ(count, 20);

    unlink(path);
}

MEL_TEST(log_sink_sqlite_context_stored, .tags = "log")
{
    const char* path = "/tmp/mel_test_log_sink_sqlite_ctx.db";
    unlink(path);

    Mel_Log_Sink* sink = mel_log_sink_sqlite_create(.db_path = str8_from_cstr(path));
    MEL_ASSERT_NOT_NULL(sink);

    Mel_Log_Entry entry = mel__test_make_entry(MEL_LOG_TRACE, "render", "draw call");
    entry.context = S8("frame/pass");
    sink->write(sink, &entry);
    sink->flush(sink);
    sink->destroy(sink);

    sqlite3* db = NULL;
    sqlite3_open(path, &db);

    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(db, "SELECT context FROM entries WHERE context IS NOT NULL", -1, &stmt, NULL);
    MEL_ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    MEL_ASSERT_STR_EQ((const char*)sqlite3_column_text(stmt, 0), "frame/pass");
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    unlink(path);
}

MEL_TEST(log_sink_sqlite_null_context_when_empty, .tags = "log")
{
    const char* path = "/tmp/mel_test_log_sink_sqlite_nullctx.db";
    unlink(path);

    Mel_Log_Sink* sink = mel_log_sink_sqlite_create(.db_path = str8_from_cstr(path));
    MEL_ASSERT_NOT_NULL(sink);

    Mel_Log_Entry entry = mel__test_make_entry(MEL_LOG_INFO, "test", "no context");
    sink->write(sink, &entry);
    sink->flush(sink);
    sink->destroy(sink);

    sqlite3* db = NULL;
    sqlite3_open(path, &db);

    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(db, "SELECT context FROM entries", -1, &stmt, NULL);
    MEL_ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    MEL_ASSERT_EQ(sqlite3_column_type(stmt, 0), SQLITE_NULL);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    unlink(path);
}
