#include "../melody/test.harness.h"
#include "../melody/vfs.h"
#include "../melody/vfs.backend.mock.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/string.str8.h"
#include "../melody/string.path.h"

#include <SDL3/SDL_timer.h>

static void mel__test_vfs_setup(Mel_Io* io, Mel_Vfs* vfs, Mel_Vfs_Backend** out_backend)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Vfs_Backend* backend = mel_vfs_backend_mock_create(alloc);
    *out_backend = backend;

    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(io, &io_desc);

    Mel_Vfs_Desc desc = { .allocator = alloc, .io = io };
    mel_vfs_init(vfs, &desc);
    mel_vfs_mount(vfs, S8("/"), backend, 0, true);
}

static void mel__test_vfs_teardown(Mel_Io* io, Mel_Vfs* vfs, Mel_Vfs_Backend* backend)
{
    mel_vfs_unmount(vfs, S8("/"));
    mel_vfs_shutdown(vfs);
    mel_io_shutdown(io);
    mel_vfs_backend_mock_destroy(backend);
}

typedef struct {
    const Mel_Alloc* alloc;
    i64 write_result;
    i32 dir_next_result;
    usize dir_needed_len;
} Mel__Test_Vfs_Fault_Data;

static i32 mel__test_fault_open(Mel_Vfs_Backend* b, str8 path, u32 flags, Mel_Vfs_Native_Handle* out)
{
    MEL_UNUSED(b);
    MEL_UNUSED(path);
    MEL_UNUSED(flags);
    *out = (Mel_Vfs_Native_Handle)1;
    return 0;
}

static void mel__test_fault_close(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h)
{
    MEL_UNUSED(b);
    MEL_UNUSED(h);
}

static i64 mel__test_fault_read(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, void* dst, usize size)
{
    MEL_UNUSED(b);
    MEL_UNUSED(h);
    MEL_UNUSED(offset);
    MEL_UNUSED(dst);
    MEL_UNUSED(size);
    return -38;
}

static i64 mel__test_fault_write(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, const void* src, usize size)
{
    MEL_UNUSED(h);
    MEL_UNUSED(offset);
    MEL_UNUSED(src);
    MEL_UNUSED(size);
    Mel__Test_Vfs_Fault_Data* d = (Mel__Test_Vfs_Fault_Data*)b->impl_data;
    return d->write_result;
}

static i64 mel__test_fault_readv(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, Mel_IoVec* iov, usize iov_cnt)
{
    MEL_UNUSED(b);
    MEL_UNUSED(h);
    MEL_UNUSED(offset);
    MEL_UNUSED(iov);
    MEL_UNUSED(iov_cnt);
    return -38;
}

static i64 mel__test_fault_writev(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, const Mel_IoVec* iov, usize iov_cnt)
{
    MEL_UNUSED(b);
    MEL_UNUSED(h);
    MEL_UNUSED(offset);
    MEL_UNUSED(iov);
    MEL_UNUSED(iov_cnt);
    return -38;
}

static i32 mel__test_fault_stat(Mel_Vfs_Backend* b, str8 path, Mel_Vfs_Stat* out)
{
    MEL_UNUSED(b);
    MEL_UNUSED(path);
    MEL_UNUSED(out);
    return -2;
}

static i32 mel__test_fault_dir_open(Mel_Vfs_Backend* b, str8 path, Mel_Vfs_Native_Handle* out)
{
    MEL_UNUSED(b);
    MEL_UNUSED(path);
    *out = (Mel_Vfs_Native_Handle)1;
    return 0;
}

static i32 mel__test_fault_dir_next(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h,
                                    u8* name_buf, usize name_cap, usize* out_name_len,
                                    Mel_Vfs_Stat* out_stat)
{
    MEL_UNUSED(h);
    MEL_UNUSED(name_buf);
    MEL_UNUSED(name_cap);
    MEL_UNUSED(out_stat);
    Mel__Test_Vfs_Fault_Data* d = (Mel__Test_Vfs_Fault_Data*)b->impl_data;
    *out_name_len = d->dir_needed_len;
    return d->dir_next_result;
}

static void mel__test_fault_dir_close(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h)
{
    MEL_UNUSED(b);
    MEL_UNUSED(h);
}

static i32 mel__test_fault_map(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, usize size, u32 flags, void** out_ptr)
{
    MEL_UNUSED(b);
    MEL_UNUSED(h);
    MEL_UNUSED(offset);
    MEL_UNUSED(size);
    MEL_UNUSED(flags);
    MEL_UNUSED(out_ptr);
    return -38;
}

static void mel__test_fault_unmap(Mel_Vfs_Backend* b, void* ptr, usize size)
{
    MEL_UNUSED(b);
    MEL_UNUSED(ptr);
    MEL_UNUSED(size);
}

static i32 mel__test_fault_sync(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h)
{
    MEL_UNUSED(b);
    MEL_UNUSED(h);
    return 0;
}

static void mel__test_fault_destroy(Mel_Vfs_Backend* b)
{
    Mel__Test_Vfs_Fault_Data* d = (Mel__Test_Vfs_Fault_Data*)b->impl_data;
    const Mel_Alloc* alloc = d->alloc;
    mel_dealloc(alloc, d);
    mel_dealloc(alloc, b);
}

static Mel_Vfs_Backend* mel__test_fault_backend_create(const Mel_Alloc* alloc, i64 write_result, i32 dir_next_result, usize dir_needed_len)
{
    Mel_Vfs_Backend* b = mel_calloc(alloc, sizeof(Mel_Vfs_Backend));
    Mel__Test_Vfs_Fault_Data* d = mel_calloc(alloc, sizeof(Mel__Test_Vfs_Fault_Data));
    d->alloc = alloc;
    d->write_result = write_result;
    d->dir_next_result = dir_next_result;
    d->dir_needed_len = dir_needed_len;

    b->open = mel__test_fault_open;
    b->close = mel__test_fault_close;
    b->read = mel__test_fault_read;
    b->write = mel__test_fault_write;
    b->readv = mel__test_fault_readv;
    b->writev = mel__test_fault_writev;
    b->stat = mel__test_fault_stat;
    b->dir_open = mel__test_fault_dir_open;
    b->dir_next = mel__test_fault_dir_next;
    b->dir_close = mel__test_fault_dir_close;
    b->map = mel__test_fault_map;
    b->unmap = mel__test_fault_unmap;
    b->watch_open = NULL;
    b->watch_next = NULL;
    b->watch_close = NULL;
    b->sync = mel__test_fault_sync;
    b->destroy = mel__test_fault_destroy;
    b->impl_data = d;
    return b;
}

