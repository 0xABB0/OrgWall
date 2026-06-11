#include "../../src/fs_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <string/path.h>

#include <errno.h>
#include <string.h>

typedef Mel_Array(Mel_Fs_Dir_Entry) Fs_Entry_Array;

bool mel_fs__backend_available(void) { return true; }

static i64 win_filetime_ns(FILETIME ft)
{
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    i64 ticks = (i64)u.QuadPart;
    return (ticks - 116444736000000000ll) * 100ll;
}

static Mel_Fs_Status win_status_from_error(DWORD e)
{
    switch (e)
    {
    case ERROR_SUCCESS:
        return MEL_FS_OK;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
        return MEL_FS_ERROR | MEL_FS_NOT_FOUND;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        return MEL_FS_ERROR | MEL_FS_EXISTS;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        return MEL_FS_ERROR | MEL_FS_PERMISSION;
    case ERROR_DIR_NOT_EMPTY:
        return MEL_FS_ERROR | MEL_FS_NOT_EMPTY;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        return MEL_FS_ERROR | MEL_FS_NO_SPACE;
    case ERROR_DIRECTORY:
        return MEL_FS_ERROR | MEL_FS_NOT_A_DIRECTORY;
    case ERROR_FILENAME_EXCED_RANGE:
    case ERROR_BUFFER_OVERFLOW:
        return MEL_FS_ERROR | MEL_FS_NAME_TOO_LONG;
    case ERROR_NOT_SAME_DEVICE:
        return MEL_FS_ERROR | MEL_FS_CROSS_DEVICE;
    case ERROR_WRITE_PROTECT:
        return MEL_FS_ERROR | MEL_FS_READ_ONLY;
    default:
        return MEL_FS_ERROR;
    }
}

static WCHAR* utf8_to_wide(str8 s, const Mel_Alloc* alloc)
{
    if (s.len == 0)
    {
        WCHAR* w = mel_alloc(alloc, sizeof(WCHAR));
        if (w)
            w[0] = 0;
        return w;
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, NULL, 0);
    if (n <= 0)
        return NULL;
    WCHAR* w = mel_alloc(alloc, sizeof(WCHAR) * (usize)(n + 1));
    if (!w)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, w, n);
    w[n] = 0;
    return w;
}

static str8 wide_to_utf8(const WCHAR* w, const Mel_Alloc* alloc)
{
    int len = (int)wcslen(w);
    if (len == 0)
        return str8_dup_alloc(STR8_EMPTY, alloc);
    int n = WideCharToMultiByte(CP_UTF8, 0, w, len, NULL, 0, NULL, NULL);
    if (n <= 0)
        return STR8_EMPTY;
    u8* buf = mel_alloc(alloc, (usize)(n + 1));
    if (!buf)
        return STR8_EMPTY;
    WideCharToMultiByte(CP_UTF8, 0, w, len, (char*)buf, n, NULL, NULL);
    buf[n] = 0;
    return str8_from_parts(buf, n);
}

static Mel_Fs_Kind win_kind_from_attrs(DWORD a)
{
    if (a & FILE_ATTRIBUTE_REPARSE_POINT)
        return MEL_FS_KIND_SYMLINK;
    if (a & FILE_ATTRIBUTE_DIRECTORY)
        return MEL_FS_KIND_DIR;
    return MEL_FS_KIND_FILE;
}

void mel_fs__do_stat(Mel_Fs_Op_Record* op)
{
    WCHAR* wp = utf8_to_wide(op->path_a, op->alloc);
    if (!wp)
    {
        op->result.stat.status = MEL_FS_ERROR;
        op->result.stat.os_error = (i32)ERROR_INVALID_PARAMETER;
        return;
    }
    WIN32_FILE_ATTRIBUTE_DATA data;
    BOOL                      ok = GetFileAttributesExW(wp, GetFileExInfoStandard, &data);
    mel_dealloc(op->alloc, wp);
    if (!ok)
    {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
        {
            op->result.stat.value.exists = false;
            op->result.stat.status = MEL_FS_OK;
            return;
        }
        op->result.stat.status = win_status_from_error(e);
        op->result.stat.os_error = (i32)e;
        return;
    }
    Mel_Fs_Stat* st = &op->result.stat.value;
    st->exists = true;
    st->kind = win_kind_from_attrs(data.dwFileAttributes);
    st->size_bytes = ((u64)data.nFileSizeHigh << 32) | (u64)data.nFileSizeLow;
    st->ctime_ns = win_filetime_ns(data.ftCreationTime);
    st->mtime_ns = win_filetime_ns(data.ftLastWriteTime);
    st->atime_ns = win_filetime_ns(data.ftLastAccessTime);
    st->read_only = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    st->mode_bits = st->read_only ? 0444u : 0666u;
    op->result.stat.status = MEL_FS_OK;
}

