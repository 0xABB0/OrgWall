#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Reactor Mel_Reactor;
typedef struct Mel_Future  Mel_Future;

typedef u32 Mel_Shell_Status;

#define MEL_SHELL_SEVERITY_MASK          0x3u
#define MEL_SHELL_OK                     0u
#define MEL_SHELL_WARNED                 1u
#define MEL_SHELL_ERROR                  2u

#define MEL_SHELL_RESULT_CANCELLED       (1u << 2)
#define MEL_SHELL_RESULT_NO_BACKEND      (1u << 3)
#define MEL_SHELL_RESULT_NO_HANDLER      (1u << 4)
#define MEL_SHELL_RESULT_DENIED          (1u << 5)
#define MEL_SHELL_RESULT_NOT_FOUND       (1u << 6)
#define MEL_SHELL_RESULT_BAD_TARGET      (1u << 7)
#define MEL_SHELL_RESULT_SPAWN_FAIL      (1u << 8)

#define MEL_SHELL_WARN_LAUNCH_UNVERIFIED (1u << 16)
#define MEL_SHELL_WARN_SCHEME_UNTRUSTED  (1u << 17)
#define MEL_SHELL_WARN_REVEAL_DEGRADED   (1u << 18)

static inline bool mel_shell_failed(Mel_Shell_Status s) { return (s & MEL_SHELL_SEVERITY_MASK) == MEL_SHELL_ERROR; }
static inline bool mel_shell_warned(Mel_Shell_Status s) { return (s & MEL_SHELL_SEVERITY_MASK) == MEL_SHELL_WARNED; }
static inline bool mel_shell_ok(Mel_Shell_Status s) { return (s & MEL_SHELL_SEVERITY_MASK) == MEL_SHELL_OK; }
static inline bool mel_shell_cancelled(Mel_Shell_Status s) { return (s & MEL_SHELL_RESULT_CANCELLED) != 0u; }

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Shell_Op;

#define MEL_SHELL_OP_NULL ((Mel_Shell_Op){ 0, 0 })

static inline bool mel_shell_op_valid(Mel_Shell_Op op) { return op.index != 0 || op.generation != 0; }
static inline bool mel_shell_op_equal(Mel_Shell_Op a, Mel_Shell_Op b) { return a.index == b.index && a.generation == b.generation; }

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Shell_Op*    out_op;
} Mel_Shell_Opt;

void mel_shell_init(const Mel_Alloc* alloc, Mel_Reactor* reactor);
void mel_shell_shutdown(void);
bool mel_shell_available(void);

Mel_Future* mel_shell_open_url_opt(str8 url, Mel_Shell_Opt opt);
Mel_Future* mel_shell_reveal_path_opt(str8 path, Mel_Shell_Opt opt);

#define mel_shell_open_url(url, ...)     mel_shell_open_url_opt((url), (Mel_Shell_Opt){ __VA_ARGS__ })
#define mel_shell_reveal_path(path, ...) mel_shell_reveal_path_opt((path), (Mel_Shell_Opt){ __VA_ARGS__ })

bool mel_shell_cancel(Mel_Shell_Op op);

Mel_Shell_Status mel_shell_future_status(const Mel_Future* f);
void             mel_shell_future_free(Mel_Future* f);

void* mel_shell_native(void);

#ifdef __cplusplus
}
#endif