MEL_TEST(vfs_write_then_read, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    bool ok = mel_vfs_write_text(&vfs, S8("/hello.txt"), S8("hello world"));
    MEL_ASSERT(ok);

    str8 text = mel_vfs_read_text_alloc(&vfs, S8("/hello.txt"), mel_alloc_heap());
    MEL_ASSERT(text.data != NULL);
    MEL_ASSERT(str8_equals(text, S8("hello world")));
    mel_dealloc(mel_alloc_heap(), text.data);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_write_binary, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    u8 data[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42 };
    bool ok = mel_vfs_write_file(&vfs, S8("/binary.dat"), data, sizeof(data));
    MEL_ASSERT(ok);

    usize out_size = 0;
    u8* read_back = mel_vfs_read_file_alloc(&vfs, S8("/binary.dat"), &out_size, mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(read_back);
    MEL_ASSERT_EQ(out_size, sizeof(data));
    MEL_ASSERT_EQ(read_back[0], (u8)0xDE);
    MEL_ASSERT_EQ(read_back[3], (u8)0xEF);
    MEL_ASSERT_EQ(read_back[4], (u8)0x00);
    MEL_ASSERT_EQ(read_back[5], (u8)0x42);
    mel_dealloc(mel_alloc_heap(), read_back);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_exists, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    MEL_ASSERT(!mel_vfs_exists(&vfs, S8("/nope.txt")));

    mel_vfs_write_text(&vfs, S8("/yep.txt"), S8("data"));
    MEL_ASSERT(mel_vfs_exists(&vfs, S8("/yep.txt")));

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_stat_sync, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_write_text(&vfs, S8("/sized.txt"), S8("twelve chars"));

    Mel_Vfs_Stat st;
    bool ok = mel_vfs_stat_sync(&vfs, S8("/sized.txt"), &st);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(st.size, (u64)12);
    MEL_ASSERT(st.flags & MEL_VFS_STAT_IS_FILE);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_read_nonexistent, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    str8 text = mel_vfs_read_text_alloc(&vfs, S8("/ghost.txt"), mel_alloc_heap());
    MEL_ASSERT_NULL(text.data);
    MEL_ASSERT_EQ(text.len, (size)0);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_overwrite, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_write_text(&vfs, S8("/file.txt"), S8("first"));
    mel_vfs_write_text(&vfs, S8("/file.txt"), S8("second"));

    str8 text = mel_vfs_read_text_alloc(&vfs, S8("/file.txt"), mel_alloc_heap());
    MEL_ASSERT(str8_equals(text, S8("second")));
    mel_dealloc(mel_alloc_heap(), text.data);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_injected_file, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_backend_mock_inject_file(backend, S8("preloaded.txt"),
                                     (const u8*)"injected", 8);

    str8 text = mel_vfs_read_text_alloc(&vfs, S8("/preloaded.txt"), mel_alloc_heap());
    MEL_ASSERT(str8_equals(text, S8("injected")));
    mel_dealloc(mel_alloc_heap(), text.data);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

typedef struct { i32 count; } Mel__Test_Enum_Ctx;

static bool mel__test_enum_counter(str8 path, const Mel_Vfs_Stat* stat, void* user)
{
    MEL_UNUSED(path);
    MEL_UNUSED(stat);
    ((Mel__Test_Enum_Ctx*)user)->count++;
    return true;
}

MEL_TEST(vfs_enumerate_files, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_backend_mock_inject_file(backend, S8("a.txt"), (const u8*)"a", 1);
    mel_vfs_backend_mock_inject_file(backend, S8("b.txt"), (const u8*)"b", 1);
    mel_vfs_backend_mock_inject_file(backend, S8("c.txt"), (const u8*)"c", 1);

    Mel__Test_Enum_Ctx ctx = {0};
    bool ok = mel_vfs_enumerate(&vfs, S8("/"), mel__test_enum_counter, &ctx);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(ctx.count, 3);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_enumerate_skip_dirs, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_backend_mock_inject_file(backend, S8("file.txt"), (const u8*)"f", 1);
    mel_vfs_backend_mock_inject_dir(backend, S8("subdir"));

    Mel__Test_Enum_Ctx ctx = {0};
    bool ok = mel_vfs_enumerate(&vfs, S8("/"), mel__test_enum_counter, &ctx);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(ctx.count, 1);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_enumerate_include_dirs, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_backend_mock_inject_file(backend, S8("file.txt"), (const u8*)"f", 1);
    mel_vfs_backend_mock_inject_dir(backend, S8("subdir"));

    Mel__Test_Enum_Ctx ctx = {0};
    bool ok = mel_vfs_enumerate(&vfs, S8("/"), mel__test_enum_counter, &ctx, .include_dirs = true);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(ctx.count, 2);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_async_submit_poll, .tags = "vfs, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Vfs_Backend* backend = mel_vfs_backend_mock_create(alloc);
    mel_vfs_backend_mock_inject_file(backend, S8("test.txt"), (const u8*)"async", 5);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), backend, 0, true);

    u64 ticket = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe sqe = {
        .ticket = ticket,
        .op = MEL_VFS_OP_STAT,
        .stat = { .path = S8("/test.txt") },
    };

    i32 accepted = mel_vfs_submit(&vfs, &sqe, 1);
    MEL_ASSERT_EQ(accepted, 1);

    Mel_Vfs_Cqe cqe;
    bool found = mel_vfs_poll_ticket(&vfs, ticket, &cqe);
    MEL_ASSERT(found);
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_VFS_STATUS_OK);
    MEL_ASSERT_EQ(cqe.stat.size, (u64)5);
    MEL_ASSERT(cqe.stat.flags & MEL_VFS_STAT_IS_FILE);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    mel_vfs_backend_mock_destroy(backend);
}

MEL_TEST(vfs_async_open_read_close, .tags = "vfs, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Vfs_Backend* backend = mel_vfs_backend_mock_create(alloc);
    mel_vfs_backend_mock_inject_file(backend, S8("data.bin"), (const u8*)"ABCD", 4);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), backend, 0, true);

    u64 t1 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe open_sqe = {
        .ticket = t1,
        .op = MEL_VFS_OP_OPEN,
        .open = { .path = S8("/data.bin"), .open_flags = MEL_VFS_OPEN_READ },
    };
    mel_vfs_submit(&vfs, &open_sqe, 1);

    Mel_Vfs_Cqe open_cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, t1, &open_cqe));
    MEL_ASSERT_EQ(open_cqe.status, (u32)MEL_VFS_STATUS_OK);

    Mel_Vfs_File file = open_cqe.file;
    MEL_ASSERT(mel_vfs_file_valid(file));

    u8 buf[16] = {0};
    u64 t2 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe read_sqe = {
        .ticket = t2,
        .op = MEL_VFS_OP_READ,
        .read = { .file = file, .offset = 0, .dst = buf, .size = sizeof(buf) },
    };
    mel_vfs_submit(&vfs, &read_sqe, 1);

    Mel_Vfs_Cqe read_cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, t2, &read_cqe));
    MEL_ASSERT_EQ(read_cqe.status, (u32)MEL_VFS_STATUS_OK);
    MEL_ASSERT_EQ(read_cqe.result, (i64)4);
    MEL_ASSERT_EQ(buf[0], (u8)'A');
    MEL_ASSERT_EQ(buf[3], (u8)'D');

    u64 t3 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe close_sqe = {
        .ticket = t3,
        .op = MEL_VFS_OP_CLOSE,
        .close = { .file = file },
    };
    mel_vfs_submit(&vfs, &close_sqe, 1);

    Mel_Vfs_Cqe close_cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, t3, &close_cqe));
    MEL_ASSERT_EQ(close_cqe.status, (u32)MEL_VFS_STATUS_OK);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    mel_vfs_backend_mock_destroy(backend);
}