void mel_fs__do_exists(Mel_Fs_Op_Record* op)
{
    WCHAR* wp = utf8_to_wide(op->path_a, op->alloc);
    if (!wp)
    {
        op->result.boolean.status = MEL_FS_ERROR;
        return;
    }
    DWORD a = GetFileAttributesW(wp);
    mel_dealloc(op->alloc, wp);
    if (a != INVALID_FILE_ATTRIBUTES)
    {
        op->result.boolean.existed = true;
        op->result.boolean.status = MEL_FS_OK;
        return;
    }
    DWORD e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
    {
        op->result.boolean.existed = false;
        op->result.boolean.status = MEL_FS_OK;
        return;
    }
    op->result.boolean.status = win_status_from_error(e);
    op->result.boolean.os_error = (i32)e;
}

static bool win_mkdir_parents(WCHAR* path, DWORD* out_err)
{
    usize len = wcslen(path);
    for (usize i = 1; i < len; i++)
    {
        if (path[i] == L'\\' || path[i] == L'/')
        {
            WCHAR save = path[i];
            path[i] = 0;
            if (!CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            {
                *out_err = GetLastError();
                path[i] = save;
                return false;
            }
            path[i] = save;
        }
    }
    if (!CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        *out_err = GetLastError();
        return false;
    }
    *out_err = ERROR_SUCCESS;
    return true;
}

void mel_fs__do_mkdir(Mel_Fs_Op_Record* op)
{
    WCHAR* wp = utf8_to_wide(op->path_a, op->alloc);
    DWORD  e = ERROR_SUCCESS;
    bool   ok;
    if (op->parents)
        ok = win_mkdir_parents(wp, &e);
    else
        ok = CreateDirectoryW(wp, NULL) || (e = GetLastError()) == ERROR_ALREADY_EXISTS;
    mel_dealloc(op->alloc, wp);
    op->result.voided.status = (ok || e == ERROR_ALREADY_EXISTS) ? MEL_FS_OK : win_status_from_error(e);
    op->result.voided.os_error = (i32)e;
}

static DWORD win_remove_recursive(WCHAR* path)
{
    DWORD a = GetFileAttributesW(path);
    if (a == INVALID_FILE_ATTRIBUTES)
        return GetLastError();
    if (!(a & FILE_ATTRIBUTE_DIRECTORY))
        return DeleteFileW(path) ? ERROR_SUCCESS : GetLastError();

    usize plen = wcslen(path);
    WCHAR pattern[MAX_PATH * 4];
    if (plen + 3 >= sizeof pattern / sizeof pattern[0])
        return ERROR_FILENAME_EXCED_RANGE;
    wcscpy(pattern, path);
    wcscat(pattern, L"\\*");

    WIN32_FIND_DATAW fd;
    HANDLE           h = FindFirstFileW(pattern, &fd);
    DWORD            e = ERROR_SUCCESS;
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            WCHAR child[MAX_PATH * 4];
            if ((usize)swprintf(child, sizeof child / sizeof child[0], L"%s\\%s", path, fd.cFileName) >= sizeof child / sizeof child[0])
            {
                e = ERROR_FILENAME_EXCED_RANGE;
                break;
            }
            e = win_remove_recursive(child);
            if (e != ERROR_SUCCESS)
                break;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (e != ERROR_SUCCESS)
        return e;
    return RemoveDirectoryW(path) ? ERROR_SUCCESS : GetLastError();
}

void mel_fs__do_remove(Mel_Fs_Op_Record* op)
{
    WCHAR* wp = utf8_to_wide(op->path_a, op->alloc);
    DWORD  e = ERROR_SUCCESS;
    if (op->recursive)
    {
        e = win_remove_recursive(wp);
    }
    else
    {
        DWORD a = GetFileAttributesW(wp);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY))
            e = RemoveDirectoryW(wp) ? ERROR_SUCCESS : GetLastError();
        else
            e = DeleteFileW(wp) ? ERROR_SUCCESS : GetLastError();
    }
    mel_dealloc(op->alloc, wp);
    op->result.voided.status = win_status_from_error(e);
    op->result.voided.os_error = (i32)e;
}

