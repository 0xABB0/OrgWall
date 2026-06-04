#define _GNU_SOURCE
#include "../process_backend.h"

#include <allocator/allocator.h>
#include <log/log.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

extern char** environ;

bool mel_process__backend_available(void) { return true; }

static Mel_Process_Status status_from_errno(int e)
{
    switch (e)
    {
    case ENOENT:
        return MEL_PROCESS_ERROR | MEL_PROCESS_SPAWN_FAILED | MEL_PROCESS_NOT_FOUND;
    case EACCES:
    case EPERM:
        return MEL_PROCESS_ERROR | MEL_PROCESS_SPAWN_FAILED | MEL_PROCESS_PERMISSION;
    case ENOMEM:
        return MEL_PROCESS_ERROR | MEL_PROCESS_NO_MEMORY;
    default:
        return MEL_PROCESS_ERROR | MEL_PROCESS_SPAWN_FAILED;
    }
}

static bool make_pipe(int fds[2])
{
#if defined(__linux__) || defined(__ANDROID__)
    if (pipe2(fds, O_CLOEXEC) == 0)
        return true;
    return false;
#else
    if (pipe(fds) != 0)
        return false;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
#endif
}

static void close_fd(int* fd)
{
    if (*fd >= 0)
    {
        close(*fd);
        *fd = -1;
    }
}

static char** build_envp(Mel_Process_Spawn_Args args, char*** out_storage)
{
    *out_storage = NULL;
    if (!args.env || args.env_count == 0)
    {
        if (args.env_clear)
        {
            char** e = mel_calloc(args.alloc, sizeof(char*));
            if (!e)
                return NULL;
            e[0] = NULL;
            *out_storage = e;
            return e;
        }
        return environ;
    }

    usize base = 0;
    if (!args.env_clear)
        for (char** e = environ; e && *e; e++)
            base++;

    usize total = base + args.env_count;
    char** envp = mel_calloc(args.alloc, sizeof(char*) * (total + 1));
    if (!envp)
        return NULL;

    usize idx = 0;
    if (!args.env_clear)
        for (char** e = environ; e && *e; e++)
            envp[idx++] = *e;

    for (usize i = 0; i < args.env_count; i++)
    {
        const char* key = args.env[i].key;
        const char* val = args.env[i].value ? args.env[i].value : "";
        usize       klen = strlen(key);
        usize       vlen = strlen(val);
        char*       entry = mel_alloc(args.alloc, klen + 1 + vlen + 1);
        if (!entry)
        {
            for (usize j = base; j < idx; j++)
                mel_dealloc(args.alloc, envp[j]);
            mel_dealloc(args.alloc, envp);
            return NULL;
        }
        memcpy(entry, key, klen);
        entry[klen] = '=';
        memcpy(entry + klen + 1, val, vlen);
        entry[klen + 1 + vlen] = '\0';
        envp[idx++] = entry;
    }
    envp[idx] = NULL;
    *out_storage = envp;
    return envp;
}

static void free_envp(Mel_Process_Spawn_Args args, char** storage)
{
    if (!storage || storage == environ)
        return;
    usize base = 0;
    if (args.env && args.env_count > 0 && !args.env_clear)
        for (char** e = environ; e && *e; e++)
            base++;
    for (char** e = storage + base; *e; e++)
        mel_dealloc(args.alloc, *e);
    mel_dealloc(args.alloc, storage);
}

static int dispose_child_fd(u32 disposition, int redirect_fd, int pipe_child_fd, int null_fd, int std_fd)
{
    switch (disposition)
    {
    case MEL_PROCESS_STDIO_NULL:
        return null_fd;
    case MEL_PROCESS_STDIO_PIPE:
        return pipe_child_fd;
    case MEL_PROCESS_STDIO_REDIRECT:
        return redirect_fd;
    default:
        return std_fd;
    }
}