MEL_TEST(vfs_mount_priority, .tags = "vfs")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Vfs_Backend* low = mel_vfs_backend_mock_create(alloc);
    Mel_Vfs_Backend* high = mel_vfs_backend_mock_create(alloc);

    mel_vfs_backend_mock_inject_file(low, S8("shared.txt"), (const u8*)"low", 3);
    mel_vfs_backend_mock_inject_file(high, S8("shared.txt"), (const u8*)"high", 4);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), low, 0, true);
    mel_vfs_mount(&vfs, S8("/"), high, 10, true);

    str8 text = mel_vfs_read_text_alloc(&vfs, S8("/shared.txt"), alloc);
    MEL_ASSERT(str8_equals(text, S8("high")));
    mel_dealloc(alloc, text.data);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    mel_vfs_backend_mock_destroy(high);
    mel_vfs_backend_mock_destroy(low);
}

MEL_TEST(vfs_mount_prefix_specificity, .tags = "vfs")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Vfs_Backend* root_be = mel_vfs_backend_mock_create(alloc);
    Mel_Vfs_Backend* mods_be = mel_vfs_backend_mock_create(alloc);

    mel_vfs_backend_mock_inject_file(root_be, S8("mods/weapon.txt"), (const u8*)"root", 4);
    mel_vfs_backend_mock_inject_file(mods_be, S8("weapon.txt"), (const u8*)"mods", 4);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), root_be, 0, true);
    mel_vfs_mount(&vfs, S8("/mods"), mods_be, 0, true);

    str8 text = mel_vfs_read_text_alloc(&vfs, S8("/mods/weapon.txt"), alloc);
    MEL_ASSERT(str8_equals(text, S8("mods")));
    mel_dealloc(alloc, text.data);

    mel_vfs_unmount(&vfs, S8("/mods"));
    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    mel_vfs_backend_mock_destroy(mods_be);
    mel_vfs_backend_mock_destroy(root_be);
}

MEL_TEST(vfs_readonly_mount, .tags = "vfs")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Vfs_Backend* backend = mel_vfs_backend_mock_create(alloc);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), backend, 0, false);

    bool ok = mel_vfs_write_text(&vfs, S8("/forbidden.txt"), S8("nope"));
    MEL_ASSERT(!ok);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    mel_vfs_backend_mock_destroy(backend);
}

MEL_TEST(vfs_path_normalize_dot, .tags = "vfs")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/foo/./bar"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/foo/bar")));
}

MEL_TEST(vfs_path_normalize_dotdot, .tags = "vfs")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/foo/baz/../bar"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/foo/bar")));
}

MEL_TEST(vfs_path_normalize_double_slash, .tags = "vfs")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/foo//bar///baz"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/foo/bar/baz")));
}

MEL_TEST(vfs_path_normalize_backslash, .tags = "vfs")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("foo\\bar\\baz"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/foo/bar/baz")));
}

MEL_TEST(vfs_path_normalize_trailing_slash, .tags = "vfs")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/foo/bar/"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/foo/bar")));
}

MEL_TEST(vfs_async_threaded_write_read, .tags = "vfs, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Vfs_Backend* backend = mel_vfs_backend_mock_create(alloc);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 2 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), backend, 0, true);

    bool ok = mel_vfs_write_text(&vfs, S8("/threaded.txt"), S8("from worker"));
    MEL_ASSERT(ok);

    str8 text = mel_vfs_read_text_alloc(&vfs, S8("/threaded.txt"), alloc);
    MEL_ASSERT(str8_equals(text, S8("from worker")));
    mel_dealloc(alloc, text.data);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    mel_vfs_backend_mock_destroy(backend);
}

MEL_TEST(vfs_watch_unsupported, .tags = "vfs")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Vfs_Backend* backend = mel_vfs_backend_mock_create(alloc);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), backend, 0, true);

    u64 ticket = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe sqe = {
        .ticket = ticket,
        .op = MEL_VFS_OP_WATCH_OPEN,
        .watch_open = { .path = S8("/"), .recursive = true },
    };
    mel_vfs_submit(&vfs, &sqe, 1);

    Mel_Vfs_Cqe cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, ticket, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_VFS_STATUS_UNSUPPORTED);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    mel_vfs_backend_mock_destroy(backend);
}

MEL_TEST(vfs_map_unmap, .tags = "vfs")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Vfs_Backend* backend = mel_vfs_backend_mock_create(alloc);
    mel_vfs_backend_mock_inject_file(backend, S8("mapped.bin"), (const u8*)"MAPPED", 6);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), backend, 0, true);

    u64 t1 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe open_sqe = {
        .ticket = t1,
        .op = MEL_VFS_OP_OPEN,
        .open = { .path = S8("/mapped.bin"), .open_flags = MEL_VFS_OPEN_READ },
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
        .map = { .file = file, .offset = 0, .size = 6, .flags = MEL_VFS_MAP_READ },
    };
    mel_vfs_submit(&vfs, &map_sqe, 1);
    Mel_Vfs_Cqe map_cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, t2, &map_cqe));
    MEL_ASSERT_EQ(map_cqe.status, (u32)MEL_VFS_STATUS_OK);
    Mel_Vfs_Map map_handle = map_cqe.map;

    usize map_size = 0;
    void* ptr = mel_vfs_map_ptr(&vfs, map_handle, &map_size);
    MEL_ASSERT_NOT_NULL(ptr);
    MEL_ASSERT_EQ(map_size, (usize)6);
    MEL_ASSERT_EQ(((u8*)ptr)[0], (u8)'M');

    u64 t3 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe unmap_sqe = {
        .ticket = t3,
        .op = MEL_VFS_OP_UNMAP,
        .unmap = { .map = map_handle },
    };
    mel_vfs_submit(&vfs, &unmap_sqe, 1);
    Mel_Vfs_Cqe unmap_cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, t3, &unmap_cqe));
    MEL_ASSERT_EQ(unmap_cqe.status, (u32)MEL_VFS_STATUS_OK);

    u64 t4 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe close_sqe = {
        .ticket = t4,
        .op = MEL_VFS_OP_CLOSE,
        .close = { .file = file },
    };
    mel_vfs_submit(&vfs, &close_sqe, 1);
    Mel_Vfs_Cqe close_cqe;
    mel_vfs_poll_ticket(&vfs, t4, &close_cqe);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    mel_vfs_backend_mock_destroy(backend);
}

