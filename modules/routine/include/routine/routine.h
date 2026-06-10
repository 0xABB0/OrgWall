#pragma once

#include <core/types.h>
#include <fiber/fiber.h>
#include <routine/routine.cfg.h>
#include <allocator/allocator.fwd.h>

typedef struct Mel_Routine_Context Mel_Routine_Context;

typedef struct
{
    i32 num_initial;
    u32 stack_size;
} Mel_Routine_Create_Opt;

Mel_Routine_Context* mel_routine_create_opt(const Mel_Alloc* alloc, Mel_Routine_Create_Opt);
#define mel_routine_create(alloc, ...) mel_routine_create_opt((alloc), (Mel_Routine_Create_Opt){ __VA_ARGS__ })

void mel_routine_destroy(Mel_Routine_Context* ctx);
void mel_routine_update(Mel_Routine_Context* ctx, f32 dt);

void mel__routine_invoke(Mel_Routine_Context* ctx, Mel_Fiber_Cb cb, void* user);
void mel__routine_end(Mel_Routine_Context* ctx, Mel_Fiber* pfrom);
void mel__routine_wait(Mel_Routine_Context* ctx, Mel_Fiber* pfrom, i32 msecs);
void mel__routine_yield(Mel_Routine_Context* ctx, Mel_Fiber* pfrom, i32 nupdates);

#define mel_routine_declare(_name)             static void routine__##_name(Mel_Fiber_Transfer __transfer)
#define mel_routine_userdata()                 __transfer.user
#define mel_routine_end(_ctx)                  mel__routine_end((_ctx), &__transfer.from)
#define mel_routine_wait(_ctx, _ms)            mel__routine_wait((_ctx), &__transfer.from, (_ms))
#define mel_routine_yield(_ctx)                mel__routine_yield((_ctx), &__transfer.from, 1)
#define mel_routine_yieldn(_ctx, _n)           mel__routine_yield((_ctx), &__transfer.from, (_n))
#define mel_routine_invoke(_ctx, _name, _user) mel__routine_invoke((_ctx), routine__##_name, (_user))