Mel_Process_Native mel_process__backend_spawn(Mel_Process_Spawn_Args args)
{
    Mel_Process_Native out = { .pid = -1 };
    out.child_stdin.fd = -1;
    out.child_stdout.fd = -1;
    out.child_stderr.fd = -1;

    int in_pipe[2] = { -1, -1 };
    int out_pipe[2] = { -1, -1 };
    int err_pipe[2] = { -1, -1 };
    int null_fd = -1;

    bool need_null = args.stdin_disposition == MEL_PROCESS_STDIO_NULL || args.stdout_disposition == MEL_PROCESS_STDIO_NULL || args.stderr_disposition == MEL_PROCESS_STDIO_NULL || (args.detached && (args.stdin_disposition == MEL_PROCESS_STDIO_INHERIT || args.stdout_disposition == MEL_PROCESS_STDIO_INHERIT || args.stderr_disposition == MEL_PROCESS_STDIO_INHERIT));

    if (need_null)
    {
        null_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (null_fd < 0)
        {
            out.status = status_from_errno(errno);
            out.os_error = errno;
            return out;
        }
    }

    if (args.stdin_disposition == MEL_PROCESS_STDIO_PIPE && !make_pipe(in_pipe))
        goto pipe_fail;
    if (args.stdout_disposition == MEL_PROCESS_STDIO_PIPE && !make_pipe(out_pipe))
        goto pipe_fail;
    if (args.stderr_disposition == MEL_PROCESS_STDIO_PIPE && !make_pipe(err_pipe))
        goto pipe_fail;

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);

    int detach_in = args.detached ? null_fd : STDIN_FILENO;
    int detach_out = args.detached ? null_fd : STDOUT_FILENO;
    int detach_err = args.detached ? null_fd : STDERR_FILENO;

    int child_in = dispose_child_fd(args.stdin_disposition, args.stdin_redirect_fd, in_pipe[0], null_fd, detach_in);
    int child_out = dispose_child_fd(args.stdout_disposition, args.stdout_redirect_fd, out_pipe[1], null_fd, detach_out);
    int child_err;
    if (args.merge_stderr)
        child_err = child_out;
    else
        child_err = dispose_child_fd(args.stderr_disposition, args.stderr_redirect_fd, err_pipe[1], null_fd, detach_err);

    if (child_in >= 0 && child_in != STDIN_FILENO)
        posix_spawn_file_actions_adddup2(&fa, child_in, STDIN_FILENO);
    if (child_out >= 0 && child_out != STDOUT_FILENO)
        posix_spawn_file_actions_adddup2(&fa, child_out, STDOUT_FILENO);
    if (child_err >= 0 && child_err != STDERR_FILENO)
        posix_spawn_file_actions_adddup2(&fa, child_err, STDERR_FILENO);

    if (args.cwd)
    {
#if defined(__APPLE__)
        posix_spawn_file_actions_addchdir(&fa, args.cwd);
#else
        posix_spawn_file_actions_addchdir_np(&fa, args.cwd);
#endif
    }

    posix_spawnattr_t sa;
    posix_spawnattr_init(&sa);
    short flags = 0;
    if (args.detached)
    {
        flags |= POSIX_SPAWN_SETSID;
    }
    posix_spawnattr_setflags(&sa, flags);

    char** envp_storage = NULL;
    char** envp = build_envp(args, &envp_storage);
    if (!envp)
    {
        posix_spawn_file_actions_destroy(&fa);
        posix_spawnattr_destroy(&sa);
        out.status = MEL_PROCESS_ERROR | MEL_PROCESS_NO_MEMORY;
        goto cleanup_pipes;
    }

    pid_t pid = 0;
    int   rc = posix_spawnp(&pid, args.argv[0], &fa, &sa, (char* const*)args.argv, envp);

    free_envp(args, envp_storage);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&sa);

    if (rc != 0)
    {
        out.status = status_from_errno(rc);
        out.os_error = rc;
        goto cleanup_pipes;
    }

    out.pid = (i64)pid;
    out.status = MEL_PROCESS_OK;

    if (args.stdin_disposition == MEL_PROCESS_STDIO_PIPE)
    {
        close_fd(&in_pipe[0]);
        out.child_stdin.fd = in_pipe[1];
        in_pipe[1] = -1;
    }
    if (args.stdout_disposition == MEL_PROCESS_STDIO_PIPE)
    {
        close_fd(&out_pipe[1]);
        out.child_stdout.fd = out_pipe[0];
        out_pipe[0] = -1;
    }
    if (args.stderr_disposition == MEL_PROCESS_STDIO_PIPE && !args.merge_stderr)
    {
        close_fd(&err_pipe[1]);
        out.child_stderr.fd = err_pipe[0];
        err_pipe[0] = -1;
    }

    close_fd(&in_pipe[0]);
    close_fd(&in_pipe[1]);
    close_fd(&out_pipe[0]);
    close_fd(&out_pipe[1]);
    close_fd(&err_pipe[0]);
    close_fd(&err_pipe[1]);
    close_fd(&null_fd);
    return out;