void mel_fs__do_rename(Mel_Fs_Op_Record* op)
{
    WCHAR* from = utf8_to_wide(op->path_a, op->alloc);
    WCHAR* to = utf8_to_wide(op->path_b, op->alloc);
    DWORD  flags = MOVEFILE_COPY_ALLOWED;
    if (op->overwrite)
        flags |= MOVEFILE_REPLACE_EXISTING;
    DWORD e = MoveFileExW(from, to, flags) ? ERROR_SUCCESS : GetLastError();
    mel_dealloc(op->alloc, from);
    mel_dealloc(op->alloc, to);
    op->result.voided.status = win_status_from_error(e);
    op->result.voided.os_error = (i32)e;
}

void mel_fs__do_copy(Mel_Fs_Op_Record* op)
{
    WCHAR* from = utf8_to_wide(op->path_a, op->alloc);
    WCHAR* to = utf8_to_wide(op->path_b, op->alloc);
    BOOL   fail_if_exists = op->overwrite ? FALSE : TRUE;
    DWORD  e = CopyFileW(from, to, fail_if_exists) ? ERROR_SUCCESS : GetLastError();
    mel_dealloc(op->alloc, from);
    mel_dealloc(op->alloc, to);
    op->result.voided.status = win_status_from_error(e);
    op->result.voided.os_error = (i32)e;
}

