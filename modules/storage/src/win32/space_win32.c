#include <storage/storage.h>

#include <string/str8.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

Mel_Storage_Space mel_storage__native_space(str8 host_root)
{
    Mel_Storage_Space sp = { 0 };

    WCHAR  wpath[4096];
    int    n = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)host_root.data, (int)host_root.len, wpath, (int)(sizeof wpath / sizeof wpath[0]) - 1);
    if (n <= 0)
        return sp;
    wpath[n] = L'\0';

    ULARGE_INTEGER avail = { 0 };
    ULARGE_INTEGER total = { 0 };
    ULARGE_INTEGER freeb = { 0 };
    if (!GetDiskFreeSpaceExW(wpath, &avail, &total, &freeb))
        return sp;

    sp.total_bytes = (u64)total.QuadPart;
    sp.free_bytes = (u64)freeb.QuadPart;
    sp.available_bytes = (u64)avail.QuadPart;
    return sp;
}