pipe_fail:
    out.status = MEL_PROCESS_ERROR | MEL_PROCESS_PIPE_FAILED;
    out.os_error = errno;
cleanup_pipes:
    close_fd(&in_pipe[0]);
    close_fd(&in_pipe[1]);
    close_fd(&out_pipe[0]);
    close_fd(&out_pipe[1]);
    close_fd(&err_pipe[0]);
    close_fd(&err_pipe[1]);
    close_fd(&null_fd);
    return out;
}

static Mel_Process_Status decode_wait(int wstatus, i32* out_exit, i32* out_signal)
{
    if (WIFEXITED(wstatus))
    {
        *out_exit = WEXITSTATUS(wstatus);
        *out_signal = 0;
        return MEL_PROCESS_OK | MEL_PROCESS_EXITED;
    }
    if (WIFSIGNALED(wstatus))
    {
        *out_exit = -1;
        *out_signal = WTERMSIG(wstatus);
        return MEL_PROCESS_OK | MEL_PROCESS_SIGNALLED;
    }
    *out_exit = -1;
    *out_signal = 0;
    return MEL_PROCESS_OK;
}

bool mel_process__backend_reap(Mel_Process_Native* native, i32* out_exit, i32* out_signal, Mel_Process_Status* out_status)
{
    if (native->pid < 0)
    {
        *out_status = MEL_PROCESS_ERROR | MEL_PROCESS_BAD_HANDLE;
        return true;
    }
    int   wstatus = 0;
    pid_t r = waitpid((pid_t)native->pid, &wstatus, WNOHANG);
    if (r == 0)
        return false;
    if (r < 0)
    {
        if (errno == ECHILD)
        {
            *out_exit = -1;
            *out_signal = 0;
            *out_status = MEL_PROCESS_OK | MEL_PROCESS_EXITED;
            return true;
        }
        *out_exit = -1;
        *out_signal = 0;
        *out_status = MEL_PROCESS_ERROR | MEL_PROCESS_BAD_HANDLE;
        return true;
    }
    *out_status = decode_wait(wstatus, out_exit, out_signal);
    return true;
}

void mel_process__backend_wait_blocking(Mel_Process_Native* native, i32* out_exit, i32* out_signal, Mel_Process_Status* out_status)
{
    if (native->pid < 0)
    {
        *out_exit = -1;
        *out_signal = 0;
        *out_status = MEL_PROCESS_ERROR | MEL_PROCESS_BAD_HANDLE;
        return;
    }
    int   wstatus = 0;
    pid_t r;
    do
    {
        r = waitpid((pid_t)native->pid, &wstatus, 0);
    } while (r < 0 && errno == EINTR);

    if (r < 0)
    {
        *out_exit = -1;
        *out_signal = 0;
        *out_status = (errno == ECHILD) ? (MEL_PROCESS_OK | MEL_PROCESS_EXITED) : (MEL_PROCESS_ERROR | MEL_PROCESS_BAD_HANDLE);
        return;
    }
    *out_status = decode_wait(wstatus, out_exit, out_signal);
}

Mel_Process_Status mel_process__backend_signal(Mel_Process_Native* native, u32 signal)
{
    if (native->pid < 0)
        return MEL_PROCESS_ERROR | MEL_PROCESS_BAD_HANDLE;
    int sig = (signal == MEL_PROCESS_SIGNAL_KILL) ? SIGKILL : SIGTERM;
    if (kill((pid_t)native->pid, sig) != 0)
    {
        if (errno == ESRCH)
            return MEL_PROCESS_OK | MEL_PROCESS_EXITED;
        return MEL_PROCESS_ERROR | (errno == EPERM ? MEL_PROCESS_PERMISSION : MEL_PROCESS_BAD_HANDLE);
    }
    return MEL_PROCESS_OK | (signal == MEL_PROCESS_SIGNAL_KILL ? MEL_PROCESS_KILLED : 0u);
}

void mel_process__backend_close(Mel_Process_Native* native)
{
    close_fd(&native->child_stdin.fd);
    close_fd(&native->child_stdout.fd);
    close_fd(&native->child_stderr.fd);
}
