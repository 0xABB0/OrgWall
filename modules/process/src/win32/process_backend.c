#include "../process_backend.h"

#include <allocator/allocator.h>
#include <log/log.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

bool mel_process__backend_available(void) { return true; }

static Mel_Process_Status status_from_win32(DWORD e)
{
    switch (e)
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return MEL_PROCESS_ERROR | MEL_PROCESS_SPAWN_FAILED | MEL_PROCESS_NOT_FOUND;
    case ERROR_ACCESS_DENIED:
        return MEL_PROCESS_ERROR | MEL_PROCESS_SPAWN_FAILED | MEL_PROCESS_PERMISSION;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return MEL_PROCESS_ERROR | MEL_PROCESS_NO_MEMORY;
    default:
        return MEL_PROCESS_ERROR | MEL_PROCESS_SPAWN_FAILED;
    }
}

static wchar_t* utf8_to_wide(const char* s, const Mel_Alloc* alloc)
{
    if (!s)
        return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0)
        return NULL;
    wchar_t* w = (wchar_t*)mel_alloc(alloc, (usize)n * sizeof(wchar_t));
    if (!w)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static void quote_arg(const char* arg, wchar_t* outbuf, usize* outlen, usize cap)
{
    (void)cap;
    int wn = MultiByteToWideChar(CP_UTF8, 0, arg, -1, NULL, 0);
    wchar_t stackw[1024];
    wchar_t* wide = stackw;
    if (wn > (int)(sizeof stackw / sizeof stackw[0]))
        wide = stackw;
    MultiByteToWideChar(CP_UTF8, 0, arg, -1, wide, wn > (int)(sizeof stackw / sizeof stackw[0]) ? (int)(sizeof stackw / sizeof stackw[0]) : wn);

    usize len = *outlen;
    bool  need_quote = wide[0] == L'\0' || wcspbrk(wide, L" \t\n\v\"") != NULL;
    if (need_quote)
        outbuf[len++] = L'"';
    for (const wchar_t* p = wide; *p; p++)
    {
        usize backslashes = 0;
        while (*p == L'\\')
        {
            backslashes++;
            p++;
        }
        if (*p == L'\0')
        {
            for (usize i = 0; i < backslashes * 2; i++)
                outbuf[len++] = L'\\';
            break;
        }
        else if (*p == L'"')
        {
            for (usize i = 0; i < backslashes * 2 + 1; i++)
                outbuf[len++] = L'\\';
            outbuf[len++] = *p;
        }
        else
        {
            for (usize i = 0; i < backslashes; i++)
                outbuf[len++] = L'\\';
            outbuf[len++] = *p;
        }
    }
    if (need_quote)
        outbuf[len++] = L'"';
    *outlen = len;
}

static wchar_t* build_cmdline(const char* const* argv, usize argc, const Mel_Alloc* alloc)
{
    usize cap = 1;
    for (usize i = 0; i < argc; i++)
        cap += (strlen(argv[i]) + 1) * 2 + 3;
    wchar_t* buf = (wchar_t*)mel_alloc(alloc, cap * sizeof(wchar_t));
    if (!buf)
        return NULL;
    usize len = 0;
    for (usize i = 0; i < argc; i++)
    {
        if (i > 0)
            buf[len++] = L' ';
        quote_arg(argv[i], buf, &len, cap);
    }
    buf[len] = L'\0';
    return buf;
}

static wchar_t* build_env_block(Mel_Process_Spawn_Args args, const Mel_Alloc* alloc)
{
    if ((!args.env || args.env_count == 0) && !args.env_clear)
        return NULL;

    usize cap = 1;
    for (usize i = 0; i < args.env_count; i++)
        cap += (strlen(args.env[i].key) + 1 + (args.env[i].value ? strlen(args.env[i].value) : 0) + 1) * 2;
    cap += 1;
    wchar_t* block = (wchar_t*)mel_alloc(alloc, cap * sizeof(wchar_t));
    if (!block)
        return NULL;
    usize len = 0;
    for (usize i = 0; i < args.env_count; i++)
    {
        const char* key = args.env[i].key;
        const char* val = args.env[i].value ? args.env[i].value : "";
        usize       entry_chars = strlen(key) + 1 + strlen(val) + 1;
        char*       tmp = (char*)mel_alloc(alloc, entry_chars);
        if (!tmp)
        {
            mel_dealloc(alloc, block);
            return NULL;
        }
        snprintf(tmp, entry_chars, "%s=%s", key, val);
        int wn = MultiByteToWideChar(CP_UTF8, 0, tmp, -1, block + len, (int)(cap - len));
        mel_dealloc(alloc, tmp);
        if (wn <= 0)
        {
            mel_dealloc(alloc, block);
            return NULL;
        }
        len += (usize)wn;
    }
    block[len] = L'\0';
    return block;
}

