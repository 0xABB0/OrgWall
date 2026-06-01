#pragma once

#include <core/types.h>
#include <reactor/reactor.h>

typedef struct Mel_Gpu_Completion_Pump Mel_Gpu_Completion_Pump;
typedef struct Mel_Gpu_Future          Mel_Gpu_Future;

typedef void (*Mel_Gpu_Continuation)(Mel_Gpu_Future* f, void* user);
typedef bool (*Mel_Gpu_Poll_Fn)(void* user);

typedef struct
{
    u32 high_water;
    i64 tick_interval_ns;
} Mel_Gpu_Pump_Opt;

Mel_Gpu_Completion_Pump* mel_gpu_pump_create_opt(Mel_Reactor* reactor, Mel_Gpu_Pump_Opt opt);
#define mel_gpu_pump_create(reactor, ...) mel_gpu_pump_create_opt((reactor), (Mel_Gpu_Pump_Opt){ __VA_ARGS__ })

void         mel_gpu_pump_destroy(Mel_Gpu_Completion_Pump* pump);
void         mel_gpu_pump_tick(Mel_Gpu_Completion_Pump* pump);
void         mel_gpu_pump_add_poller(Mel_Gpu_Completion_Pump* pump, Mel_Gpu_Poll_Fn fn, void* user);
void         mel_gpu_pump_remove_poller(Mel_Gpu_Completion_Pump* pump, Mel_Gpu_Poll_Fn fn, void* user);
Mel_Reactor* mel_gpu_pump_reactor(Mel_Gpu_Completion_Pump* pump);

Mel_Gpu_Future* mel_gpu_future_create(Mel_Gpu_Completion_Pump* pump, Mel_Reactor* target);
void            mel_gpu_future_destroy(Mel_Gpu_Future* f);
void            mel_gpu_future_then(Mel_Gpu_Future* f, Mel_Gpu_Continuation cont, void* user);
void            mel_gpu_future_resolve(Mel_Gpu_Future* f, void* value, u32 status);
bool            mel_gpu_future_resolved(Mel_Gpu_Future* f);
void*           mel_gpu_future_value(Mel_Gpu_Future* f);
u32             mel_gpu_future_status(Mel_Gpu_Future* f);

u32 mel_gpu_future_wait(Mel_Gpu_Future* f);
