#pragma once

#include <allocator/allocator.fwd.h>
#include <core/types.h>
#include <executor/executor.h>

#define MEL_VAT_NEVER    INT64_MAX

#define MEL_VAT_WAKE_IN  (1u << 0)
#define MEL_VAT_WAKE_OUT (1u << 1)
#define MEL_VAT_WAKE_ERR (1u << 2)
#define MEL_VAT_WAKE_HUP (1u << 3)

typedef struct Mel_Vat          Mel_Vat;
typedef struct Mel_Vat_Source   Mel_Vat_Source;
typedef struct Mel_Vat_Waiter   Mel_Vat_Waiter;
typedef struct Mel_Vat_Driver   Mel_Vat_Driver;
typedef struct Mel_Vat_Embedder Mel_Vat_Embedder;

typedef struct Mel_Vat_Wakeable
{
    i64 handle;
    u32 events;
    u32 revents;
} Mel_Vat_Wakeable;

typedef struct Mel_Vat_Source_Vtbl
{
    void (*wakeables)(Mel_Vat_Source* source, Mel_Vat_Wakeable** out, usize* count);
    i64 (*deadline)(Mel_Vat_Source* source);
    bool (*drain)(Mel_Vat_Source* source, u32 budget);
    void (*cancel)(Mel_Vat_Source* source);
} Mel_Vat_Source_Vtbl;

typedef struct Mel_Vat_Waiter_Vtbl
{
    bool (*arm)(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable);
    void (*disarm)(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable);
    i32 (*wait)(Mel_Vat_Waiter* waiter, i64 timeout_ns);
    void (*ring)(Mel_Vat_Waiter* waiter);
    void (*close)(Mel_Vat_Waiter* waiter);
} Mel_Vat_Waiter_Vtbl;

struct Mel_Vat_Waiter
{
    const Mel_Vat_Waiter_Vtbl* vt;
};

typedef struct Mel_Vat_Driver_Vtbl
{
    bool (*turn)(Mel_Vat_Driver* driver, Mel_Vat* vat);
    void (*close)(Mel_Vat_Driver* driver);
} Mel_Vat_Driver_Vtbl;

typedef struct Mel_Vat_Embedder_Vtbl
{
    void (*schedule_work)(Mel_Vat_Embedder* embedder);
    void (*schedule_delayed_work)(Mel_Vat_Embedder* embedder, i64 delay_ns);
    void (*close)(Mel_Vat_Embedder* embedder);
} Mel_Vat_Embedder_Vtbl;

struct Mel_Vat_Embedder
{
    const Mel_Vat_Embedder_Vtbl* vt;
};

struct Mel_Vat_Driver
{
    const Mel_Vat_Driver_Vtbl* vt;
};

typedef struct Mel_Vat_Desc
{
    Mel_Vat_Waiter* waiter;
    Mel_Vat_Driver* driver;
} Mel_Vat_Desc;

Mel_Vat*         mel_vat_open(const Mel_Alloc* alloc, Mel_Vat_Desc desc);
void             mel_vat_close(Mel_Vat* vat);
void             mel_vat_retain(Mel_Vat* vat);
void             mel_vat_release(Mel_Vat* vat);
void             mel_vat_run(Mel_Vat* vat);
bool             mel_vat_step(Mel_Vat* vat);
void             mel_vat_quit(Mel_Vat* vat);
i32              mel_vat_depth(const Mel_Vat* vat);
void             mel_vat_post(Mel_Vat* vat, Mel_Task* task);
Mel_Executor*    mel_vat_executor(Mel_Vat* vat);
bool             mel_vat_is_owner(const Mel_Vat* vat);
const Mel_Alloc* mel_vat_alloc(const Mel_Vat* vat);
Mel_Vat_Waiter*  mel_vat_waiter(Mel_Vat* vat);

Mel_Vat_Source* mel_vat_source_open(Mel_Vat* vat, const Mel_Vat_Source_Vtbl* vt, void* state);
void            mel_vat_source_close(Mel_Vat_Source* source);
void*           mel_vat_source_state(Mel_Vat_Source* source);
Mel_Vat*        mel_vat_source_vat(Mel_Vat_Source* source);
void            mel_vat_source_demand_changed(Mel_Vat_Source* source);

Mel_Vat_Waiter* mel_vat_waiter_kqueue(const Mel_Alloc* alloc);
Mel_Vat_Waiter* mel_vat_waiter_epoll(const Mel_Alloc* alloc);
Mel_Vat_Waiter* mel_vat_waiter_cocoa(const Mel_Alloc* alloc);
Mel_Vat_Waiter* mel_vat_waiter_ui(const Mel_Alloc* alloc);
Mel_Vat_Waiter* mel_vat_waiter_io(const Mel_Alloc* alloc);
Mel_Vat_Waiter* mel_vat_waiter_guest(const Mel_Alloc* alloc, Mel_Vat_Embedder* embedder);
Mel_Vat_Driver* mel_vat_driver_fair(const Mel_Alloc* alloc, u32 budget);