static volatile LONG g_pipe_serial = 0;

static bool make_async_pipe(HANDLE* read_h, HANDLE* write_h, bool read_is_parent)
{
    wchar_t name[MAX_PATH];
    LONG    serial = InterlockedIncrement(&g_pipe_serial);
    swprintf(name, MAX_PATH, L"\\\\.\\Pipe\\MelProcess.%08lx.%08lx", (unsigned long)GetCurrentProcessId(), (unsigned long)serial);

    SECURITY_ATTRIBUTES sa = { .nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = TRUE, .lpSecurityDescriptor = NULL };

    HANDLE server = CreateNamedPipeW(name, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, &sa);
    DWORD  open_mode = read_is_parent ? GENERIC_WRITE : GENERIC_READ;
    if (read_is_parent)
    {
        CloseHandle(server);
        server = CreateNamedPipeW(name, PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, &sa);
        open_mode = GENERIC_READ;
    }
    if (server == INVALID_HANDLE_VALUE)
        return false;

    HANDLE client = CreateFileW(name, open_mode, 0, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (client == INVALID_HANDLE_VALUE)
    {
        CloseHandle(server);
        return false;
    }

    if (read_is_parent)
    {
        *read_h = server;
        *write_h = client;
    }
    else
    {
        *write_h = server;
        *read_h = client;
    }
    return true;
}

static int handle_to_fd(HANDLE h, int crt_flags)
{
    int fd = _open_osfhandle((intptr_t)h, crt_flags);
    return fd;
}

Mel_Process_Native mel_process__backend_spawn(Mel_Process_Spawn_Args args)
{
    Mel_Process_Native out = { .pid = -1 };
    out.child_stdin.fd = -1;
    out.child_stdout.fd = -1;
    out.child_stderr.fd = -1;

    HANDLE in_read = NULL, in_write = NULL;
    HANDLE out_read = NULL, out_write = NULL;
    HANDLE err_read = NULL, err_write = NULL;
    HANDLE null_h = INVALID_HANDLE_VALUE;

    SECURITY_ATTRIBUTES sa = { .nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = TRUE, .lpSecurityDescriptor = NULL };

    if (args.stdin_disposition == MEL_PROCESS_STDIO_PIPE && !make_async_pipe(&in_read, &in_write, false))
        goto pipe_fail;
    if (args.stdout_disposition == MEL_PROCESS_STDIO_PIPE && !make_async_pipe(&out_read, &out_write, true))
        goto pipe_fail;
    if (args.stderr_disposition == MEL_PROCESS_STDIO_PIPE && !args.merge_stderr && !make_async_pipe(&err_read, &err_write, true))
        goto pipe_fail;

    bool need_null = args.stdin_disposition == MEL_PROCESS_STDIO_NULL || args.stdout_disposition == MEL_PROCESS_STDIO_NULL || args.stderr_disposition == MEL_PROCESS_STDIO_NULL;
    if (need_null)
    {
        null_h = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
        if (null_h == INVALID_HANDLE_VALUE)
        {
            out.status = status_from_win32(GetLastError());
            out.os_error = (i32)GetLastError();
            goto cleanup;
        }
    }

    HANDLE child_in = INVALID_HANDLE_VALUE;
    HANDLE child_out = INVALID_HANDLE_VALUE;
    HANDLE child_err = INVALID_HANDLE_VALUE;

    switch (args.stdin_disposition)
    {
    case MEL_PROCESS_STDIO_PIPE: child_in = in_read; break;
    case MEL_PROCESS_STDIO_NULL: child_in = null_h; break;
    case MEL_PROCESS_STDIO_REDIRECT: child_in = (HANDLE)_get_osfhandle(args.stdin_redirect_fd); break;
    default: child_in = args.detached ? null_h : GetStdHandle(STD_INPUT_HANDLE); break;
    }
    switch (args.stdout_disposition)
    {
    case MEL_PROCESS_STDIO_PIPE: child_out = out_write; break;
    case MEL_PROCESS_STDIO_NULL: child_out = null_h; break;
    case MEL_PROCESS_STDIO_REDIRECT: child_out = (HANDLE)_get_osfhandle(args.stdout_redirect_fd); break;
    default: child_out = args.detached ? null_h : GetStdHandle(STD_OUTPUT_HANDLE); break;
    }
    if (args.merge_stderr)
        child_err = child_out;
    else
        switch (args.stderr_disposition)
        {
        case MEL_PROCESS_STDIO_PIPE: child_err = err_write; break;
        case MEL_PROCESS_STDIO_NULL: child_err = null_h; break;
        case MEL_PROCESS_STDIO_REDIRECT: child_err = (HANDLE)_get_osfhandle(args.stderr_redirect_fd); break;
        default: child_err = args.detached ? null_h : GetStdHandle(STD_ERROR_HANDLE); break;
        }

    if (child_in != INVALID_HANDLE_VALUE)
        SetHandleInformation(child_in, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    if (child_out != INVALID_HANDLE_VALUE)
        SetHandleInformation(child_out, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    if (child_err != INVALID_HANDLE_VALUE)
        SetHandleInformation(child_err, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    if (in_write)
        SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
    if (out_read)
        SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    if (err_read)
        SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

    wchar_t* cmdline = build_cmdline(args.argv, args.argc, args.alloc);
    if (!cmdline)
    {
        out.status = MEL_PROCESS_ERROR | MEL_PROCESS_NO_MEMORY;
        goto cleanup;
    }
    wchar_t* env_block = build_env_block(args, args.alloc);
    wchar_t* cwd_w = utf8_to_wide(args.cwd, args.alloc);

    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (job)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jli = { 0 };
        jli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jli, sizeof jli);
    }

    STARTUPINFOW si = { .cb = sizeof(STARTUPINFOW) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = child_in;
    si.hStdOutput = child_out;
    si.hStdError = child_err;

    PROCESS_INFORMATION pi = { 0 };
    DWORD               flags = CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED;
    if (args.detached)
        flags |= DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;

    BOOL ok = CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, flags, env_block, cwd_w, &si, &pi);
    DWORD spawn_err = GetLastError();

    mel_dealloc(args.alloc, cmdline);
    if (env_block)
        mel_dealloc(args.alloc, env_block);
    if (cwd_w)
        mel_dealloc(args.alloc, cwd_w);

    if (!ok)
    {
        if (job)
            CloseHandle(job);
        out.status = status_from_win32(spawn_err);
        out.os_error = (i32)spawn_err;
        goto cleanup;
    }

    if (job)
    {
        AssignProcessToJobObject(job, pi.hProcess);
        out.job = job;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    out.pid = (i64)pi.dwProcessId;
    out.handle = pi.hProcess;
    out.status = MEL_PROCESS_OK;

    if (args.stdin_disposition == MEL_PROCESS_STDIO_PIPE)
    {
        CloseHandle(in_read);
        in_read = NULL;
        out.child_stdin.fd = handle_to_fd(in_write, _O_WRONLY | _O_BINARY);
        out.child_stdin.handle = in_write;
        in_write = NULL;
    }
    if (args.stdout_disposition == MEL_PROCESS_STDIO_PIPE)
    {
        CloseHandle(out_write);
        out_write = NULL;
        out.child_stdout.fd = handle_to_fd(out_read, _O_RDONLY | _O_BINARY);
        out.child_stdout.handle = out_read;
        out_read = NULL;
    }
    if (args.stderr_disposition == MEL_PROCESS_STDIO_PIPE && !args.merge_stderr)
    {
        CloseHandle(err_write);
        err_write = NULL;
        out.child_stderr.fd = handle_to_fd(err_read, _O_RDONLY | _O_BINARY);
        out.child_stderr.handle = err_read;
        err_read = NULL;
    }

cleanup:
    if (in_read)
        CloseHandle(in_read);
    if (in_write)
        CloseHandle(in_write);
    if (out_read)
        CloseHandle(out_read);
    if (out_write)
        CloseHandle(out_write);
    if (err_read)
        CloseHandle(err_read);
    if (err_write)
        CloseHandle(err_write);
    if (null_h != INVALID_HANDLE_VALUE)
        CloseHandle(null_h);
    return out;

pipe_fail:
    out.status = MEL_PROCESS_ERROR | MEL_PROCESS_PIPE_FAILED;
    out.os_error = (i32)GetLastError();
    goto cleanup;
}

static bool reap_handle(Mel_Process_Native* native, DWORD wait_ms, i32* out_exit, i32* out_signal, Mel_Process_Status* out_status)
{
    if (!native->handle)
    {
        *out_exit = -1;
        *out_signal = 0;
        *out_status = MEL_PROCESS_ERROR | MEL_PROCESS_BAD_HANDLE;
        return true;
    }
    DWORD w = WaitForSingleObject((HANDLE)native->handle, wait_ms);
    if (w == WAIT_TIMEOUT)
        return false;
    if (w != WAIT_OBJECT_0)
    {
        *out_exit = -1;
        *out_signal = 0;
        *out_status = MEL_PROCESS_ERROR | MEL_PROCESS_BAD_HANDLE;
        return true;
    }
    DWORD code = 0;
    GetExitCodeProcess((HANDLE)native->handle, &code);
    *out_exit = (i32)code;
    *out_signal = 0;
    *out_status = MEL_PROCESS_OK | MEL_PROCESS_EXITED;
    return true;
}

bool mel_process__backend_reap(Mel_Process_Native* native, i32* out_exit, i32* out_signal, Mel_Process_Status* out_status)
{
    return reap_handle(native, 0, out_exit, out_signal, out_status);
}

void mel_process__backend_wait_blocking(Mel_Process_Native* native, i32* out_exit, i32* out_signal, Mel_Process_Status* out_status)
{
    reap_handle(native, INFINITE, out_exit, out_signal, out_status);
}

Mel_Process_Status mel_process__backend_signal(Mel_Process_Native* native, u32 signal)
{
    if (!native->handle)
        return MEL_PROCESS_ERROR | MEL_PROCESS_BAD_HANDLE;
    if (native->job)
    {
        if (!TerminateJobObject((HANDLE)native->job, 1))
            return MEL_PROCESS_ERROR | MEL_PROCESS_BAD_HANDLE;
        return MEL_PROCESS_OK | (signal == MEL_PROCESS_SIGNAL_KILL ? MEL_PROCESS_KILLED : 0u);
    }
    if (!TerminateProcess((HANDLE)native->handle, 1))
    {
        DWORD e = GetLastError();
        if (e == ERROR_ACCESS_DENIED)
            return MEL_PROCESS_OK | MEL_PROCESS_EXITED;
        return MEL_PROCESS_ERROR | MEL_PROCESS_BAD_HANDLE;
    }
    return MEL_PROCESS_OK | (signal == MEL_PROCESS_SIGNAL_KILL ? MEL_PROCESS_KILLED : 0u);
}

void mel_process__backend_close(Mel_Process_Native* native)
{
    if (native->child_stdin.fd >= 0)
        _close(native->child_stdin.fd);
    else if (native->child_stdin.handle)
        CloseHandle((HANDLE)native->child_stdin.handle);
    if (native->child_stdout.fd >= 0)
        _close(native->child_stdout.fd);
    else if (native->child_stdout.handle)
        CloseHandle((HANDLE)native->child_stdout.handle);
    if (native->child_stderr.fd >= 0)
        _close(native->child_stderr.fd);
    else if (native->child_stderr.handle)
        CloseHandle((HANDLE)native->child_stderr.handle);
    if (native->handle)
        CloseHandle((HANDLE)native->handle);
    if (native->job)
        CloseHandle((HANDLE)native->job);
    native->handle = NULL;
    native->job = NULL;
}