MEL_TEST(vfs_sync_op, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_write_text(&vfs, S8("/sync_me.txt"), S8("data"));

    u64 t1 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe open_sqe = {
        .ticket = t1,
        .op = MEL_VFS_OP_OPEN,
        .open = { .path = S8("/sync_me.txt"), .open_flags = MEL_VFS_OPEN_WRITE },
    };
    mel_vfs_submit(&vfs, &open_sqe, 1);
    Mel_Vfs_Cqe open_cqe;
    mel_vfs_poll_ticket(&vfs, t1, &open_cqe);
    Mel_Vfs_File file = open_cqe.file;

    bool ok = mel_vfs_sync_file(&vfs, file);
    MEL_ASSERT(ok);

    u64 t2 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe close_sqe = {
        .ticket = t2,
        .op = MEL_VFS_OP_CLOSE,
        .close = { .file = file },
    };
    mel_vfs_submit(&vfs, &close_sqe, 1);
    Mel_Vfs_Cqe close_cqe;
    mel_vfs_poll_ticket(&vfs, t2, &close_cqe);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_ticket_monotonic, .tags = "vfs, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);

    u64 t1 = mel_vfs_next_ticket(&vfs);
    u64 t2 = mel_vfs_next_ticket(&vfs);
    u64 t3 = mel_vfs_next_ticket(&vfs);

    MEL_ASSERT_GT(t1, (u64)0);
    MEL_ASSERT_GT(t2, t1);
    MEL_ASSERT_GT(t3, t2);

    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
}

MEL_TEST(vfs_submit_chain_atomic, .tags = "vfs, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Vfs_Backend* backend = mel_vfs_backend_mock_create(alloc);
    mel_vfs_backend_mock_inject_file(backend, S8("chain.txt"), (const u8*)"X", 1);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0, .sq_capacity = 4 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), backend, 0, true);

    u64 t1 = mel_vfs_next_ticket(&vfs);
    u64 t2 = mel_vfs_next_ticket(&vfs);

    Mel_Vfs_Sqe chain[2] = {
        {
            .ticket = t1,
            .op = MEL_VFS_OP_STAT,
            .flags = MEL_VFS_SQE_F_LINK_NEXT,
            .stat = { .path = S8("/chain.txt") },
        },
        {
            .ticket = t2,
            .op = MEL_VFS_OP_STAT,
            .stat = { .path = S8("/chain.txt") },
        },
    };

    i32 accepted = mel_vfs_submit(&vfs, chain, 2);
    MEL_ASSERT_EQ(accepted, 2);

    Mel_Vfs_Cqe cqe1, cqe2;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, t1, &cqe1));
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, t2, &cqe2));
    MEL_ASSERT_EQ(cqe1.status, (u32)MEL_VFS_STATUS_OK);
    MEL_ASSERT_EQ(cqe2.status, (u32)MEL_VFS_STATUS_OK);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    mel_vfs_backend_mock_destroy(backend);
}

MEL_TEST(vfs_path_normalize_root_escape_clamps, .tags = "vfs")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/../../../etc/passwd"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/etc/passwd")));
}

MEL_TEST(vfs_path_normalize_pure_dotdot, .tags = "vfs")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/../../.."), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/")));
}

MEL_TEST(vfs_path_normalize_empty, .tags = "vfs")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8(""), buf, sizeof(buf));
    MEL_ASSERT_EQ(result.len, (size)0);
}

MEL_TEST(vfs_path_normalize_just_slash, .tags = "vfs")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/")));
}

MEL_TEST(vfs_path_normalize_complex, .tags = "vfs")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/a/b/../c/./d//e"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/a/c/d/e")));
}

MEL_TEST(vfs_empty_file_read, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_backend_mock_inject_file(backend, S8("empty.txt"), NULL, 0);

    usize out_size = 999;
    u8* data = mel_vfs_read_file_alloc(&vfs, S8("/empty.txt"), &out_size, mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(data);
    MEL_ASSERT_EQ(out_size, (usize)0);
    mel_dealloc(mel_alloc_heap(), data);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_no_mount_returns_not_found, .tags = "vfs")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);

    MEL_ASSERT(!mel_vfs_exists(&vfs, S8("/anything.txt")));

    u64 ticket = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe sqe = {
        .ticket = ticket,
        .op = MEL_VFS_OP_STAT,
        .stat = { .path = S8("/anything.txt") },
    };
    mel_vfs_submit(&vfs, &sqe, 1);

    Mel_Vfs_Cqe cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, ticket, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_VFS_STATUS_NOT_FOUND);

    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
}

