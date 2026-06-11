#include <shell/backend.h>
#include <allocator/allocator.h>
#include <string/str8.h>
#include <log/log.h>

#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

extern char** environ;

bool mel_shell__plat_available(void) { return true; }

static Mel_Shell_Status spawn_detached(char* const argv[])
{
    posix_spawnattr_t attr;
    if (posix_spawnattr_init(&attr) != 0)
        return MEL_SHELL_ERROR | MEL_SHELL_RESULT_SPAWN_FAIL;
    sigset_t empty;
    sigemptyset(&empty);
    posix_spawnattr_setsigmask(&attr, &empty);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);

    pid_t pid = 0;
    int   rc = posix_spawnp(&pid, argv[0], NULL, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);
    if (rc != 0)
    {
        mel_log_error("shell", "posix_spawnp %s: %s", argv[0], strerror(rc));
        return MEL_SHELL_ERROR | (rc == ENOENT ? MEL_SHELL_RESULT_NO_HANDLER : MEL_SHELL_RESULT_SPAWN_FAIL);
    }
    int   wst = 0;
    pid_t reaped;
    do
        reaped = waitpid(pid, &wst, 0);
    while (reaped < 0 && errno == EINTR);
    if (reaped < 0)
    {
        mel_log_error("shell", "waitpid %s: %s", argv[0], strerror(errno));
        return MEL_SHELL_ERROR | MEL_SHELL_RESULT_SPAWN_FAIL;
    }
    if (WIFEXITED(wst) && WEXITSTATUS(wst) != 0)
        return MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_HANDLER;
    return MEL_SHELL_OK;
}

void mel_shell__plat_open_url(Mel_Shell_Job* job)
{
    const Mel_Alloc* a = mel_shell_job_alloc(job);
    char*            t = (char*)str8_to_cstr(mel_shell_job_target(job), a);
    if (!t)
    {
        mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_BAD_TARGET);
        return;
    }
    char* const      argv[] = { (char*)"xdg-open", t, NULL };
    Mel_Shell_Status s = spawn_detached(argv);
    mel_dealloc(a, t);
    mel_shell_job_resolve(job, s);
}

static bool dbus_show_items(const Mel_Alloc* a, str8 path)
{
    str8 uri = str8_starts_with(path, S8("file://")) ? str8_dup(path, a) : str8_fmt(a, "file://%.*s", (int)path.len, (const char*)path.data);
    if (str8_is_empty(uri))
        return false;
    char* arg = (char*)str8_fmt(a, "array:string:%.*s", (int)uri.len, (const char*)uri.data).data;
    mel_dealloc(a, uri.data);
    if (!arg)
        return false;

    char* const argv[] = {
        (char*)"dbus-send", (char*)"--session", (char*)"--dest=org.freedesktop.FileManager1", (char*)"--type=method_call", (char*)"/org/freedesktop/FileManager1", (char*)"org.freedesktop.FileManager1.ShowItems", arg, (char*)"string:", NULL,
    };

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int   rc = posix_spawnp(&pid, "dbus-send", &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    mel_dealloc(a, arg);
    if (rc != 0)
        return false;
    int wst = 0;
    waitpid(pid, &wst, 0);
    return WIFEXITED(wst) && WEXITSTATUS(wst) == 0;
}

void mel_shell__plat_reveal_path(Mel_Shell_Job* job)
{
    const Mel_Alloc* a = mel_shell_job_alloc(job);
    str8             target = mel_shell_job_target(job);
    char*            t = (char*)str8_to_cstr(target, a);
    if (!t)
    {
        mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_BAD_TARGET);
        return;
    }
    struct stat st;
    if (stat(t, &st) != 0 && !str8_starts_with(target, S8("file://")))
    {
        mel_dealloc(a, t);
        mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_NOT_FOUND);
        return;
    }

    if (dbus_show_items(a, target))
    {
        mel_dealloc(a, t);
        mel_shell_job_resolve(job, MEL_SHELL_OK);
        return;
    }

    size             slash = str8_rfind(target, S8("/"));
    str8             dir = (slash > 0) ? str8_prefix(target, slash) : S8(".");
    char*            d = (char*)str8_to_cstr(dir, a);
    char* const      argv[] = { (char*)"xdg-open", d ? d : t, NULL };
    Mel_Shell_Status s = spawn_detached(argv);
    if (d)
        mel_dealloc(a, d);
    mel_dealloc(a, t);
    if (mel_shell_ok(s))
        mel_shell_job_resolve(job, MEL_SHELL_WARNED | MEL_SHELL_WARN_REVEAL_DEGRADED);
    else
        mel_shell_job_resolve(job, s);
}

void* mel_shell__plat_native(void) { return NULL; }
