#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <collection/array.h>
#include <executor/executor.h>

#include <stdatomic.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Future_Status;

#define MEL_FUTURE_SEVERITY_MASK 0x3u
#define MEL_FUTURE_OK            0u
#define MEL_FUTURE_WARNED        1u
#define MEL_FUTURE_ERROR         2u

#define MEL_FUTURE_CANCELLED     (1u << 2)
#define MEL_FUTURE_TIMED_OUT     (1u << 3)
#define MEL_FUTURE_BROKEN        (1u << 4)
#define MEL_FUTURE_PARTIAL       (1u << 8)

static inline bool mel_future_status_failed(Mel_Future_Status s) { return (s & MEL_FUTURE_SEVERITY_MASK) == MEL_FUTURE_ERROR; }
static inline bool mel_future_status_warned(Mel_Future_Status s) { return (s & MEL_FUTURE_SEVERITY_MASK) == MEL_FUTURE_WARNED; }
static inline bool mel_future_status_cancelled(Mel_Future_Status s) { return (s & MEL_FUTURE_CANCELLED) != 0u; }

typedef struct Mel_Future Mel_Future;

typedef void (*Mel_Future_Free)(void* value, const Mel_Alloc* alloc);

struct Mel_Future
{
    _Atomic(u32)      state;
    Mel_Future_Status status;
    void*             value;

    _Atomic(Mel_Task*) cont;
    Mel_Executor*      cont_exec;

    Mel_Future_Free  free_value;
    const Mel_Alloc* alloc;
};

void mel_future_init(Mel_Future* f, Mel_Future_Free free_value, const Mel_Alloc* alloc);

bool mel_future_resolve(Mel_Future* f, void* value, Mel_Future_Status status);
bool mel_future_cancel(Mel_Future* f);

void mel_future_then(Mel_Future* f, Mel_Task* cont, Mel_Executor* target_executor);

bool              mel_future_resolved(const Mel_Future* f);
void*             mel_future_value(const Mel_Future* f);
Mel_Future_Status mel_future_status(const Mel_Future* f);

typedef struct Mel_Future_Scope Mel_Future_Scope;

struct Mel_Future_Scope
{
    Mel_Array(Mel_Future*) children;
    const Mel_Alloc* alloc;
};

void mel_future_scope_init(Mel_Future_Scope* scope, const Mel_Alloc* alloc);
void mel_future_scope_adopt(Mel_Future_Scope* scope, Mel_Future* f);
void mel_future_scope_cancel(Mel_Future_Scope* scope);
void mel_future_scope_teardown(Mel_Future_Scope* scope);

typedef struct Mel_Future_When Mel_Future_When;

Mel_Future_When* mel_future_when_all(Mel_Future* const* inputs, usize n, const Mel_Alloc* alloc);
Mel_Future_When* mel_future_when_any(Mel_Future* const* inputs, usize n, const Mel_Alloc* alloc);
Mel_Future*      mel_future_when_future(Mel_Future_When* w);
void             mel_future_when_free(Mel_Future_When* w);

#ifdef __cplusplus
}
#endif