MEL_TEST(vfs_readv_writev_mock, .tags = "vfs, async")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    u64 t1 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe open_sqe = {
        .ticket = t1,
        .op = MEL_VFS_OP_OPEN,
        .open = { .path = S8("/vec.bin"), .open_flags = MEL_VFS_OPEN_READ | MEL_VFS_OPEN_WRITE | MEL_VFS_OPEN_CREATE },
    };
    mel_vfs_submit(&vfs, &open_sqe, 1);
    Mel_Vfs_Cqe open_cqe;
    mel_vfs_poll_ticket(&vfs, t1, &open_cqe);
    Mel_Vfs_File file = open_cqe.file;

    u8 a[] = "XX";
    u8 b[] = "YY";
    Mel_IoVec wvec[2] = { { .buffer = a, .len = 2 }, { .buffer = b, .len = 2 } };

    u64 t2 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe wv_sqe = {
        .ticket = t2,
        .op = MEL_VFS_OP_WRITEV,
        .writev = { .file = file, .offset = 0, .iov = (const Mel_IoVec*)wvec, .iov_cnt = 2 },
    };
    mel_vfs_submit(&vfs, &wv_sqe, 1);
    Mel_Vfs_Cqe wv_cqe;
    mel_vfs_poll_ticket(&vfs, t2, &wv_cqe);
    MEL_ASSERT_EQ(wv_cqe.result, (i64)4);

    u8 ra[2] = {0}, rb[2] = {0};
    Mel_IoVec rvec[2] = { { .buffer = ra, .len = 2 }, { .buffer = rb, .len = 2 } };

    u64 t3 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe rv_sqe = {
        .ticket = t3,
        .op = MEL_VFS_OP_READV,
        .readv = { .file = file, .offset = 0, .iov = rvec, .iov_cnt = 2 },
    };
    mel_vfs_submit(&vfs, &rv_sqe, 1);
    Mel_Vfs_Cqe rv_cqe;
    mel_vfs_poll_ticket(&vfs, t3, &rv_cqe);
    MEL_ASSERT_EQ(rv_cqe.result, (i64)4);
    MEL_ASSERT_EQ(ra[0], (u8)'X');
    MEL_ASSERT_EQ(rb[0], (u8)'Y');

    u64 t4 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe close_sqe = { .ticket = t4, .op = MEL_VFS_OP_CLOSE, .close = { .file = file } };
    mel_vfs_submit(&vfs, &close_sqe, 1);
    Mel_Vfs_Cqe close_cqe;
    mel_vfs_poll_ticket(&vfs, t4, &close_cqe);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_dir_next_batch, .tags = "vfs, async")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_backend_mock_inject_file(backend, S8("x.txt"), (const u8*)"x", 1);
    mel_vfs_backend_mock_inject_file(backend, S8("y.txt"), (const u8*)"y", 1);
    mel_vfs_backend_mock_inject_file(backend, S8("z.txt"), (const u8*)"z", 1);

    u64 t1 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe open_sqe = {
        .ticket = t1,
        .op = MEL_VFS_OP_DIR_OPEN,
        .dir_open = { .path = S8("/") },
    };
    mel_vfs_submit(&vfs, &open_sqe, 1);
    Mel_Vfs_Cqe open_cqe;
    mel_vfs_poll_ticket(&vfs, t1, &open_cqe);
    MEL_ASSERT_EQ(open_cqe.status, (u32)MEL_VFS_STATUS_OK);
    Mel_Vfs_Dir dir = open_cqe.dir;

    Mel_Vfs_Dir_Entry entries[8];
    u8 str_blob[256];

    u64 t2 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe batch_sqe = {
        .ticket = t2,
        .op = MEL_VFS_OP_DIR_NEXT_BATCH,
        .dir_next_batch = {
            .dir = dir,
            .entries = entries,
            .entry_cap = 8,
            .str_blob = str_blob,
            .str_blob_cap = sizeof(str_blob),
        },
    };
    mel_vfs_submit(&vfs, &batch_sqe, 1);
    Mel_Vfs_Cqe batch_cqe;
    mel_vfs_poll_ticket(&vfs, t2, &batch_cqe);
    MEL_ASSERT_EQ(batch_cqe.status, (u32)MEL_VFS_STATUS_OK);
    MEL_ASSERT_EQ(batch_cqe.result, (i64)3);

    u64 t3 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe close_sqe = { .ticket = t3, .op = MEL_VFS_OP_DIR_CLOSE, .dir_close = { .dir = dir } };
    mel_vfs_submit(&vfs, &close_sqe, 1);
    Mel_Vfs_Cqe close_cqe;
    mel_vfs_poll_ticket(&vfs, t3, &close_cqe);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_multiple_open_close, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_backend_mock_inject_file(backend, S8("multi.txt"), (const u8*)"data", 4);

    for (i32 round = 0; round < 10; round++) {
        u64 t1 = mel_vfs_next_ticket(&vfs);
        Mel_Vfs_Sqe open_sqe = {
            .ticket = t1,
            .op = MEL_VFS_OP_OPEN,
            .open = { .path = S8("/multi.txt"), .open_flags = MEL_VFS_OPEN_READ },
        };
        mel_vfs_submit(&vfs, &open_sqe, 1);
        Mel_Vfs_Cqe open_cqe;
        mel_vfs_poll_ticket(&vfs, t1, &open_cqe);
        MEL_ASSERT_EQ(open_cqe.status, (u32)MEL_VFS_STATUS_OK);

        u64 t2 = mel_vfs_next_ticket(&vfs);
        Mel_Vfs_Sqe close_sqe = { .ticket = t2, .op = MEL_VFS_OP_CLOSE, .close = { .file = open_cqe.file } };
        mel_vfs_submit(&vfs, &close_sqe, 1);
        Mel_Vfs_Cqe close_cqe;
        mel_vfs_poll_ticket(&vfs, t2, &close_cqe);
    }

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_poll_nonexistent_ticket, .tags = "vfs, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);

    Mel_Vfs_Cqe cqe;
    bool found = mel_vfs_poll_ticket(&vfs, 99999, &cqe);
    MEL_ASSERT(!found);

    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
}

MEL_TEST(vfs_cancel_op, .tags = "vfs, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Vfs_Backend* backend = mel_vfs_backend_mock_create(alloc);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), backend, 0, true);

    u64 target_ticket = mel_vfs_next_ticket(&vfs);

    u64 cancel_ticket = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe cancel_sqe = {
        .ticket = cancel_ticket,
        .op = MEL_VFS_OP_CANCEL,
        .cancel = { .ticket_to_cancel = target_ticket },
    };
    mel_vfs_submit(&vfs, &cancel_sqe, 1);

    Mel_Vfs_Cqe cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, cancel_ticket, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_VFS_STATUS_OK);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    mel_vfs_backend_mock_destroy(backend);
}

MEL_TEST(vfs_invalid_op, .tags = "vfs, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);

    u64 ticket = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe sqe = { .ticket = ticket, .op = 255 };
    mel_vfs_submit(&vfs, &sqe, 1);

    Mel_Vfs_Cqe cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, ticket, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_VFS_STATUS_INVALID_ARGUMENT);

    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
}

MEL_TEST(vfs_write_file_short_write_is_failure, .tags = "vfs")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Vfs_Backend* backend = mel__test_fault_backend_create(alloc, 3, 1, 0);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), backend, 0, true);

    u8 data[4] = {1, 2, 3, 4};
    bool ok = mel_vfs_write_file(&vfs, S8("/short.bin"), data, sizeof(data));
    MEL_ASSERT(!ok);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    backend->destroy(backend);
}

MEL_TEST(vfs_dir_next_backend_error_maps_to_io_error, .tags = "vfs, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Vfs_Backend* backend = mel__test_fault_backend_create(alloc, 0, -5, 0);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), backend, 0, true);

    u64 t1 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe open_sqe = {
        .ticket = t1,
        .op = MEL_VFS_OP_DIR_OPEN,
        .dir_open = { .path = S8("/") },
    };
    mel_vfs_submit(&vfs, &open_sqe, 1);
    Mel_Vfs_Cqe open_cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, t1, &open_cqe));
    MEL_ASSERT_EQ(open_cqe.status, (u32)MEL_VFS_STATUS_OK);

    Mel_Vfs_Dir dir = open_cqe.dir;
    u8 name_buf[32];
    Mel_Vfs_Dir_Entry entry;

    u64 t2 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe next_sqe = {
        .ticket = t2,
        .op = MEL_VFS_OP_DIR_NEXT,
        .dir_next = {
            .dir = dir,
            .entry = &entry,
            .name_buf = name_buf,
            .name_cap = sizeof(name_buf),
        },
    };
    mel_vfs_submit(&vfs, &next_sqe, 1);
    Mel_Vfs_Cqe next_cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, t2, &next_cqe));
    MEL_ASSERT_EQ(next_cqe.status, (u32)MEL_VFS_STATUS_IO_ERROR);

    u64 t3 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe close_sqe = {
        .ticket = t3,
        .op = MEL_VFS_OP_DIR_CLOSE,
        .dir_close = { .dir = dir },
    };
    mel_vfs_submit(&vfs, &close_sqe, 1);
    Mel_Vfs_Cqe close_cqe;
    mel_vfs_poll_ticket(&vfs, t3, &close_cqe);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    backend->destroy(backend);
}

