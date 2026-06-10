#include <io/io.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <vat/vat.h>
#include <future/future.h>
#include <thread/thread.h>
#include <executor/executor.h>
#include <test/test.h>

#include <collection/list.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

MEL_TEST(io, const_memory_reads_back_bytes)
{
    const char* msg = "hello-io";
    Mel_Stream* s = mel_io_memory_const(.buffer = msg, .len = strlen(msg));
    MEL_REQUIRE_NOT_NULL(s);

    char          buf[16] = { 0 };
    Mel_IO_Result r = mel_stream_read_sync(s, buf, 4, MEL_IO_NO_OFFSET);
    MEL_EXPECT_EQ((i64)r.bytes_transferred, (i64)4);
    MEL_EXPECT(memcmp(buf, "hell", 4) == 0);
    MEL_EXPECT_EQ((i64)mel_stream_tell(s), (i64)4);

    Mel_IO_Result r2 = mel_stream_read_sync(s, buf, 16, MEL_IO_NO_OFFSET);
    MEL_EXPECT_EQ((i64)r2.bytes_transferred, (i64)4);
    MEL_EXPECT(mel_io_status_eof(r2.status));
    mel_stream_destroy(s);
}

MEL_TEST(io, const_memory_rejects_write)
{
    const char* msg = "ro";
    Mel_Stream* s = mel_io_memory_const(.buffer = msg, .len = strlen(msg));
    MEL_REQUIRE_NOT_NULL(s);
    Mel_IO_Result r = mel_stream_write_sync(s, "x", 1, MEL_IO_NO_OFFSET);
    MEL_EXPECT(mel_io_status_failed(r.status));
    MEL_EXPECT((r.status & MEL_IO_READ_ONLY) != 0u);
    mel_stream_destroy(s);
}

MEL_TEST(io, fixed_memory_write_then_seek_read)
{
    u8          backing[8] = { 0 };
    Mel_Stream* s = mel_io_memory_fixed(.buffer = backing, .len = sizeof backing);
    MEL_REQUIRE_NOT_NULL(s);

    MEL_EXPECT_EQ(mel_stream_write_all(s, "ABCD", 4), MEL_IO_OK);
    i64 pos = 0;
    MEL_EXPECT_EQ(mel_stream_seek(s, 0, MEL_IO_SEEK_SET, &pos), MEL_IO_OK);
    MEL_EXPECT_EQ((i64)pos, (i64)0);

    char buf[5] = { 0 };
    MEL_EXPECT_EQ(mel_stream_read_exact(s, buf, 4), MEL_IO_OK);
    MEL_EXPECT(memcmp(buf, "ABCD", 4) == 0);

    i64 sz = 0;
    MEL_EXPECT_EQ(mel_stream_size(s, &sz), MEL_IO_OK);
    MEL_EXPECT_EQ((i64)sz, (i64)8);
    mel_stream_destroy(s);
}

MEL_TEST(io, growable_grows_and_detaches)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Stream*      s = mel_io_memory_growable(.initial_capacity = 2, .alloc = alloc);
    MEL_REQUIRE_NOT_NULL(s);

    const char* payload = "growable-stream-payload";
    MEL_EXPECT_EQ(mel_stream_write_all(s, payload, strlen(payload)), MEL_IO_OK);
    MEL_EXPECT_EQ((i64)mel_io_growable_len(s), (i64)strlen(payload));
    MEL_EXPECT(memcmp(mel_io_growable_data(s), payload, strlen(payload)) == 0);

    usize n = 0;
    void* base = NULL;
    MEL_EXPECT(mel_stream_native_memory(s, &base, &n));
    MEL_EXPECT_EQ((i64)n, (i64)strlen(payload));

    usize out_len = 0;
    void* detached = mel_io_growable_detach(s, &out_len);
    MEL_REQUIRE_NOT_NULL(detached);
    MEL_EXPECT_EQ((i64)out_len, (i64)strlen(payload));
    MEL_EXPECT(memcmp(detached, payload, strlen(payload)) == 0);
    mel_dealloc(alloc, detached);
    mel_stream_destroy(s);
}

