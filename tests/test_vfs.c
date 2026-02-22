#include "../melody/test.harness.h"
#include "../melody/vfs.h"
#include "../melody/vfs.backend.mock.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/string.str8.h"
#include "../melody/string.path.h"

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

MEL_TEST(vfs_cancel_unsupported, .tags = "vfs, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Io io;
    Mel_Io_Desc io_desc = { .allocator = alloc, .worker_count = 0 };
    mel_io_init(&io, &io_desc);

    Mel_Vfs vfs;
    Mel_Vfs_Desc desc = { .allocator = alloc, .io = &io };
    mel_vfs_init(&vfs, &desc);

    u64 ticket = mel_vfs_next_ticket(&vfs);
    Mel_Vfs_Sqe sqe = {
        .ticket = ticket,
        .op = MEL_VFS_OP_CANCEL,
        .cancel = { .ticket_to_cancel = 42 },
    };
    mel_vfs_submit(&vfs, &sqe, 1);

    Mel_Vfs_Cqe cqe;
    MEL_ASSERT(mel_vfs_poll_ticket(&vfs, ticket, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_VFS_STATUS_UNSUPPORTED);

    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);
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