MEL_TEST(vfs_open_path_escape_is_permission_error, .tags = "vfs, async")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_backend_mock_inject_file(backend, S8("secret.txt"), (const u8*)"x", 1);

    u64 t = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe sqe = {
        .ticket = t,
        .op = MEL_VFS_OP_OPEN,
        .open = {
            .path = S8("/../../secret.txt"),
            .open_flags = MEL_VFS_OPEN_READ,
        },
    };
    mel_vfs_submit(&vfs, &sqe, 1);

    Mel_Vfs_Cqe cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, t, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_VFS_STATUS_PERMISSION);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

static bool mel__test_enum_stop_after_one(str8 path, const Mel_Vfs_Stat* stat, void* user)
{
    MEL_UNUSED(path);
    MEL_UNUSED(stat);
    i32* count = (i32*)user;
    (*count)++;
    return false;
}

MEL_TEST(vfs_enumerate_early_stop, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_backend_mock_inject_file(backend, S8("a.txt"), (const u8*)"a", 1);
    mel_vfs_backend_mock_inject_file(backend, S8("b.txt"), (const u8*)"b", 1);
    mel_vfs_backend_mock_inject_file(backend, S8("c.txt"), (const u8*)"c", 1);

    i32 count = 0;
    bool ok = mel_vfs_enumerate(&vfs, S8("/"), mel__test_enum_stop_after_one, &count);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(count, 1);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_enumerate_recursive, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_backend_mock_inject_file(backend, S8("top.txt"), (const u8*)"t", 1);
    mel_vfs_backend_mock_inject_dir(backend, S8("sub"));
    mel_vfs_backend_mock_inject_file(backend, S8("sub/nested.txt"), (const u8*)"n", 1);

    Mel__Test_Enum_Ctx ctx = {0};
    bool ok = mel_vfs_enumerate(&vfs, S8("/"), mel__test_enum_counter, &ctx, .recursive = true);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(ctx.count, 2);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_enumerate_recursive_include_dirs, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_backend_mock_inject_file(backend, S8("top.txt"), (const u8*)"t", 1);
    mel_vfs_backend_mock_inject_dir(backend, S8("sub"));
    mel_vfs_backend_mock_inject_file(backend, S8("sub/nested.txt"), (const u8*)"n", 1);

    Mel__Test_Enum_Ctx ctx = {0};
    bool ok = mel_vfs_enumerate(&vfs, S8("/"), mel__test_enum_counter, &ctx, .recursive = true, .include_dirs = true);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(ctx.count, 3);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_enumerate_empty_dir, .tags = "vfs")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    Mel__Test_Enum_Ctx ctx = {0};
    bool ok = mel_vfs_enumerate(&vfs, S8("/"), mel__test_enum_counter, &ctx);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(ctx.count, 0);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_read_at_offset, .tags = "vfs, async")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_backend_mock_inject_file(backend, S8("offset.bin"), (const u8*)"ABCDEFGH", 8);

    u64 t1 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe open_sqe = {
        .ticket = t1,
        .op = MEL_VFS_OP_OPEN,
        .open = { .path = S8("/offset.bin"), .open_flags = MEL_VFS_OPEN_READ },
    };
    mel_vfs_submit(&vfs, &open_sqe, 1);
    Mel_Vfs_Cqe open_cqe;
    mel_vfs_poll_ticket(&vfs, t1, &open_cqe);
    Mel_Vfs_File file = open_cqe.file;

    u8 buf[4] = {0};
    u64 t2 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe read_sqe = {
        .ticket = t2,
        .op = MEL_VFS_OP_READ,
        .read = { .file = file, .offset = 4, .dst = buf, .size = 4 },
    };
    mel_vfs_submit(&vfs, &read_sqe, 1);
    Mel_Vfs_Cqe read_cqe;
    mel_vfs_poll_ticket(&vfs, t2, &read_cqe);
    MEL_ASSERT_EQ(read_cqe.status, (u32)MEL_VFS_STATUS_OK);
    MEL_ASSERT_EQ(read_cqe.result, (i64)4);
    MEL_ASSERT_EQ(buf[0], (u8)'E');
    MEL_ASSERT_EQ(buf[3], (u8)'H');

    u64 t3 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe close_sqe = { .ticket = t3, .op = MEL_VFS_OP_CLOSE, .close = { .file = file } };
    mel_vfs_submit(&vfs, &close_sqe, 1);
    Mel_Vfs_Cqe close_cqe;
    mel_vfs_poll_ticket(&vfs, t3, &close_cqe);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_write_at_offset, .tags = "vfs, async")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_write_text(&vfs, S8("/gap.bin"), S8("AABBCCDD"));

    u64 t1 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe open_sqe = {
        .ticket = t1,
        .op = MEL_VFS_OP_OPEN,
        .open = { .path = S8("/gap.bin"), .open_flags = MEL_VFS_OPEN_WRITE },
    };
    mel_vfs_submit(&vfs, &open_sqe, 1);
    Mel_Vfs_Cqe open_cqe;
    mel_vfs_poll_ticket(&vfs, t1, &open_cqe);
    Mel_Vfs_File file = open_cqe.file;

    u64 t2 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe write_sqe = {
        .ticket = t2,
        .op = MEL_VFS_OP_WRITE,
        .write = { .file = file, .offset = 2, .src = (const u8*)"XX", .size = 2 },
    };
    mel_vfs_submit(&vfs, &write_sqe, 1);
    Mel_Vfs_Cqe write_cqe;
    mel_vfs_poll_ticket(&vfs, t2, &write_cqe);
    MEL_ASSERT_EQ(write_cqe.status, (u32)MEL_VFS_STATUS_OK);
    MEL_ASSERT_EQ(write_cqe.result, (i64)2);

    u64 t3 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe close_sqe = { .ticket = t3, .op = MEL_VFS_OP_CLOSE, .close = { .file = file } };
    mel_vfs_submit(&vfs, &close_sqe, 1);
    Mel_Vfs_Cqe close_cqe;
    mel_vfs_poll_ticket(&vfs, t3, &close_cqe);

    str8 text = mel_vfs_read_text_alloc(&vfs, S8("/gap.bin"), mel_alloc_heap());
    MEL_ASSERT_EQ(text.len, (size)8);
    MEL_ASSERT_EQ(text.data[0], (u8)'A');
    MEL_ASSERT_EQ(text.data[2], (u8)'X');
    MEL_ASSERT_EQ(text.data[3], (u8)'X');
    MEL_ASSERT_EQ(text.data[4], (u8)'C');
    mel_dealloc(mel_alloc_heap(), text.data);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_user_data_passthrough, .tags = "vfs, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Vfs_Backend* backend = mel_vfs_backend_mock_create(alloc);
    mel_vfs_backend_mock_inject_file(backend, S8("ud.txt"), (const u8*)"x", 1);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), backend, 0, true);

    u64 sentinel = 0xDEADBEEF;
    u64 ticket = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe sqe = {
        .ticket = ticket,
        .op = MEL_VFS_OP_STAT,
        .stat = { .path = S8("/ud.txt") },
        .user_data = &sentinel,
    };
    mel_vfs_submit(&vfs, &sqe, 1);

    Mel_Vfs_Cqe cqe;
    mel_vfs_poll_ticket(&vfs, ticket, &cqe);
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_VFS_STATUS_OK);
    MEL_ASSERT_EQ(cqe.user_data, (void*)&sentinel);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    mel_vfs_backend_mock_destroy(backend);
}