MEL_TEST(io, endian_helpers_roundtrip)
{
    Mel_Stream* s = mel_io_memory_growable(.initial_capacity = 32);
    MEL_REQUIRE_NOT_NULL(s);

    MEL_EXPECT_EQ(mel_stream_write_u16_le(s, 0x1234), MEL_IO_OK);
    MEL_EXPECT_EQ(mel_stream_write_u16_be(s, 0x1234), MEL_IO_OK);
    MEL_EXPECT_EQ(mel_stream_write_u32_le(s, 0xDEADBEEFu), MEL_IO_OK);
    MEL_EXPECT_EQ(mel_stream_write_u64_be(s, 0x0102030405060708ull), MEL_IO_OK);

    const u8* raw = (const u8*)mel_io_growable_data(s);
    MEL_EXPECT_EQ((i64)raw[0], (i64)0x34);
    MEL_EXPECT_EQ((i64)raw[1], (i64)0x12);
    MEL_EXPECT_EQ((i64)raw[2], (i64)0x12);
    MEL_EXPECT_EQ((i64)raw[3], (i64)0x34);

    i64 pos = 0;
    mel_stream_seek(s, 0, MEL_IO_SEEK_SET, &pos);

    u16 a = 0, bbe = 0;
    u32 c = 0;
    u64 d = 0;
    MEL_EXPECT_EQ(mel_stream_read_u16_le(s, &a), MEL_IO_OK);
    MEL_EXPECT_EQ(mel_stream_read_u16_be(s, &bbe), MEL_IO_OK);
    MEL_EXPECT_EQ(mel_stream_read_u32_le(s, &c), MEL_IO_OK);
    MEL_EXPECT_EQ(mel_stream_read_u64_be(s, &d), MEL_IO_OK);
    MEL_EXPECT_EQ((i64)a, (i64)0x1234);
    MEL_EXPECT_EQ((i64)bbe, (i64)0x1234);
    MEL_EXPECT_EQ((i64)c, (i64)0xDEADBEEFu);
    MEL_EXPECT((u64)d == 0x0102030405060708ull);
    mel_stream_destroy(s);
}

typedef struct
{
    const Mel_Alloc* inner;
    usize            calls;
} Counting_Alloc;

static void* counting_cb(void* ptr, usize size, u32 align, const char* file, const char* func, u32 line, void* user)
{
    Counting_Alloc* c = (Counting_Alloc*)user;
    c->calls++;
    const Mel_Alloc* in = c->inner;
    if (ptr == NULL)
        return align ? in->alloc_cb(NULL, size, align, file, func, line, in->user_data) : mel__alloc(in, size, file, func, line);
    if (size == 0)
    {
        in->alloc_cb(ptr, 0, align, file, func, line, in->user_data);
        return NULL;
    }
    return in->alloc_cb(ptr, size, align, file, func, line, in->user_data);
}

MEL_TEST(io, sync_ops_do_not_allocate)
{
    Counting_Alloc state = { .inner = mel_alloc_heap(), .calls = 0 };
    Mel_Alloc      counting = { .alloc_cb = counting_cb, .user_data = &state };

    u8          backing[64] = { 0 };
    Mel_Stream* s = mel_io_memory_fixed(.buffer = backing, .len = sizeof backing, .alloc = &counting);
    MEL_REQUIRE_NOT_NULL(s);

    usize baseline = state.calls;

    MEL_EXPECT_EQ(mel_stream_write_u32_le(s, 0xDEADBEEFu), MEL_IO_OK);
    MEL_EXPECT_EQ(mel_stream_write_u64_be(s, 0x0102030405060708ull), MEL_IO_OK);
    char src[8] = "abcdefgh";
    MEL_EXPECT_EQ(mel_stream_write_all(s, src, sizeof src), MEL_IO_OK);

    i64 pos = 0;
    MEL_EXPECT_EQ(mel_stream_seek(s, 0, MEL_IO_SEEK_SET, &pos), MEL_IO_OK);

    u32  a = 0;
    u64  b = 0;
    char dst[8] = { 0 };
    MEL_EXPECT_EQ(mel_stream_read_u32_le(s, &a), MEL_IO_OK);
    MEL_EXPECT_EQ(mel_stream_read_u64_be(s, &b), MEL_IO_OK);
    MEL_EXPECT_EQ(mel_stream_read_exact(s, dst, sizeof dst), MEL_IO_OK);
    for (int i = 0; i < 32; i++)
    {
        Mel_IO_Result r = mel_stream_read_sync(s, dst, 0, 0);
        (void)r;
    }

    MEL_EXPECT_EQ((i64)a, (i64)0xDEADBEEFu);
    MEL_EXPECT((u64)b == 0x0102030405060708ull);
    MEL_EXPECT(memcmp(dst, src, sizeof src) == 0);

    MEL_EXPECT_EQ((i64)(state.calls - baseline), (i64)0);
    mel_stream_destroy(s);
}

