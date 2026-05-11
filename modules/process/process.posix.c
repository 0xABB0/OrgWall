#include "process.h"

#ifndef _WIN32

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

Mel_Proc mel__cmd_start_process__platform(Mel_Cmd cmd, Mel_Fd *fdin, Mel_Fd *fdout, Mel_Fd *fderr)
{
    pid_t cpid = fork();
    if (cpid < 0) {
        mel_log_error("process", "Could not fork child process: %s", strerror(errno));
        return MEL_INVALID_PROC;
    }

    if (cpid == 0) {
        if (fdin) {
            if (dup2(*fdin, STDIN_FILENO) < 0) {
                mel_log_error("process", "Could not setup stdin for child process: %s", strerror(errno));
                _exit(1);
            }
        }

        if (fdout) {
            if (dup2(*fdout, STDOUT_FILENO) < 0) {
                mel_log_error("process", "Could not setup stdout for child process: %s", strerror(errno));
                _exit(1);
            }
        }

        if (fderr) {
            if (dup2(*fderr, STDERR_FILENO) < 0) {
                mel_log_error("process", "Could not setup stderr for child process: %s", strerror(errno));
                _exit(1);
            }
        }

        Mel_Cmd cmd_null = {0};
        mel_array_init(&cmd_null, mel_alloc_heap());
        for (usize i = 0; i < cmd.count; i++) {
            mel_da_append(&cmd_null, cmd.items[i]);
        }
        mel_da_append(&cmd_null, NULL);

        if (execvp(cmd.items[0], (char * const *)cmd_null.items) < 0) {
            mel_log_error("process", "Could not exec child process for %s: %s", cmd.items[0], strerror(errno));
            _exit(1);
        }
    }

    return cpid;
}

i32 mel__proc_wait_async(Mel_Proc proc, i32 ms)
{
    if (proc == MEL_INVALID_PROC) return -1;

    i64 ns = (i64)ms * 1000 * 1000;
    struct timespec duration = {
        .tv_sec  = (time_t)(ns / 1000000000),
        .tv_nsec = (long)(ns % 1000000000),
    };

    i32 wstatus = 0;
    pid_t pid = waitpid(proc, &wstatus, WNOHANG);
    if (pid < 0) {
        mel_log_error("process", "could not wait on command (pid %d): %s", proc, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        nanosleep(&duration, NULL);
        return 0;
    }

    if (WIFEXITED(wstatus)) {
        i32 exit_status = WEXITSTATUS(wstatus);
        if (exit_status != 0) {
            mel_log_error("process", "command exited with exit code %d", exit_status);
            return -1;
        }
        return 1;
    }

    if (WIFSIGNALED(wstatus)) {
        mel_log_error("process", "command process was terminated by signal %d", WTERMSIG(wstatus));
        return -1;
    }

    nanosleep(&duration, NULL);
    return 0;
}

bool mel_proc_wait(Mel_Proc proc)
{
    if (proc == MEL_INVALID_PROC) return false;

    for (;;) {
        i32 wstatus = 0;
        if (waitpid(proc, &wstatus, 0) < 0) {
            mel_log_error("process", "could not wait on command (pid %d): %s", proc, strerror(errno));
            return false;
        }

        if (WIFEXITED(wstatus)) {
            i32 exit_status = WEXITSTATUS(wstatus);
            if (exit_status != 0) {
                mel_log_error("process", "command exited with exit code %d", exit_status);
                return false;
            }
            return true;
        }

        if (WIFSIGNALED(wstatus)) {
            mel_log_error("process", "command process was terminated by signal %d", WTERMSIG(wstatus));
            return false;
        }
    }
}

Mel_Fd mel_fd_open_for_read(const char *path)
{
    Mel_Fd result = open(path, O_RDONLY);
    if (result < 0) {
        mel_log_error("process", "Could not open file %s: %s", path, strerror(errno));
        return MEL_INVALID_FD;
    }
    return result;
}

Mel_Fd mel_fd_open_for_write(const char *path)
{
    Mel_Fd result = open(path, O_WRONLY | O_CREAT | O_TRUNC,
                         S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (result < 0) {
        mel_log_error("process", "could not open file %s: %s", path, strerror(errno));
        return MEL_INVALID_FD;
    }
    return result;
}

void mel_fd_close(Mel_Fd fd)
{
    close(fd);
}

i32 mel_nprocs(void)
{
    return (i32)sysconf(_SC_NPROCESSORS_ONLN);
}

#endif
