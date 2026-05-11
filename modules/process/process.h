#pragma once

#include "core.types.h"
#include "allocator.heap.h"
#include "log.h"
#include <string.h>

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

typedef struct {
    Mel_Proc *items;
    usize count;
    usize capacity;
} Mel_Procs;

typedef struct {
    const char **items;
    usize count;
    usize capacity;
} Mel_Cmd;

typedef struct {
    Mel_Procs *async;
    usize max_procs;
    bool dont_reset;
    const char *stdin_path;
    const char *stdout_path;
    const char *stderr_path;
} Mel_Cmd_Opt;

#define MEL_DA_INIT_CAP 8

#define mel__da_reserve(da, expected)                                                       \
    do {                                                                                    \
        if ((expected) > (da)->capacity) {                                                  \
            if ((da)->capacity == 0) {                                                      \
                (da)->capacity = MEL_DA_INIT_CAP;                                           \
            }                                                                               \
            while ((expected) > (da)->capacity) {                                           \
                (da)->capacity *= 2;                                                        \
            }                                                                               \
            if ((da)->items == NULL) {                                                      \
                (da)->items = mel_alloc(mel_alloc_heap(), (da)->capacity * sizeof(*(da)->items)); \
            } else {                                                                        \
                (da)->items = mel_realloc(mel_alloc_heap(), (da)->items,                    \
                                          (da)->capacity * sizeof(*(da)->items));           \
            }                                                                               \
        }                                                                                   \
    } while (0)

#define mel__da_free(da)                                                \
    do {                                                                \
        if ((da).items) {                                               \
            mel_dealloc(mel_alloc_heap(), (da).items);                  \
            (da).items = NULL;                                          \
            (da).count = 0;                                             \
            (da).capacity = 0;                                          \
        }                                                               \
    } while (0)

#define mel_cmd_append(cmd, ...)                                                              \
    do {                                                                                      \
        const char *__mel_args[] = {__VA_ARGS__};                                             \
        usize __mel_n = sizeof(__mel_args) / sizeof(__mel_args[0]);                           \
        mel__da_reserve((cmd), (cmd)->count + __mel_n);                                       \
        memcpy((cmd)->items + (cmd)->count, __mel_args, __mel_n * sizeof(const char *));      \
        (cmd)->count += __mel_n;                                                              \
    } while (0)

#define mel_cmd_extend(cmd, other)                                                  \
    do {                                                                            \
        mel__da_reserve((cmd), (cmd)->count + (other)->count);                      \
        memcpy((cmd)->items + (cmd)->count, (other)->items,                         \
               (other)->count * sizeof(const char *));                              \
        (cmd)->count += (other)->count;                                             \
    } while (0)

#define mel_cmd_free(cmd) mel__da_free(*(cmd))

#define mel_da_append(da, item)                   \
    do {                                          \
        mel__da_reserve((da), (da)->count + 1);   \
        (da)->items[(da)->count++] = (item);      \
    } while (0)

#define mel_da_remove_unordered(da, i)                  \
    do {                                                \
        assert((usize)(i) < (da)->count);               \
        (da)->items[(i)] = (da)->items[--(da)->count];  \
    } while (0)

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
