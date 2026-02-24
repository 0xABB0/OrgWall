#include "../melody/test.harness.h"
#include "../melody/vfs.h"
#include "../melody/vfs.backend.os.h"
#include "../melody/async.io.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/string.str8.h"

#include <unistd.h>
#include <sys/stat.h>

static str8 mel__test_tmpdir(void)
{
    static u8 buf[256];
    const char* tmpl = "/tmp/mel_vfs_test_XXXXXX";
    memcpy(buf, tmpl, strlen(tmpl) + 1);
    char* result = mkdtemp((char*)buf);
    assert(result);
    return str8_from_cstr(result);
}

static void mel__test_os_setup(Mel_Io* io, Mel_Vfs* vfs, Mel_Vfs_Backend** out_backend, str8* out_tmpdir)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    *out_tmpdir = mel__test_tmpdir();
    *out_backend = mel_vfs_backend_os_create(alloc, *out_tmpdir);

    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(io, &io_desc);

    Mel_Vfs_Desc desc = { .allocator = alloc, .io = io };
    mel_vfs_init(vfs, &desc);
    mel_vfs_mount(vfs, S8("/"), *out_backend, 0, true);
}

static void mel__test_os_teardown(Mel_Io* io, Mel_Vfs* vfs, Mel_Vfs_Backend* backend, str8 tmpdir)
{
    mel_vfs_unmount(vfs, S8("/"));
    mel_vfs_shutdown(vfs);
    mel_io_shutdown(io);
    mel_vfs_backend_os_destroy(backend);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %.*s", (int)tmpdir.len, (char*)tmpdir.data);
    system(cmd);
}