MEL_TEST(vfs_open_nonexistent_no_create, .tags = "vfs, async")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    u64 ticket = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe sqe = {
        .ticket = ticket,
        .op = MEL_VFS_OP_OPEN,
        .open = { .path = S8("/ghost.bin"), .open_flags = MEL_VFS_OPEN_READ },
    };
    mel_vfs_submit(&vfs, &sqe, 1);

    Mel_Vfs_Cqe cqe;
    mel_vfs_poll_ticket(&vfs, ticket, &cqe);
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_VFS_STATUS_IO_ERROR);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

MEL_TEST(vfs_mount_insertion_order_tiebreak, .tags = "vfs")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Vfs_Backend* first = mel_vfs_backend_mock_create(alloc);
    Mel_Vfs_Backend* second = mel_vfs_backend_mock_create(alloc);

    mel_vfs_backend_mock_inject_file(first, S8("tie.txt"), (const u8*)"first", 5);
    mel_vfs_backend_mock_inject_file(second, S8("tie.txt"), (const u8*)"second", 6);

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);
    mel_vfs_mount(&vfs, S8("/"), first, 0, true);
    mel_vfs_mount(&vfs, S8("/"), second, 0, true);

    str8 text = mel_vfs_read_text_alloc(&vfs, S8("/tie.txt"), alloc);
    MEL_ASSERT(str8_equals(text, S8("first")));
    mel_dealloc(alloc, text.data);

    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_unmount(&vfs, S8("/"));
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
    mel_vfs_backend_mock_destroy(second);
    mel_vfs_backend_mock_destroy(first);
}

MEL_TEST(vfs_read_past_eof, .tags = "vfs, async")
{
    Mel_Io io;
    Mel_Vfs vfs;
    Mel_Vfs_Backend* backend;
    mel__test_vfs_setup(&io, &vfs, &backend);

    mel_vfs_backend_mock_inject_file(backend, S8("small.bin"), (const u8*)"AB", 2);

    u64 t1 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe open_sqe = {
        .ticket = t1,
        .op = MEL_VFS_OP_OPEN,
        .open = { .path = S8("/small.bin"), .open_flags = MEL_VFS_OPEN_READ },
    };
    mel_vfs_submit(&vfs, &open_sqe, 1);
    Mel_Vfs_Cqe open_cqe;
    mel_vfs_poll_ticket(&vfs, t1, &open_cqe);
    Mel_Vfs_File file = open_cqe.file;

    u8 buf[16] = {0};
    u64 t2 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe read_sqe = {
        .ticket = t2,
        .op = MEL_VFS_OP_READ,
        .read = { .file = file, .offset = 100, .dst = buf, .size = sizeof(buf) },
    };
    mel_vfs_submit(&vfs, &read_sqe, 1);
    Mel_Vfs_Cqe read_cqe;
    mel_vfs_poll_ticket(&vfs, t2, &read_cqe);
    MEL_ASSERT_EQ(read_cqe.status, (u32)MEL_VFS_STATUS_OK);
    MEL_ASSERT_EQ(read_cqe.result, (i64)0);

    u64 t3 = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe close_sqe = { .ticket = t3, .op = MEL_VFS_OP_CLOSE, .close = { .file = file } };
    mel_vfs_submit(&vfs, &close_sqe, 1);
    Mel_Vfs_Cqe close_cqe;
    mel_vfs_poll_ticket(&vfs, t3, &close_cqe);

    mel__test_vfs_teardown(&io, &vfs, backend);
}

static bool mel__test_io_cancel_noop_handler_called;
static void mel__test_io_cancel_noop(void* ctx, const Mel_Io_Sqe* sqe, Mel_Io_Cqe* cqe)
{
    MEL_UNUSED(ctx);
    MEL_UNUSED(sqe);
    cqe->status = MEL_IO_STATUS_OK;
    mel__test_io_cancel_noop_handler_called = true;
}

