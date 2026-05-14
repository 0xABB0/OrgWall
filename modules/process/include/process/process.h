#pragma once

#include <core/types.h>
#include <collection.array/collection.array.h>
#include <allocator.heap/allocator.heap.h>
#include <log/log.h>

#ifdef _WIN32
#include <Windows.h>
typedef HANDLE Mel_Proc;
#define MEL_INVALID_PROC INVALID_HANDLE_VALUE
typedef HANDLE Mel_Fd;
#define MEL_INVALID_FD INVALID_HANDLE_VALUE
#else
typedef int Mel_Proc;
#define MEL_INVALID_PROC (-1)
typedef int Mel_Fd;
#define MEL_INVALID_FD (-1)
#endif

typedef Mel_Array(Mel_Proc)    Mel_Procs;
typedef Mel_Array(const char*) Mel_Cmd;

typedef struct {
    Mel_Procs *async;
    usize max_procs;
    bool dont_reset;
    const char *stdin_path;
    const char *stdout_path;
    const char *stderr_path;
} Mel_Cmd_Opt;

#define mel_cmd_append(cmd, ...)                                                              \
    do {                                                                                      \
        if ((cmd)->allocator == NULL) mel_array_init((cmd), mel_alloc_heap());                \
        const char *__mel_args[] = {__VA_ARGS__};                                             \
        usize __mel_n = sizeof(__mel_args) / sizeof(__mel_args[0]);                           \
        for (usize __mel_i = 0; __mel_i < __mel_n; __mel_i++)                                 \
            mel_array_push((cmd), __mel_args[__mel_i]);                                       \
    } while (0)

#define mel_cmd_extend(cmd, other)                                          \
    do {                                                                    \
        for (usize __mel_i = 0; __mel_i < (other)->count; __mel_i++)       \
            mel_array_push((cmd), (other)->items[__mel_i]);                \
    } while (0)

#define mel_cmd_free(cmd) mel_array_free(cmd)

#define mel_da_append mel_array_push
#define mel_da_remove_unordered mel_array_remove_unordered

Mel_Proc mel__cmd_start_process(Mel_Cmd cmd, Mel_Fd *fdin, Mel_Fd *fdout, Mel_Fd *fderr);
bool     mel_cmd_run_opt(Mel_Cmd *cmd, Mel_Cmd_Opt opt);

#define mel_cmd_run(cmd, ...) mel_cmd_run_opt((cmd), (Mel_Cmd_Opt){__VA_ARGS__})

bool mel_proc_wait(Mel_Proc proc);
bool mel_procs_wait(Mel_Procs procs);
bool mel_procs_flush(Mel_Procs *procs);
i32  mel_nprocs(void);

Mel_Fd mel_fd_open_for_read(const char *path);
Mel_Fd mel_fd_open_for_write(const char *path);
void   mel_fd_close(Mel_Fd fd);

Mel_Proc mel__cmd_start_process__platform(Mel_Cmd cmd, Mel_Fd *fdin, Mel_Fd *fdout, Mel_Fd *fderr);
i32      mel__proc_wait_async(Mel_Proc proc, i32 ms);
