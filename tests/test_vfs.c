#include "../melody/test.harness.h"
#include "../melody/vfs.h"
#include "../melody/vfs.backend.os.h"
#include "../melody/vfs.backend.mock.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/string.str8.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

MEL_TEST(vfs_mock_read_file, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    Mel_Vfs_Mock* mock = mel_vfs_mock_create(mel_alloc_heap());
    mel_vfs_mock_add_file(mock, "/hello.txt", "Hello VFS!", 10);

    mel_vfs_mount(S8("/test"), mel_vfs_mock_backend(mock), .root = S8("/"));

    i64 fsize = 0;
    u8* data = mel_vfs_read_file(S8("/test/hello.txt"), &fsize, mel_alloc_heap());

    MEL_ASSERT_NOT_NULL(data);
    MEL_ASSERT_EQ(fsize, 10);
    MEL_ASSERT(memcmp(data, "Hello VFS!", 10) == 0);

    mel_dealloc(mel_alloc_heap(), data);
    mel_vfs_unmount(S8("/test"));
    mel_vfs_mock_destroy(mock);
    mel_vfs_shutdown();
}

MEL_TEST(vfs_mock_write_read_roundtrip, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    Mel_Vfs_Mock* mock = mel_vfs_mock_create(mel_alloc_heap());
    mel_vfs_mount(S8("/data"), mel_vfs_mock_backend(mock), .root = S8("/"), .writable = true);

    bool ok = mel_vfs_write_file(S8("/data/out.txt"), "written data", 12);
    MEL_ASSERT(ok);

    i64 fsize = 0;
    u8* data = mel_vfs_read_file(S8("/data/out.txt"), &fsize, mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(data);
    MEL_ASSERT_EQ(fsize, 12);
    MEL_ASSERT(memcmp(data, "written data", 12) == 0);

    mel_dealloc(mel_alloc_heap(), data);
    mel_vfs_unmount(S8("/data"));
    mel_vfs_mock_destroy(mock);
    mel_vfs_shutdown();
}

MEL_TEST(vfs_mock_nonexistent_file, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    Mel_Vfs_Mock* mock = mel_vfs_mock_create(mel_alloc_heap());
    mel_vfs_mount(S8("/test"), mel_vfs_mock_backend(mock), .root = S8("/"));

    i64 fsize = 0;
    u8* data = mel_vfs_read_file(S8("/test/nope.txt"), &fsize, mel_alloc_heap());
    MEL_ASSERT_NULL(data);

    mel_vfs_unmount(S8("/test"));
    mel_vfs_mock_destroy(mock);
    mel_vfs_shutdown();
}

MEL_TEST(vfs_mock_exists, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    Mel_Vfs_Mock* mock = mel_vfs_mock_create(mel_alloc_heap());
    mel_vfs_mock_add_file(mock, "/a.txt", "data", 4);
    mel_vfs_mount(S8("/test"), mel_vfs_mock_backend(mock), .root = S8("/"));

    MEL_ASSERT(mel_vfs_exists(S8("/test/a.txt")));
    MEL_ASSERT(!mel_vfs_exists(S8("/test/b.txt")));

    mel_vfs_unmount(S8("/test"));
    mel_vfs_mock_destroy(mock);
    mel_vfs_shutdown();
}

MEL_TEST(vfs_mock_stat, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    Mel_Vfs_Mock* mock = mel_vfs_mock_create(mel_alloc_heap());
    mel_vfs_mock_add_file(mock, "/info.bin", "12345", 5);
    mel_vfs_mount(S8("/test"), mel_vfs_mock_backend(mock), .root = S8("/"));

    Mel_Vfs_Stat st = {0};
    bool ok = mel_vfs_stat(S8("/test/info.bin"), &st);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(st.size, 5);
    MEL_ASSERT(st.flags & MEL_VFS_STAT_IS_FILE);

    mel_vfs_unmount(S8("/test"));
    mel_vfs_mock_destroy(mock);
    mel_vfs_shutdown();
}

MEL_TEST(vfs_mock_delete, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    Mel_Vfs_Mock* mock = mel_vfs_mock_create(mel_alloc_heap());
    mel_vfs_mock_add_file(mock, "/del.txt", "bye", 3);
    mel_vfs_mount(S8("/test"), mel_vfs_mock_backend(mock), .root = S8("/"), .writable = true);

    MEL_ASSERT(mel_vfs_exists(S8("/test/del.txt")));
    MEL_ASSERT(mel_vfs_delete(S8("/test/del.txt")));
    MEL_ASSERT(!mel_vfs_exists(S8("/test/del.txt")));

    mel_vfs_unmount(S8("/test"));
    mel_vfs_mock_destroy(mock);
    mel_vfs_shutdown();
}

MEL_TEST(vfs_unmount_blocks_reads, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    Mel_Vfs_Mock* mock = mel_vfs_mock_create(mel_alloc_heap());
    mel_vfs_mock_add_file(mock, "/x.txt", "hi", 2);
    mel_vfs_mount(S8("/mnt"), mel_vfs_mock_backend(mock), .root = S8("/"));

    MEL_ASSERT(mel_vfs_exists(S8("/mnt/x.txt")));

    mel_vfs_unmount(S8("/mnt"));

    i64 fsize = 0;
    u8* data = mel_vfs_read_file(S8("/mnt/x.txt"), &fsize, mel_alloc_heap());
    MEL_ASSERT_NULL(data);

    mel_vfs_mock_destroy(mock);
    mel_vfs_shutdown();
}

MEL_TEST(vfs_longest_prefix_match, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    Mel_Vfs_Mock* mock_a = mel_vfs_mock_create(mel_alloc_heap());
    Mel_Vfs_Mock* mock_b = mel_vfs_mock_create(mel_alloc_heap());

    mel_vfs_mock_add_file(mock_a, "/file.txt", "from_a", 6);
    mel_vfs_mock_add_file(mock_b, "/file.txt", "from_b", 6);

    mel_vfs_mount(S8("/data"), mel_vfs_mock_backend(mock_a), .root = S8("/"));
    mel_vfs_mount(S8("/data/sub"), mel_vfs_mock_backend(mock_b), .root = S8("/"));

    i64 fsize = 0;
    u8* data = mel_vfs_read_file(S8("/data/sub/file.txt"), &fsize, mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(data);
    MEL_ASSERT(memcmp(data, "from_b", 6) == 0);
    mel_dealloc(mel_alloc_heap(), data);

    mel_vfs_unmount(S8("/data"));
    mel_vfs_unmount(S8("/data/sub"));
    mel_vfs_mock_destroy(mock_a);
    mel_vfs_mock_destroy(mock_b);
    mel_vfs_shutdown();
}

MEL_TEST(vfs_priority_override, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    Mel_Vfs_Mock* mock_low = mel_vfs_mock_create(mel_alloc_heap());
    Mel_Vfs_Mock* mock_high = mel_vfs_mock_create(mel_alloc_heap());

    mel_vfs_mock_add_file(mock_low, "/f.txt", "low", 3);
    mel_vfs_mock_add_file(mock_high, "/f.txt", "high", 4);

    mel_vfs_mount(S8("/test"), mel_vfs_mock_backend(mock_low), .root = S8("/"), .priority = 0);
    mel_vfs_mount(S8("/test"), mel_vfs_mock_backend(mock_high), .root = S8("/"), .priority = 10);

    i64 fsize = 0;
    u8* data = mel_vfs_read_file(S8("/test/f.txt"), &fsize, mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(data);
    MEL_ASSERT_EQ(fsize, 4);
    MEL_ASSERT(memcmp(data, "high", 4) == 0);

    mel_dealloc(mel_alloc_heap(), data);
    mel_vfs_unmount(S8("/test"));
    mel_vfs_mock_destroy(mock_low);
    mel_vfs_mock_destroy(mock_high);
    mel_vfs_shutdown();
}

MEL_TEST(vfs_handle_read, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    Mel_Vfs_Mock* mock = mel_vfs_mock_create(mel_alloc_heap());
    mel_vfs_mock_add_file(mock, "/data.bin", "ABCDEFGH", 8);
    mel_vfs_mount(S8("/test"), mel_vfs_mock_backend(mock), .root = S8("/"));

    Mel_Vfs_Handle fh = mel_vfs_open(S8("/test/data.bin"), MEL_VFS_OPEN_READ);
    MEL_ASSERT_NEQ(fh.generation, 0u);

    MEL_ASSERT_EQ(mel_vfs_file_length(fh), 8);

    char buf[4] = {0};
    i64 n = mel_vfs_read_handle(fh, buf, 4, 2);
    MEL_ASSERT_EQ(n, 4);
    MEL_ASSERT(memcmp(buf, "CDEF", 4) == 0);

    mel_vfs_close(fh);

    mel_vfs_unmount(S8("/test"));
    mel_vfs_mock_destroy(mock);
    mel_vfs_shutdown();
}

MEL_TEST(vfs_os_read_file, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    int fd = open("/tmp/mel_test_vfs_os.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    write(fd, "OS backend test data", 20);
    close(fd);

    mel_vfs_mount(S8("/"), mel_vfs_backend_os(), .root = S8("/"));

    i64 fsize = 0;
    u8* data = mel_vfs_read_file(str8_from_cstr("/tmp/mel_test_vfs_os.txt"), &fsize, mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(data);
    MEL_ASSERT_EQ(fsize, 20);
    MEL_ASSERT(memcmp(data, "OS backend test data", 20) == 0);

    mel_dealloc(mel_alloc_heap(), data);
    mel_vfs_unmount(S8("/"));
    unlink("/tmp/mel_test_vfs_os.txt");
    mel_vfs_shutdown();
}

MEL_TEST(vfs_os_write_read_roundtrip, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    mel_vfs_mount(S8("/"), mel_vfs_backend_os(), .root = S8("/"), .writable = true);

    str8 path = str8_from_cstr("/tmp/mel_test_vfs_os_write.txt");
    MEL_ASSERT(mel_vfs_write_file(path, "written via VFS", 15));

    i64 fsize = 0;
    u8* data = mel_vfs_read_file(path, &fsize, mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(data);
    MEL_ASSERT_EQ(fsize, 15);
    MEL_ASSERT(memcmp(data, "written via VFS", 15) == 0);

    mel_dealloc(mel_alloc_heap(), data);
    mel_vfs_unmount(S8("/"));
    unlink("/tmp/mel_test_vfs_os_write.txt");
    mel_vfs_shutdown();
}

MEL_TEST(vfs_os_stat, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    int fd = open("/tmp/mel_test_vfs_os_stat.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    write(fd, "12345", 5);
    close(fd);

    mel_vfs_mount(S8("/"), mel_vfs_backend_os(), .root = S8("/"));

    Mel_Vfs_Stat st = {0};
    MEL_ASSERT(mel_vfs_stat(str8_from_cstr("/tmp/mel_test_vfs_os_stat.txt"), &st));
    MEL_ASSERT_EQ(st.size, 5);
    MEL_ASSERT(st.flags & MEL_VFS_STAT_IS_FILE);

    mel_vfs_unmount(S8("/"));
    unlink("/tmp/mel_test_vfs_os_stat.txt");
    mel_vfs_shutdown();
}

MEL_TEST(vfs_os_exists, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    mel_vfs_mount(S8("/"), mel_vfs_backend_os(), .root = S8("/"));

    MEL_ASSERT(mel_vfs_exists(S8("/tmp")));
    MEL_ASSERT(!mel_vfs_exists(str8_from_cstr("/tmp/mel_vfs_definitely_does_not_exist_xyz")));

    mel_vfs_unmount(S8("/"));
    mel_vfs_shutdown();
}

static bool vfs_enum_counter(str8 name, const Mel_Vfs_Stat* st, void* user)
{
    (void)name; (void)st;
    u32* count = user;
    (*count)++;
    return true;
}

MEL_TEST(vfs_os_enumerate, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    mkdir("/tmp/mel_test_vfs_enum", 0755);
    int fd1 = open("/tmp/mel_test_vfs_enum/a.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    write(fd1, "a", 1); close(fd1);
    int fd2 = open("/tmp/mel_test_vfs_enum/b.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    write(fd2, "bb", 2); close(fd2);

    mel_vfs_mount(S8("/"), mel_vfs_backend_os(), .root = S8("/"));

    u32 count = 0;
    mel_vfs_enumerate(str8_from_cstr("/tmp/mel_test_vfs_enum"), vfs_enum_counter, &count);
    MEL_ASSERT_GE(count, 2u);

    mel_vfs_unmount(S8("/"));
    unlink("/tmp/mel_test_vfs_enum/a.txt");
    unlink("/tmp/mel_test_vfs_enum/b.txt");
    rmdir("/tmp/mel_test_vfs_enum");
    mel_vfs_shutdown();
}

MEL_TEST(vfs_os_handle_mmap, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    int fd = open("/tmp/mel_test_vfs_mmap.bin", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    write(fd, "mmap test data 12345", 20);
    close(fd);

    mel_vfs_mount(S8("/"), mel_vfs_backend_os(), .root = S8("/"));

    Mel_Vfs_Handle fh = mel_vfs_open(str8_from_cstr("/tmp/mel_test_vfs_mmap.bin"), MEL_VFS_OPEN_READ);
    MEL_ASSERT_NEQ(fh.generation, 0u);

    i64 len = mel_vfs_file_length(fh);
    MEL_ASSERT_EQ(len, 20);

    Mel_Vfs_Map map = mel_vfs_map(fh, 0, len, MEL_VFS_MAP_READ);
    MEL_ASSERT_NOT_NULL(map.ptr);
    MEL_ASSERT_EQ(map.size, len);
    MEL_ASSERT(memcmp(map.ptr, "mmap test data 12345", 20) == 0);

    mel_vfs_unmap(map);
    mel_vfs_close(fh);
    mel_vfs_unmount(S8("/"));
    unlink("/tmp/mel_test_vfs_mmap.bin");
    mel_vfs_shutdown();
}

MEL_TEST(vfs_write_needs_writable_mount, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    Mel_Vfs_Mock* mock = mel_vfs_mock_create(mel_alloc_heap());
    mel_vfs_mount(S8("/test"), mel_vfs_mock_backend(mock), .root = S8("/"), .writable = false);

    bool ok = mel_vfs_write_file(S8("/test/new.txt"), "data", 4);
    MEL_ASSERT(!ok);

    mel_vfs_unmount(S8("/test"));
    mel_vfs_mock_destroy(mock);
    mel_vfs_shutdown();
}

MEL_TEST(vfs_no_mount_returns_null, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    i64 fsize = 0;
    u8* data = mel_vfs_read_file(S8("/unmounted/file.txt"), &fsize, mel_alloc_heap());
    MEL_ASSERT_NULL(data);

    mel_vfs_shutdown();
}

MEL_TEST(vfs_path_normalization, .tags = "vfs")
{
    mel_vfs_init(mel_alloc_heap());

    Mel_Vfs_Mock* mock = mel_vfs_mock_create(mel_alloc_heap());
    mel_vfs_mock_add_file(mock, "/file.txt", "norm", 4);
    mel_vfs_mount(S8("/test"), mel_vfs_mock_backend(mock), .root = S8("/"));

    i64 fsize = 0;
    u8* data;

    data = mel_vfs_read_file(S8("/test//file.txt"), &fsize, mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(data);
    mel_dealloc(mel_alloc_heap(), data);

    data = mel_vfs_read_file(S8("/test/./file.txt"), &fsize, mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(data);
    mel_dealloc(mel_alloc_heap(), data);

    data = mel_vfs_read_file(S8("/test/sub/../file.txt"), &fsize, mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(data);
    mel_dealloc(mel_alloc_heap(), data);

    mel_vfs_unmount(S8("/test"));
    mel_vfs_mock_destroy(mock);
    mel_vfs_shutdown();
}
