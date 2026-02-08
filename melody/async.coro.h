#pragma once

#include "types.h"
#include "async.fiber.h"
#include "async.coro.cfg.h"
#include "allocator.fwd.h"

typedef struct Mel_Coro_Context Mel_Coro_Context;

typedef struct {
    i32 num_initial;
    u32 stack_size;
} Mel_Coro_Create_Opt;

Mel_Coro_Context* mel_coro_create_opt(const Mel_Alloc* alloc, Mel_Coro_Create_Opt);
#define mel_coro_create(alloc, ...) mel_coro_create_opt((alloc), (Mel_Coro_Create_Opt){__VA_ARGS__})

void mel_coro_destroy(Mel_Coro_Context* ctx);
void mel_coro_update(Mel_Coro_Context* ctx, f32 dt);

void mel__coro_invoke(Mel_Coro_Context* ctx, Mel_Fiber_Cb cb, void* user);
void mel__coro_end(Mel_Coro_Context* ctx, Mel_Fiber* pfrom);
void mel__coro_wait(Mel_Coro_Context* ctx, Mel_Fiber* pfrom, i32 msecs);
void mel__coro_yield(Mel_Coro_Context* ctx, Mel_Fiber* pfrom, i32 nupdates);

#define mel_coro_declare(_name)             static void coro__##_name(Mel_Fiber_Transfer __transfer)
#define mel_coro_userdata()                 __transfer.user
#define mel_coro_end(_ctx)                  mel__coro_end((_ctx), &__transfer.from)
#define mel_coro_wait(_ctx, _ms)            mel__coro_wait((_ctx), &__transfer.from, (_ms))
#define mel_coro_yield(_ctx)                mel__coro_yield((_ctx), &__transfer.from, 1)
#define mel_coro_yieldn(_ctx, _n)           mel__coro_yield((_ctx), &__transfer.from, (_n))
#define mel_coro_invoke(_ctx, _name, _user) mel__coro_invoke((_ctx), coro__##_name, (_user))