MEL_TEST(vfs_os_write_read_text, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    bool ok = mel_vfs_write_text(&vfs, S8("/greeting.txt"), S8("hello from OS backend"));
    MEL_ASSERT(ok);

    str8 text = mel_vfs_read_text_alloc(&vfs, S8("/greeting.txt"), mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(text.data);
    MEL_ASSERT(str8_equals(text, S8("hello from OS backend")));
    mel_dealloc(mel_alloc_heap(), text.data);

    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

MEL_TEST(vfs_os_write_read_binary, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    u8 data[256];
    for (usize i = 0; i < sizeof(data); i++) data[i] = (u8)(i & 0xFF);

    bool ok = mel_vfs_write_file(&vfs, S8("/binary.dat"), data, sizeof(data));
    MEL_ASSERT(ok);

    usize out_size = 0;
    u8* read_back = mel_vfs_read_file_alloc(&vfs, S8("/binary.dat"), &out_size, mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(read_back);
    MEL_ASSERT_EQ(out_size, sizeof(data));
    for (usize i = 0; i < sizeof(data); i++) {
        MEL_ASSERT_EQ(read_back[i], data[i]);
    }
    mel_dealloc(mel_alloc_heap(), read_back);

    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

MEL_TEST(vfs_os_stat, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    mel_vfs_write_text(&vfs, S8("/measured.txt"), S8("1234567890"));

    Mel_Vfs_Stat st;
    bool ok = mel_vfs_stat_sync(&vfs, S8("/measured.txt"), &st);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(st.size, (u64)10);
    MEL_ASSERT(st.flags & MEL_VFS_STAT_IS_FILE);
    MEL_ASSERT_GT(st.mtime_ns, (u64)0);

    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

MEL_TEST(vfs_os_exists, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    MEL_ASSERT(!mel_vfs_exists(&vfs, S8("/nope.txt")));

    mel_vfs_write_text(&vfs, S8("/exists.txt"), S8("here"));
    MEL_ASSERT(mel_vfs_exists(&vfs, S8("/exists.txt")));

    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

MEL_TEST(vfs_os_overwrite, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    mel_vfs_write_text(&vfs, S8("/ow.txt"), S8("first version is long"));
    mel_vfs_write_text(&vfs, S8("/ow.txt"), S8("short"));

    str8 text = mel_vfs_read_text_alloc(&vfs, S8("/ow.txt"), mel_alloc_heap());
    MEL_ASSERT(str8_equals(text, S8("short")));
    mel_dealloc(mel_alloc_heap(), text.data);

    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

typedef struct { i32 count; } Mel__Test_Os_Enum_Ctx;

static bool mel__test_os_enum_counter(str8 path, const Mel_Vfs_Stat* stat, void* user)
{
    MEL_UNUSED(path);
    MEL_UNUSED(stat);
    ((Mel__Test_Os_Enum_Ctx*)user)->count++;
    return true;
}

MEL_TEST(vfs_os_enumerate, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    mel_vfs_write_text(&vfs, S8("/a.txt"), S8("a"));
    mel_vfs_write_text(&vfs, S8("/b.txt"), S8("b"));
    mel_vfs_write_text(&vfs, S8("/c.txt"), S8("c"));

    Mel__Test_Os_Enum_Ctx ctx = {0};
    bool ok = mel_vfs_enumerate(&vfs, S8("/"), mel__test_os_enum_counter, &ctx);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(ctx.count, 3);

    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

MEL_TEST(vfs_os_map_unmap, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    mel_vfs_write_text(&vfs, S8("/mapme.txt"), S8("MAPPABLE"));

    u64 t1 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe open_sqe = {
        .ticket = t1,
        .op = MEL_VFS_OP_OPEN,
        .open = { .path = S8("/mapme.txt"), .open_flags = MEL_VFS_OPEN_READ },
    };
    mel_vfs_submit(&vfs, &open_sqe, 1);
    Mel_Vfs_Cqe open_cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, t1, &open_cqe));
    MEL_ASSERT_EQ(open_cqe.status, (u32)MEL_VFS_STATUS_OK);
    Mel_Vfs_File file = open_cqe.file;

    u64 t2 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe map_sqe = {
        .ticket = t2,
        .op = MEL_VFS_OP_MAP,
        .map = { .file = file, .offset = 0, .size = 8, .flags = MEL_VFS_MAP_READ },
    };
    mel_vfs_submit(&vfs, &map_sqe, 1);
    Mel_Vfs_Cqe map_cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, t2, &map_cqe));
    MEL_ASSERT_EQ(map_cqe.status, (u32)MEL_VFS_STATUS_OK);

    usize map_size = 0;
    u8* ptr = (u8*)mel_vfs_map_ptr(&vfs, map_cqe.map, &map_size);
    MEL_ASSERT_NOT_NULL(ptr);
    MEL_ASSERT_EQ(map_size, (usize)8);
    MEL_ASSERT_EQ(ptr[0], (u8)'M');
    MEL_ASSERT_EQ(ptr[7], (u8)'E');

    u64 t3 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe unmap_sqe = {
        .ticket = t3,
        .op = MEL_VFS_OP_UNMAP,
        .unmap = { .map = map_cqe.map },
    };
    mel_vfs_submit(&vfs, &unmap_sqe, 1);
    Mel_Vfs_Cqe unmap_cqe;
    mel_vfs_poll_ticket(&vfs, t3, &unmap_cqe);

    u64 t4 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe close_sqe = {
        .ticket = t4,
        .op = MEL_VFS_OP_CLOSE,
        .close = { .file = file },
    };
    mel_vfs_submit(&vfs, &close_sqe, 1);
    Mel_Vfs_Cqe close_cqe;
    mel_vfs_poll_ticket(&vfs, t4, &close_cqe);

    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

MEL_TEST(vfs_os_sync, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    mel_vfs_write_text(&vfs, S8("/sync_test.txt"), S8("data"));

    u64 t1 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe open_sqe = {
        .ticket = t1,
        .op = MEL_VFS_OP_OPEN,
        .open = { .path = S8("/sync_test.txt"), .open_flags = MEL_VFS_OPEN_WRITE },
    };
    mel_vfs_submit(&vfs, &open_sqe, 1);
    Mel_Vfs_Cqe open_cqe;
    mel_vfs_poll_ticket(&vfs, t1, &open_cqe);

    bool ok = mel_vfs_sync_file(&vfs, open_cqe.file);
    MEL_ASSERT(ok);

    u64 t2 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe close_sqe = {
        .ticket = t2,
        .op = MEL_VFS_OP_CLOSE,
        .close = { .file = open_cqe.file },
    };
    mel_vfs_submit(&vfs, &close_sqe, 1);
    Mel_Vfs_Cqe close_cqe;
    mel_vfs_poll_ticket(&vfs, t2, &close_cqe);

    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

MEL_TEST(vfs_os_threaded, .tags = "vfs, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    str8 tmpdir = mel__test_tmpdir();
    Mel_Vfs_Backend* backend = mel_vfs_backend_os_create(alloc, tmpdir);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 2 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), backend, 0, true);

    bool ok = mel_vfs_write_text(&vfs, S8("/threaded_os.txt"), S8("workers unite"));
    MEL_ASSERT(ok);

    str8 text = mel_vfs_read_text_alloc(&vfs, S8("/threaded_os.txt"), alloc);
    MEL_ASSERT(str8_equals(text, S8("workers unite")));
    mel_dealloc(alloc, text.data);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    mel_vfs_backend_os_destroy(backend);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %.*s", (int)tmpdir.len, (char*)tmpdir.data);
    system(cmd);
}

MEL_TEST(vfs_os_readv_writev, .tags = "vfs, async")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    u64 t1 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe open_sqe = {
        .ticket = t1,
        .op = MEL_VFS_OP_OPEN,
        .open = { .path = S8("/vectored.bin"), .open_flags = MEL_VFS_OPEN_READ | MEL_VFS_OPEN_WRITE | MEL_VFS_OPEN_CREATE },
    };
    mel_vfs_submit(&vfs, &open_sqe, 1);
    Mel_Vfs_Cqe open_cqe;
    mel_vfs_poll_ticket(&vfs, t1, &open_cqe);
    MEL_ASSERT_EQ(open_cqe.status, (u32)MEL_VFS_STATUS_OK);
    Mel_Vfs_File file = open_cqe.file;

    u8 chunk_a[] = "AAAA";
    u8 chunk_b[] = "BBBB";
    Mel_IoVec wvec[2] = {
        { .buffer = chunk_a, .len = 4 },
        { .buffer = chunk_b, .len = 4 },
    };

    u64 t2 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe wv_sqe = {
        .ticket = t2,
        .op = MEL_VFS_OP_WRITEV,
        .writev = { .file = file, .offset = 0, .iov = (const Mel_IoVec*)wvec, .iov_cnt = 2 },
    };
    mel_vfs_submit(&vfs, &wv_sqe, 1);
    Mel_Vfs_Cqe wv_cqe;
    mel_vfs_poll_ticket(&vfs, t2, &wv_cqe);
    MEL_ASSERT_EQ(wv_cqe.status, (u32)MEL_VFS_STATUS_OK);
    MEL_ASSERT_EQ(wv_cqe.result, (i64)8);

    u8 rbuf_a[4] = {0};
    u8 rbuf_b[4] = {0};
    Mel_IoVec rvec[2] = {
        { .buffer = rbuf_a, .len = 4 },
        { .buffer = rbuf_b, .len = 4 },
    };

    u64 t3 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe rv_sqe = {
        .ticket = t3,
        .op = MEL_VFS_OP_READV,
        .readv = { .file = file, .offset = 0, .iov = rvec, .iov_cnt = 2 },
    };
    mel_vfs_submit(&vfs, &rv_sqe, 1);
    Mel_Vfs_Cqe rv_cqe;
    mel_vfs_poll_ticket(&vfs, t3, &rv_cqe);
    MEL_ASSERT_EQ(rv_cqe.status, (u32)MEL_VFS_STATUS_OK);
    MEL_ASSERT_EQ(rv_cqe.result, (i64)8);
    MEL_ASSERT_EQ(rbuf_a[0], (u8)'A');
    MEL_ASSERT_EQ(rbuf_b[0], (u8)'B');

    u64 t4 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe close_sqe = {
        .ticket = t4,
        .op = MEL_VFS_OP_CLOSE,
        .close = { .file = file },
    };
    mel_vfs_submit(&vfs, &close_sqe, 1);
    Mel_Vfs_Cqe close_cqe;
    mel_vfs_poll_ticket(&vfs, t4, &close_cqe);

    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

#include "../melody/core.platform.h"
#if MEL_PLATFORM_APPLE

#include <fcntl.h>

MEL_TEST(vfs_os_watch_file_modify, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    mel_vfs_write_text(&vfs, S8("/watch_me.txt"), S8("initial"));

    Mel_Vfs_Native_Handle wh;
    i32 err = backend->watch_open(backend, S8("watch_me.txt"), false, 0, &wh);
    MEL_ASSERT_EQ(err, 0);

    mel_vfs_write_text(&vfs, S8("/watch_me.txt"), S8("modified"));

    u8 path_buf[512];
    usize path_len = 0;
    i32 action = 0;
    i32 result = backend->watch_next(backend, wh, 2000, path_buf, sizeof(path_buf), &path_len, &action);
    MEL_ASSERT_EQ(result, 0);
    MEL_ASSERT_EQ(action, MEL_VFS_WATCH_MODIFIED);
    MEL_ASSERT_GT(path_len, (usize)0);

    backend->watch_close(backend, wh);
    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

MEL_TEST(vfs_os_watch_file_delete, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    mel_vfs_write_text(&vfs, S8("/delete_me.txt"), S8("goodbye"));

    Mel_Vfs_Native_Handle wh;
    i32 err = backend->watch_open(backend, S8("delete_me.txt"), false, 0, &wh);
    MEL_ASSERT_EQ(err, 0);

    char full_path[4096];
    snprintf(full_path, sizeof(full_path), "%.*s/delete_me.txt", (int)tmpdir.len, (char*)tmpdir.data);
    unlink(full_path);

    u8 path_buf[512];
    usize path_len = 0;
    i32 action = 0;
    i32 result = backend->watch_next(backend, wh, 2000, path_buf, sizeof(path_buf), &path_len, &action);
    MEL_ASSERT_EQ(result, 0);
    MEL_ASSERT_EQ(action, MEL_VFS_WATCH_REMOVED);

    backend->watch_close(backend, wh);
    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

MEL_TEST(vfs_os_watch_timeout, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    mel_vfs_write_text(&vfs, S8("/quiet.txt"), S8("nothing happens"));

    Mel_Vfs_Native_Handle wh;
    i32 err = backend->watch_open(backend, S8("quiet.txt"), false, 0, &wh);
    MEL_ASSERT_EQ(err, 0);

    u8 path_buf[512];
    usize path_len = 0;
    i32 action = 0;
    i32 result = backend->watch_next(backend, wh, 100, path_buf, sizeof(path_buf), &path_len, &action);
    MEL_ASSERT_EQ(result, 1);

    backend->watch_close(backend, wh);
    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

MEL_TEST(vfs_os_watch_recursive_unsupported, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    Mel_Vfs_Native_Handle wh;
    i32 err = backend->watch_open(backend, S8("."), true, 0, &wh);
    MEL_ASSERT(err < 0);

    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

MEL_TEST(vfs_os_watch_close_no_crash, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    str8 tmpdir;
    mel__test_os_setup(&io, &vfs, &backend, &tmpdir);

    mel_vfs_write_text(&vfs, S8("/closeme.txt"), S8("data"));

    Mel_Vfs_Native_Handle wh;
    i32 err = backend->watch_open(backend, S8("closeme.txt"), false, 0, &wh);
    MEL_ASSERT_EQ(err, 0);

    backend->watch_close(backend, wh);

    mel__test_os_teardown(&io, &vfs, backend, tmpdir);
}

#endif
