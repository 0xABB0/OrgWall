#include <storage/storage.h>

#include <string/str8.h>

#include <sys/statvfs.h>

Mel_Storage_Space mel_storage__native_space(str8 host_root)
{
    Mel_Storage_Space sp = { 0 };
    char              path[4096];
    usize             n = host_root.len < sizeof path - 1 ? host_root.len : sizeof path - 1;
    if (n > 0)
        __builtin_memcpy(path, host_root.data, n);
    path[n] = '\0';

    struct statvfs vfs;
    if (statvfs(path, &vfs) != 0)
        return sp;

    u64 frsize = vfs.f_frsize ? (u64)vfs.f_frsize : (u64)vfs.f_bsize;
    sp.total_bytes = (u64)vfs.f_blocks * frsize;
    sp.free_bytes = (u64)vfs.f_bfree * frsize;
    sp.available_bytes = (u64)vfs.f_bavail * frsize;
    return sp;
}
