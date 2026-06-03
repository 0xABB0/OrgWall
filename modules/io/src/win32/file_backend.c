#include <io/backend.h>

#include <log/log.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

bool mel_io__backend_available(void) { return true; }

static Mel_IO_Status status_from_errno(int e)
{
    switch (e)
    {
    case ENOENT:
        return MEL_IO_ERROR | MEL_IO_NOT_FOUND;
    case EACCES:
        return MEL_IO_ERROR | MEL_IO_PERMISSION;
    case EEXIST:
        return MEL_IO_ERROR | MEL_IO_EXISTS;
    case EBADF:
        return MEL_IO_ERROR | MEL_IO_BAD_HANDLE;
    case ENOSPC:
        return MEL_IO_ERROR | MEL_IO_NO_SPACE;
    default:
        return MEL_IO_ERROR;
    }
}

Mel_IO_File_Native mel_io__backend_open(const char* path, u32 flags, u32 mode)
{
    Mel_IO_File_Native out = { .fd = -1, .handle = NULL };

    int  oflags = _O_BINARY;
    bool rd = (flags & MEL_IO_FILE_READ) != 0;
    bool wr = (flags & MEL_IO_FILE_WRITE) != 0;
    if (rd && wr)
        oflags |= _O_RDWR;
    else if (wr)
        oflags |= _O_WRONLY;
    else
        oflags |= _O_RDONLY;
    if (flags & MEL_IO_FILE_CREATE)
        oflags |= _O_CREAT;
    if (flags & MEL_IO_FILE_TRUNCATE)
        oflags |= _O_TRUNC;
    if (flags & MEL_IO_FILE_APPEND)
        oflags |= _O_APPEND;
    if (flags & MEL_IO_FILE_EXCL)
        oflags |= _O_EXCL;

    int pmode = (mode & 0200) ? (_S_IREAD | _S_IWRITE) : _S_IREAD;
    int fd = _open(path, oflags, pmode);
    if (fd < 0)
    {
        out.status = status_from_errno(errno);
        mel_log_warn("io", "_open('%s') failed: %s", path, strerror(errno));
        return out;
    }

    HANDLE h = (HANDLE)_get_osfhandle(fd);
    bool   seekable = false;
    i64    size = 0;
    if (h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_DISK)
    {
        seekable = true;
        LARGE_INTEGER li;
        if (GetFileSizeEx(h, &li))
            size = (i64)li.QuadPart;
    }

    out.fd = fd;
    out.handle = (h == INVALID_HANDLE_VALUE) ? NULL : (void*)h;
    out.initial_size = size;
    out.seekable = seekable;
    out.status = MEL_IO_OK;
    return out;
}

void mel_io__backend_close(Mel_IO_File_Native native)
{
    if (native.fd >= 0)
        _close(native.fd);
}

Mel_IO_Status mel_io__backend_pio(Mel_IO_File_Native native, bool is_read, void* buffer, usize len, i64 offset, Mel_IO_Result* out)
{
    out->bytes_transferred = 0;
    out->os_error = 0;
    out->status = MEL_IO_OK;

    if (native.fd < 0)
    {
        out->status = MEL_IO_ERROR | MEL_IO_BAD_HANDLE;
        return out->status;
    }

    if (native.seekable)
    {
        if (_lseeki64(native.fd, (__int64)offset, SEEK_SET) < 0)
        {
            out->os_error = errno;
            out->status = status_from_errno(errno);
            return out->status;
        }
    }

    unsigned want = len > 0x7fffffffu ? 0x7fffffffu : (unsigned)len;
    int      n = is_read ? _read(native.fd, buffer, want) : _write(native.fd, buffer, want);
    if (n < 0)
    {
        out->os_error = errno;
        out->status = status_from_errno(errno);
        return out->status;
    }

    out->bytes_transferred = (usize)n;
    if (is_read && n == 0 && len > 0)
        out->status = MEL_IO_OK | MEL_IO_EOF;
    else if ((usize)n < len)
        out->status = MEL_IO_OK | MEL_IO_PARTIAL;
    return out->status;
}

Mel_IO_Status mel_io__backend_seek(Mel_IO_File_Native native, i64 offset, i32 whence, i64* out_pos)
{
    if (native.fd < 0)
        return MEL_IO_ERROR | MEL_IO_BAD_HANDLE;
    int w = whence == MEL_IO_SEEK_SET ? SEEK_SET : whence == MEL_IO_SEEK_CUR ? SEEK_CUR : whence == MEL_IO_SEEK_END ? SEEK_END : -1;
    if (w < 0)
        return MEL_IO_ERROR | MEL_IO_NOT_SEEKABLE;
    __int64 p = _lseeki64(native.fd, (__int64)offset, w);
    if (p < 0)
        return status_from_errno(errno);
    *out_pos = (i64)p;
    return MEL_IO_OK;
}

bool mel_io__backend_size(Mel_IO_File_Native native, i64* out_size)
{
    if (native.fd < 0)
        return false;
    HANDLE h = (HANDLE)_get_osfhandle(native.fd);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li))
        return false;
    *out_size = (i64)li.QuadPart;
    return true;
}

Mel_IO_Status mel_io__backend_flush(Mel_IO_File_Native native)
{
    if (native.fd < 0)
        return MEL_IO_ERROR | MEL_IO_BAD_HANDLE;
    HANDLE h = (HANDLE)_get_osfhandle(native.fd);
    if (h == INVALID_HANDLE_VALUE)
        return MEL_IO_ERROR | MEL_IO_BAD_HANDLE;
    if (!FlushFileBuffers(h))
    {
        DWORD e = GetLastError();
        if (e == ERROR_INVALID_HANDLE || e == ERROR_INVALID_FUNCTION)
            return MEL_IO_OK;
        return MEL_IO_ERROR;
    }
    return MEL_IO_OK;
}
