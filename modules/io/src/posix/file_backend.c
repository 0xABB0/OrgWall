#include <io/backend.h>

#include <log/log.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

bool mel_io__backend_available(void) { return true; }

static Mel_IO_Status status_from_errno(int e)
{
    switch (e)
    {
    case ENOENT:
        return MEL_IO_ERROR | MEL_IO_NOT_FOUND;
    case EACCES:
    case EPERM:
        return MEL_IO_ERROR | MEL_IO_PERMISSION;
    case EEXIST:
        return MEL_IO_ERROR | MEL_IO_EXISTS;
    case EBADF:
        return MEL_IO_ERROR | MEL_IO_BAD_HANDLE;
    case ENOSPC:
        return MEL_IO_ERROR | MEL_IO_NO_SPACE;
    case ESPIPE:
        return MEL_IO_ERROR | MEL_IO_NOT_SEEKABLE;
    default:
        return MEL_IO_ERROR;
    }
}

Mel_IO_File_Native mel_io__backend_open(const char* path, u32 flags, u32 mode)
{
    Mel_IO_File_Native out = { .fd = -1, .handle = NULL };

    int  oflags = 0;
    bool rd = (flags & MEL_IO_FILE_READ) != 0;
    bool wr = (flags & MEL_IO_FILE_WRITE) != 0;
    if (rd && wr)
        oflags |= O_RDWR;
    else if (wr)
        oflags |= O_WRONLY;
    else
        oflags |= O_RDONLY;
    if (flags & MEL_IO_FILE_CREATE)
        oflags |= O_CREAT;
    if (flags & MEL_IO_FILE_TRUNCATE)
        oflags |= O_TRUNC;
    if (flags & MEL_IO_FILE_APPEND)
        oflags |= O_APPEND;
    if (flags & MEL_IO_FILE_EXCL)
        oflags |= O_EXCL;

    int fd = open(path, oflags, (mode_t)mode);
    if (fd < 0)
    {
        out.status = status_from_errno(errno);
        mel_log_warn("io", "open('%s') failed: %s", path, strerror(errno));
        return out;
    }

    struct stat st;
    bool        seekable = false;
    i64         size = 0;
    if (fstat(fd, &st) == 0)
    {
        seekable = S_ISREG(st.st_mode);
        if (seekable)
            size = (i64)st.st_size;
    }

    out.fd = fd;
    out.handle = NULL;
    out.initial_size = size;
    out.seekable = seekable;
    out.async_capable = true;
    out.status = MEL_IO_OK;
    return out;
}

void mel_io__backend_close(Mel_IO_File_Native native)
{
    if (native.fd >= 0)
        close(native.fd);
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

    ssize_t n;
    if (is_read)
    {
        n = native.seekable ? pread(native.fd, buffer, len, (off_t)offset) : read(native.fd, buffer, len);
    }
    else
    {
        n = native.seekable ? pwrite(native.fd, buffer, len, (off_t)offset) : write(native.fd, buffer, len);
    }

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
    off_t p = lseek(native.fd, (off_t)offset, w);
    if (p < 0)
        return status_from_errno(errno);
    *out_pos = (i64)p;
    return MEL_IO_OK;
}

bool mel_io__backend_size(Mel_IO_File_Native native, i64* out_size)
{
    if (native.fd < 0)
        return false;
    struct stat st;
    if (fstat(native.fd, &st) != 0)
        return false;
    *out_size = (i64)st.st_size;
    return true;
}

Mel_IO_Status mel_io__backend_flush(Mel_IO_File_Native native)
{
    if (native.fd < 0)
        return MEL_IO_ERROR | MEL_IO_BAD_HANDLE;
    if (fsync(native.fd) != 0)
    {
        if (errno == EINVAL || errno == ENOTSUP)
            return MEL_IO_OK;
        return status_from_errno(errno);
    }
    return MEL_IO_OK;
}