MEL_TEST(io_cancel_basic, .tags = "async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = alloc, .sq_capacity = 64, .cq_capacity = 64, .worker_count = 0 };
    mel_io_init(&io, &desc);

    u16 hid = mel_io_register_handler(&io, mel__test_io_cancel_noop, NULL);

    u64 t1 = mel_io_next_ticket(&io);
    u64 t2 = mel_io_next_ticket(&io);
    u64 t3 = mel_io_next_ticket(&io);

    mel_io_cancel(&io, t2);

    Mel_Io_Sqe sqes[3] = {
        { .ticket = t1, .handler_id = hid, .op = 0 },
        { .ticket = t2, .handler_id = hid, .op = 0 },
        { .ticket = t3, .handler_id = hid, .op = 0 },
    };

    mel__test_io_cancel_noop_handler_called = false;
    mel_io_submit(&io, sqes, 3);

    Mel_Io_Cqe cqe;
    MEL_ASSERT(mel_io_poll_ticket(&io, t1, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_OK);

    MEL_ASSERT(mel_io_poll_ticket(&io, t2, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_CANCELLED);

    MEL_ASSERT(mel_io_poll_ticket(&io, t3, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_OK);

    mel_io_shutdown(&io);
}

MEL_TEST(io_cancel_nonexistent, .tags = "async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = alloc, .sq_capacity = 16, .cq_capacity = 16, .worker_count = 0 };
    mel_io_init(&io, &desc);

    mel_io_cancel(&io, 99999);

    u16 hid = mel_io_register_handler(&io, mel__test_io_cancel_noop, NULL);
    u64 t1 = mel_io_next_ticket(&io);
    Mel_Io_Sqe sqe = { .ticket = t1, .handler_id = hid, .op = 0 };
    mel_io_submit(&io, &sqe, 1);

    Mel_Io_Cqe cqe;
    MEL_ASSERT(mel_io_poll_ticket(&io, t1, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_OK);

    mel_io_shutdown(&io);
}

MEL_TEST(io_cancel_chain_propagation, .tags = "async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = alloc, .sq_capacity = 64, .cq_capacity = 64, .worker_count = 0 };
    mel_io_init(&io, &desc);

    u16 hid = mel_io_register_handler(&io, mel__test_io_cancel_noop, NULL);

    u64 t1 = mel_io_next_ticket(&io);
    u64 t2 = mel_io_next_ticket(&io);
    u64 t3 = mel_io_next_ticket(&io);

    mel_io_cancel(&io, t1);

    Mel_Io_Sqe sqes[3] = {
        { .ticket = t1, .handler_id = hid, .op = 0, .flags = MEL_IO_SQE_F_LINK_NEXT },
        { .ticket = t2, .handler_id = hid, .op = 0, .flags = MEL_IO_SQE_F_LINK_NEXT },
        { .ticket = t3, .handler_id = hid, .op = 0 },
    };
    mel_io_submit(&io, sqes, 3);

    Mel_Io_Cqe cqe;
    MEL_ASSERT(mel_io_poll_ticket(&io, t1, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_CANCELLED);

    MEL_ASSERT(mel_io_poll_ticket(&io, t2, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_CANCELLED);

    MEL_ASSERT(mel_io_poll_ticket(&io, t3, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_CANCELLED);

    mel_io_shutdown(&io);
}

static u64 mel__test_qos_completion_order[8];
static i32 mel__test_qos_completion_idx;

static void mel__test_qos_record_handler(void* ctx, const Mel_Io_Sqe* sqe, Mel_Io_Cqe* cqe)
{
    MEL_UNUSED(ctx);
    mel__test_qos_completion_order[mel__test_qos_completion_idx++] = sqe->ticket;
    cqe->status = MEL_IO_STATUS_OK;
}

MEL_TEST(io_qos_lane_ordering, .tags = "async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = alloc, .sq_capacity = 64, .cq_capacity = 64, .worker_count = 0 };
    mel_io_init(&io, &desc);

    u16 hid = mel_io_register_handler(&io, mel__test_qos_record_handler, NULL);

    u64 t_bulk = mel_io_next_ticket(&io);
    u64 t_crit = mel_io_next_ticket(&io);

    mel__test_qos_completion_idx = 0;

    Mel_Io_Sqe bulk_sqe = { .ticket = t_bulk, .handler_id = hid, .qos_class = MEL_IO_QOS_BULK };
    mel_io_submit(&io, &bulk_sqe, 1);

    MEL_ASSERT(mel_io_poll_ticket(&io, t_bulk, &(Mel_Io_Cqe){0}));

    mel__test_qos_completion_idx = 0;

    Mel_Io_Sqe sqes[2] = {
        { .ticket = t_bulk = mel_io_next_ticket(&io), .handler_id = hid, .qos_class = MEL_IO_QOS_BULK },
        { .ticket = t_crit = mel_io_next_ticket(&io), .handler_id = hid, .qos_class = MEL_IO_QOS_LATENCY_CRITICAL },
    };
    mel_io_submit(&io, &sqes[0], 1);
    mel_io_submit(&io, &sqes[1], 1);

    mel__test_qos_completion_idx = 0;

    Mel_Io_Cqe cqe;
    MEL_ASSERT(mel_io_poll_ticket(&io, t_crit, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_OK);
    MEL_ASSERT(mel_io_poll_ticket(&io, t_bulk, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_OK);

    mel_io_shutdown(&io);
}

MEL_TEST(io_qos_deadline_expired, .tags = "async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = alloc, .sq_capacity = 64, .cq_capacity = 64, .worker_count = 0 };
    mel_io_init(&io, &desc);

    u16 hid = mel_io_register_handler(&io, mel__test_io_cancel_noop, NULL);

    u64 t1 = mel_io_next_ticket(&io);
    Mel_Io_Sqe sqe = { .ticket = t1, .handler_id = hid, .deadline_ns = 1 };
    mel_io_submit(&io, &sqe, 1);

    Mel_Io_Cqe cqe;
    MEL_ASSERT(mel_io_poll_ticket(&io, t1, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_TIMEOUT);

    mel_io_shutdown(&io);
}

MEL_TEST(io_qos_deadline_not_expired, .tags = "async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = alloc, .sq_capacity = 64, .cq_capacity = 64, .worker_count = 0 };
    mel_io_init(&io, &desc);

    u16 hid = mel_io_register_handler(&io, mel__test_io_cancel_noop, NULL);

    u64 t1 = mel_io_next_ticket(&io);
    u64 far_future = SDL_GetTicksNS() + 60000000000ULL;
    Mel_Io_Sqe sqe = { .ticket = t1, .handler_id = hid, .deadline_ns = far_future };
    mel_io_submit(&io, &sqe, 1);

    Mel_Io_Cqe cqe;
    MEL_ASSERT(mel_io_poll_ticket(&io, t1, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_OK);

    mel_io_shutdown(&io);
}

MEL_TEST(io_qos_chain_same_lane, .tags = "async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = alloc, .sq_capacity = 64, .cq_capacity = 64, .worker_count = 0 };
    mel_io_init(&io, &desc);

    u16 hid = mel_io_register_handler(&io, mel__test_qos_record_handler, NULL);

    u64 t1 = mel_io_next_ticket(&io);
    u64 t2 = mel_io_next_ticket(&io);

    mel__test_qos_completion_idx = 0;

    Mel_Io_Sqe sqes[2] = {
        { .ticket = t1, .handler_id = hid, .qos_class = MEL_IO_QOS_STREAMING, .flags = MEL_IO_SQE_F_LINK_NEXT },
        { .ticket = t2, .handler_id = hid, .qos_class = MEL_IO_QOS_STREAMING },
    };
    mel_io_submit(&io, sqes, 2);

    MEL_ASSERT_EQ(mel__test_qos_completion_order[0], t1);
    MEL_ASSERT_EQ(mel__test_qos_completion_order[1], t2);

    Mel_Io_Cqe cqe;
    MEL_ASSERT(mel_io_poll_ticket(&io, t1, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_OK);
    MEL_ASSERT(mel_io_poll_ticket(&io, t2, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_OK);

    mel_io_shutdown(&io);
}