MEL_TEST(io, save_append_extends_file)
{
    char path[] = "/tmp/mel_io_append_XXXXXX";
    int  tfd = mkstemp(path);
    MEL_REQUIRE(tfd >= 0);
    close(tfd);

    const char* first = "first-";
    Mel_Future* f1 = mel_io_save_file(.path = path, .data = first, .len = strlen(first));
    MEL_REQUIRE_NOT_NULL(f1);
    MEL_EXPECT_EQ(mel_io_status_failed(mel_io_save_future_result(f1)->status), false);
    mel_io_save_future_release(f1);

    const char* second = "second";
    Mel_Future* f2 = mel_io_save_file(.path = path, .data = second, .len = strlen(second), .flags = MEL_IO_FILE_APPEND);
    MEL_REQUIRE_NOT_NULL(f2);
    const Mel_IO_Result* r2 = mel_io_save_future_result(f2);
    MEL_EXPECT_EQ(mel_io_status_failed(r2->status), false);
    MEL_EXPECT_EQ((i64)r2->bytes_transferred, (i64)strlen(second));
    mel_io_save_future_release(f2);

    Mel_Future* lf = mel_io_load_file(.path = path);
    MEL_REQUIRE_NOT_NULL(lf);
    const Mel_IO_Blob* blob = mel_io_load_future_result(lf);
    MEL_EXPECT_EQ(mel_io_status_failed(blob->status), false);
    MEL_EXPECT_EQ((i64)blob->len, (i64)(strlen(first) + strlen(second)));
    MEL_EXPECT(memcmp(blob->data, "first-second", strlen("first-second")) == 0);
    mel_io_load_future_release(lf);

    unlink(path);
}

MEL_TEST(io, append_without_write_rejected)
{
    char path[] = "/tmp/mel_io_appflag_XXXXXX";
    int  tfd = mkstemp(path);
    MEL_REQUIRE(tfd >= 0);
    close(tfd);

    Mel_IO_File_Open_Result o = mel_io_file_open(.path = path, .flags = MEL_IO_FILE_READ | MEL_IO_FILE_APPEND);
    MEL_EXPECT(mel_io_status_failed(o.status));
    MEL_EXPECT_EQ((void*)o.value, (void*)NULL);

    unlink(path);
}

MEL_TEST(io, file_sync_save_then_load_roundtrip)
{
    char path[] = "/tmp/mel_io_test_XXXXXX";
    int  tfd = mkstemp(path);
    MEL_REQUIRE(tfd >= 0);
    close(tfd);

    MEL_REQUIRE(mel_io_file_available());

    const char* payload = "synchronous-whole-file-roundtrip";
    Mel_Future* sf = mel_io_save_file(.path = path, .data = payload, .len = strlen(payload));
    MEL_REQUIRE_NOT_NULL(sf);
    const Mel_IO_Result* sr = mel_io_save_future_result(sf);
    MEL_EXPECT_EQ(mel_io_status_failed(sr->status), false);
    MEL_EXPECT_EQ((i64)sr->bytes_transferred, (i64)strlen(payload));
    mel_io_save_future_release(sf);

    Mel_Future* lf = mel_io_load_file(.path = path);
    MEL_REQUIRE_NOT_NULL(lf);
    const Mel_IO_Blob* blob = mel_io_load_future_result(lf);
    MEL_EXPECT_EQ(mel_io_status_failed(blob->status), false);
    MEL_EXPECT_EQ((i64)blob->len, (i64)strlen(payload));
    MEL_EXPECT(memcmp(blob->data, payload, strlen(payload)) == 0);
    mel_io_load_future_release(lf);

    unlink(path);
}