void mel_fs__do_read_file(Mel_Fs_Op_Record* op)
{
    WCHAR* wp = utf8_to_wide(op->path_a, op->alloc);
    HANDLE h = CreateFileW(wp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    mel_dealloc(op->alloc, wp);
    if (h == INVALID_HANDLE_VALUE)
    {
        DWORD e = GetLastError();
        op->result.bytes.status = win_status_from_error(e);
        op->result.bytes.os_error = (i32)e;
        return;
    }
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    usize cap = (usize)sz.QuadPart;
    u8*   buf = cap > 0 ? mel_alloc(op->alloc, cap) : NULL;
    if (cap > 0 && !buf)
    {
        CloseHandle(h);
        op->result.bytes.status = MEL_FS_ERROR;
        op->result.bytes.os_error = (i32)ERROR_OUTOFMEMORY;
        return;
    }
    usize off = 0;
    DWORD e = ERROR_SUCCESS;
    while (off < cap)
    {
        DWORD want = (DWORD)((cap - off) > 0x40000000u ? 0x40000000u : (cap - off));
        DWORD got = 0;
        if (!ReadFile(h, buf + off, want, &got, NULL))
        {
            e = GetLastError();
            break;
        }
        if (got == 0)
            break;
        off += got;
    }
    CloseHandle(h);
    if (e != ERROR_SUCCESS)
    {
        if (buf)
            mel_dealloc(op->alloc, buf);
        op->result.bytes.status = win_status_from_error(e);
        op->result.bytes.os_error = (i32)e;
        return;
    }
    op->result.bytes.data = buf;
    op->result.bytes.len = off;
    op->result.bytes.status = MEL_FS_OK;
}

void mel_fs__do_write_file(Mel_Fs_Op_Record* op)
{
    if (op->create_parents)
    {
        str8 parent = mel_path_parent(op->path_a);
        if (parent.len > 0)
        {
            WCHAR* wpar = utf8_to_wide(parent, op->alloc);
            DWORD  perr = ERROR_SUCCESS;
            if (wpar)
            {
                win_mkdir_parents(wpar, &perr);
                mel_dealloc(op->alloc, wpar);
            }
        }
    }

    str8 target = op->path_a;
    str8 tmp = STR8_EMPTY;
    if (op->atomic)
    {
        tmp = str8_fmt_alloc(op->alloc, "%.*s.mel-tmp-%lu", (int)op->path_a.len, op->path_a.data, (unsigned long)GetCurrentProcessId());
        target = tmp;
    }

    WCHAR* wp = utf8_to_wide(target, op->alloc);
    HANDLE h = CreateFileW(wp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    mel_dealloc(op->alloc, wp);
    DWORD e = ERROR_SUCCESS;
    if (h == INVALID_HANDLE_VALUE)
    {
        e = GetLastError();
    }
    else
    {
        usize off = 0;
        while (off < op->write_len)
        {
            DWORD want = (DWORD)((op->write_len - off) > 0x40000000u ? 0x40000000u : (op->write_len - off));
            DWORD wrote = 0;
            if (!WriteFile(h, op->write_data + off, want, &wrote, NULL))
            {
                e = GetLastError();
                break;
            }
            off += wrote;
        }
        FlushFileBuffers(h);
        CloseHandle(h);
    }

    if (op->atomic)
    {
        if (e == ERROR_SUCCESS)
        {
            WCHAR* wtmp = utf8_to_wide(tmp, op->alloc);
            WCHAR* wdst = utf8_to_wide(op->path_a, op->alloc);
            if (!MoveFileExW(wtmp, wdst, MOVEFILE_REPLACE_EXISTING))
            {
                e = GetLastError();
                DeleteFileW(wtmp);
            }
            mel_dealloc(op->alloc, wtmp);
            mel_dealloc(op->alloc, wdst);
        }
        else
        {
            WCHAR* wtmp = utf8_to_wide(tmp, op->alloc);
            if (wtmp)
            {
                DeleteFileW(wtmp);
                mel_dealloc(op->alloc, wtmp);
            }
        }
        if (tmp.data)
            mel_dealloc(op->alloc, tmp.data);
    }

    op->result.voided.status = win_status_from_error(e);
    op->result.voided.os_error = (i32)e;
}

static void win_push_entry(Mel_Fs_Op_Record* op, Fs_Entry_Array* arr, const WIN32_FIND_DATAW* fd)
{
    Mel_Fs_Dir_Entry e = { 0 };
    e.name = wide_to_utf8(fd->cFileName, op->alloc);
    e.kind = win_kind_from_attrs(fd->dwFileAttributes);
    e.size_bytes = ((u64)fd->nFileSizeHigh << 32) | (u64)fd->nFileSizeLow;
    e.mtime_ns = win_filetime_ns(fd->ftLastWriteTime);
    mel_array_push(arr, e);
}

void mel_fs__do_enumerate(Mel_Fs_Op_Record* op)
{
    str8   pattern = str8_fmt_alloc(op->alloc, "%.*s\\*", (int)op->path_a.len, op->path_a.data);
    WCHAR* wp = utf8_to_wide(pattern, op->alloc);
    if (pattern.data)
        mel_dealloc(op->alloc, pattern.data);

    WIN32_FIND_DATAW fd;
    HANDLE           h = FindFirstFileW(wp, &fd);
    mel_dealloc(op->alloc, wp);
    if (h == INVALID_HANDLE_VALUE)
    {
        DWORD e = GetLastError();
        op->result.dir.status = win_status_from_error(e);
        op->result.dir.os_error = (i32)e;
        return;
    }

    Fs_Entry_Array arr;
    mel_array_init(&arr, op->alloc);
    Fs_Entry_Array batch;
    mel_array_init(&batch, op->alloc);
    u32 bcap = op->batch == 0 ? 64 : op->batch;

    do
    {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        win_push_entry(op, &arr, &fd);
        if (op->on_batch)
        {
            mel_array_push(&batch, arr.items[arr.count - 1]);
            if (batch.count >= bcap)
            {
                op->on_batch(batch.items, (u32)batch.count, op->stream_user);
                mel_array_clear(&batch);
            }
        }
    } while (FindNextFileW(h, &fd));
    if (op->on_batch && batch.count > 0)
        op->on_batch(batch.items, (u32)batch.count, op->stream_user);
    mel_array_free(&batch);
    FindClose(h);

    op->result.dir.entries = arr.items;
    op->result.dir.count = (u32)arr.count;
    op->result.dir.status = MEL_FS_OK;
}

static DWORD win_glob_walk(Mel_Fs_Op_Record* op, str8 dir, str8 rel, Fs_Entry_Array* arr)
{
    str8   pattern = str8_fmt_alloc(op->alloc, "%.*s\\*", (int)dir.len, dir.data);
    WCHAR* wp = utf8_to_wide(pattern, op->alloc);
    if (pattern.data)
        mel_dealloc(op->alloc, pattern.data);

    WIN32_FIND_DATAW fd;
    HANDLE           h = FindFirstFileW(wp, &fd);
    mel_dealloc(op->alloc, wp);
    if (h == INVALID_HANDLE_VALUE)
    {
        DWORD e = GetLastError();
        return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? ERROR_SUCCESS : e;
    }

    DWORD e = ERROR_SUCCESS;
    do
    {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        str8 name = wide_to_utf8(fd.cFileName, op->alloc);
        bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (mel_fs_glob_match(op->glob_pattern, name, op->case_insensitive))
        {
            Mel_Fs_Dir_Entry me = { 0 };
            if (rel.len > 0)
                me.name = str8_fmt_alloc(op->alloc, "%.*s/%.*s", (int)rel.len, rel.data, (int)name.len, name.data);
            else
                me.name = str8_dup_alloc(name, op->alloc);
            me.kind = win_kind_from_attrs(fd.dwFileAttributes);
            me.size_bytes = ((u64)fd.nFileSizeHigh << 32) | (u64)fd.nFileSizeLow;
            me.mtime_ns = win_filetime_ns(fd.ftLastWriteTime);
            mel_array_push(arr, me);
        }

        if (op->recursive && is_dir)
        {
            str8 child = str8_fmt_alloc(op->alloc, "%.*s\\%.*s", (int)dir.len, dir.data, (int)name.len, name.data);
            str8 subrel = rel.len > 0 ? str8_fmt_alloc(op->alloc, "%.*s/%.*s", (int)rel.len, rel.data, (int)name.len, name.data) : str8_dup_alloc(name, op->alloc);
            e = win_glob_walk(op, child, subrel, arr);
            if (child.data)
                mel_dealloc(op->alloc, child.data);
            if (subrel.data)
                mel_dealloc(op->alloc, subrel.data);
            if (name.data)
                mel_dealloc(op->alloc, name.data);
            if (e != ERROR_SUCCESS)
                break;
            continue;
        }
        if (name.data)
            mel_dealloc(op->alloc, name.data);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return e;
}

void mel_fs__do_glob(Mel_Fs_Op_Record* op)
{
    Fs_Entry_Array arr;
    mel_array_init(&arr, op->alloc);
    DWORD e = win_glob_walk(op, op->path_a, STR8_EMPTY, &arr);
    if (e != ERROR_SUCCESS)
    {
        for (usize i = 0; i < arr.count; i++)
            if (arr.items[i].name.data)
                mel_dealloc(op->alloc, arr.items[i].name.data);
        mel_array_free(&arr);
        op->result.dir.status = win_status_from_error(e);
        op->result.dir.os_error = (i32)e;
        return;
    }
    op->result.dir.entries = arr.items;
    op->result.dir.count = (u32)arr.count;
    op->result.dir.status = MEL_FS_OK;
}

Mel_Fs_Path_Result mel_fs__backend_cwd(const Mel_Alloc* alloc)
{
    Mel_Fs_Path_Result r = { 0 };
    DWORD              need = GetCurrentDirectoryW(0, NULL);
    if (need == 0)
    {
        DWORD e = GetLastError();
        r.status = win_status_from_error(e);
        r.os_error = (i32)e;
        return r;
    }
    WCHAR* w = mel_alloc(alloc, sizeof(WCHAR) * need);
    if (!w)
    {
        r.status = MEL_FS_ERROR;
        return r;
    }
    GetCurrentDirectoryW(need, w);
    r.value = wide_to_utf8(w, alloc);
    r.status = MEL_FS_OK;
    mel_dealloc(alloc, w);
    return r;
}

Mel_Fs_Void_Result mel_fs__backend_chdir(str8 path)
{
    Mel_Fs_Void_Result r = { 0 };
    WCHAR*             w = utf8_to_wide(path, mel_alloc_heap());
    if (!w)
    {
        r.status = MEL_FS_ERROR;
        return r;
    }
    DWORD e = SetCurrentDirectoryW(w) ? ERROR_SUCCESS : GetLastError();
    mel_dealloc(mel_alloc_heap(), w);
    r.status = win_status_from_error(e);
    r.os_error = (i32)e;
    return r;
}

typedef struct
{
    str8        org;
    str8        app;
    const char* bundle_id;
    bool        set;
} Fs_Pref_Identity;

static Fs_Pref_Identity g_pref;

void mel_fs_pref_identity_opt(Mel_Fs_Pref_Opt opt)
{
    g_pref.org = opt.org;
    g_pref.app = opt.app;
    g_pref.bundle_id = opt.bundle_id;
    g_pref.set = true;
}

static Mel_Fs_Path_Result known_folder(REFKNOWNFOLDERID id, const Mel_Alloc* alloc)
{
    PWSTR              w = NULL;
    Mel_Fs_Path_Result r = { 0 };
    HRESULT            hr = SHGetKnownFolderPath(id, 0, NULL, &w);
    if (FAILED(hr) || !w)
    {
        if (w)
            CoTaskMemFree(w);
        r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
        return r;
    }
    r.value = wide_to_utf8(w, alloc);
    r.status = MEL_FS_OK;
    CoTaskMemFree(w);
    return r;
}

Mel_Fs_Path_Result mel_fs__backend_folder(Mel_Fs_Folder folder, const Mel_Alloc* alloc)
{
    switch (folder)
    {
    case MEL_FS_FOLDER_BASE:
    {
        WCHAR w[MAX_PATH * 4];
        DWORD n = GetModuleFileNameW(NULL, w, sizeof w / sizeof w[0]);
        if (n == 0)
            return mel_fs__backend_cwd(alloc);
        str8               full = wide_to_utf8(w, alloc);
        str8               parent = mel_path_parent(full);
        Mel_Fs_Path_Result r = { 0 };
        r.value = str8_dup_alloc(parent, alloc);
        r.status = MEL_FS_OK;
        if (full.data)
            mel_dealloc(alloc, full.data);
        return r;
    }
    case MEL_FS_FOLDER_PREF:
    {
        Mel_Fs_Path_Result base = known_folder(&FOLDERID_RoamingAppData, alloc);
        if (mel_fs_failed(base.status))
            return base;
        str8 leaf = STR8_EMPTY;
        if (g_pref.set && g_pref.app.len > 0)
            leaf = g_pref.app;
        else if (g_pref.set && g_pref.bundle_id)
            leaf = str8_from_cstr(g_pref.bundle_id);
        if (leaf.len == 0)
            return base;
        u8   buf[2048];
        str8 joined;
        if (g_pref.set && g_pref.org.len > 0)
        {
            u8   obuf[2048];
            str8 with_org = mel_path_join(base.value, g_pref.org, obuf, sizeof obuf);
            joined = mel_path_join(with_org, leaf, buf, sizeof buf);
        }
        else
        {
            joined = mel_path_join(base.value, leaf, buf, sizeof buf);
        }
        Mel_Fs_Path_Result r = { 0 };
        r.value = str8_dup_alloc(joined, alloc);
        r.status = MEL_FS_OK;
        if (base.value.data)
            mel_dealloc(alloc, base.value.data);
        return r;
    }
    case MEL_FS_FOLDER_HOME:
        return known_folder(&FOLDERID_Profile, alloc);
    case MEL_FS_FOLDER_DESKTOP:
        return known_folder(&FOLDERID_Desktop, alloc);
    case MEL_FS_FOLDER_DOCUMENTS:
        return known_folder(&FOLDERID_Documents, alloc);
    case MEL_FS_FOLDER_DOWNLOADS:
        return known_folder(&FOLDERID_Downloads, alloc);
    case MEL_FS_FOLDER_MUSIC:
        return known_folder(&FOLDERID_Music, alloc);
    case MEL_FS_FOLDER_PICTURES:
        return known_folder(&FOLDERID_Pictures, alloc);
    case MEL_FS_FOLDER_VIDEOS:
        return known_folder(&FOLDERID_Videos, alloc);
    case MEL_FS_FOLDER_TEMPLATES:
        return known_folder(&FOLDERID_Templates, alloc);
    case MEL_FS_FOLDER_SAVED_GAMES:
        return known_folder(&FOLDERID_SavedGames, alloc);
    case MEL_FS_FOLDER_SCREENSHOTS:
        return known_folder(&FOLDERID_Screenshots, alloc);
    case MEL_FS_FOLDER_CACHE:
        return known_folder(&FOLDERID_LocalAppData, alloc);
    case MEL_FS_FOLDER_TEMP:
    {
        WCHAR              w[MAX_PATH + 1];
        DWORD              n = GetTempPathW(MAX_PATH + 1, w);
        Mel_Fs_Path_Result r = { 0 };
        if (n == 0)
        {
            r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
            return r;
        }
        r.value = wide_to_utf8(w, alloc);
        r.status = MEL_FS_OK;
        return r;
    }
    default:
    {
        Mel_Fs_Path_Result r = { 0 };
        r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
        return r;
    }
    }
}