MEL_TEST(io, load_missing_file_fails_not_found)
{
    Mel_Future* lf = mel_io_load_file(.path = "/tmp/mel_io_definitely_absent_path_zzz");
    MEL_REQUIRE_NOT_NULL(lf);
    const Mel_IO_Blob* blob = mel_io_load_future_result(lf);
    MEL_EXPECT(mel_io_status_failed(blob->status));
    MEL_EXPECT((blob->status & MEL_IO_NOT_FOUND) != 0u);
    mel_io_load_future_release(lf);
}

typedef struct
{
    Mel_Vat*      vat;
    const char*   path;
    int           turn;
    int           submit_turn;
    Mel_Task      task;
    Mel_Future*   pending;
    bool          armed;
    bool          ran_inline;
    bool          done;
    Mel_Thread_Id loop_tid;
    Mel_Thread_Id cont_tid;
    Mel_IO_Status status;
    usize         len;
    char          got[64];
} Vat_Load_Ctx;

static void vat_loaded(Mel_Task* self)
{
    Vat_Load_Ctx*      a = mel_container_of(self, Vat_Load_Ctx, task);
    const Mel_IO_Blob* blob = mel_io_load_future_result(a->pending);
    a->status = blob->status;
    a->len = blob->len;
    if (blob->len > 0 && blob->len < sizeof a->got)
        memcpy(a->got, blob->data, blob->len);
    mel_io_load_future_release(a->pending);
    a->pending = NULL;
    a->cont_tid = mel_thread_current_id();
    if (!a->armed)
        a->ran_inline = true;
    a->done = true;
}

static void vat_load_idle(void* user)
{
    Vat_Load_Ctx* a = (Vat_Load_Ctx*)user;
    a->turn++;
    if (a->turn == 2)
    {
        a->submit_turn = a->turn;
        a->pending = mel_io_load_file(.path = a->path, .vat = a->vat);
        mel_task_init(&a->task, vat_loaded);
        mel_future_then(a->pending, &a->task, mel_vat_executor(a->vat));
        a->armed = true;
    }
    if (a->done)
        mel_vat_quit(a->vat);
    if (a->turn > 5000)
        mel_vat_quit(a->vat);
}

static i64 vat_load_deadline(Mel_Vat_Source* s)
{
    (void)s;
    return 0;
}

static bool vat_load_drain(Mel_Vat_Source* s, u32 budget)
{
    (void)budget;
    vat_load_idle(mel_vat_source_state(s));
    return false;
}

static const Mel_Vat_Source_Vtbl VAT_LOAD_VT = {
    .wakeables = NULL,
    .deadline = vat_load_deadline,
    .drain = vat_load_drain,
    .cancel = NULL,
};

MEL_TEST(io, load_on_vat_delivers_on_loop_executor)
{
    char path[] = "/tmp/mel_io_async_XXXXXX";
    int  tfd = mkstemp(path);
    MEL_REQUIRE(tfd >= 0);
    const char* payload = "vat-hosted-load";
    ssize_t     w = write(tfd, payload, strlen(payload));
    (void)w;
    close(tfd);

    Vat_Load_Ctx a = { 0 };
    a.path = path;

    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Vat_Waiter*  waiter = mel_vat_waiter_io(alloc);
    Mel_Vat_Driver*  driver = mel_vat_driver_fair(alloc, 64);
    Mel_Vat*         vat = mel_vat_open(alloc, (Mel_Vat_Desc){ .waiter = waiter, .driver = driver });
    a.vat = vat;
    a.loop_tid = mel_thread_current_id();
    Mel_Vat_Source* idle = mel_vat_source_open(vat, &VAT_LOAD_VT, &a);
    mel_vat_run(vat);
    mel_vat_source_close(idle);
    mel_vat_close(vat);
    driver->vt->close(driver);
    waiter->vt->close(waiter);

    MEL_EXPECT(a.done);
    MEL_EXPECT_EQ(a.ran_inline, false);
    MEL_EXPECT(mel_thread_id_equal(a.cont_tid, a.loop_tid));
    MEL_EXPECT_EQ(mel_io_status_failed(a.status), false);
    MEL_EXPECT_EQ((i64)a.len, (i64)strlen(payload));
    MEL_EXPECT(memcmp(a.got, payload, strlen(payload)) == 0);

    unlink(path);
}
