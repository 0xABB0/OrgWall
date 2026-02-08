#pragma once

#include "types.h"
#include "async.fiber.config.h"

typedef void* Mel_Fiber;
#define MEL_FIBER_INVALID NULL

typedef struct Mel_Fiber_Transfer {
    Mel_Fiber from;
    void*     user;
} Mel_Fiber_Transfer;

typedef struct Mel_Fiber_Stack {
    void* sptr;
    u32   ssize;
} Mel_Fiber_Stack;

typedef void (*Mel_Fiber_Cb)(Mel_Fiber_Transfer transfer);

bool               mel_fiber_stack_init(Mel_Fiber_Stack* fstack, u32 size);
void               mel_fiber_stack_init_ptr(Mel_Fiber_Stack* fstack, void* ptr, u32 size);
void               mel_fiber_stack_release(Mel_Fiber_Stack* fstack);
Mel_Fiber          mel_fiber_create(Mel_Fiber_Stack stack, Mel_Fiber_Cb cb);
Mel_Fiber_Transfer mel_fiber_switch(Mel_Fiber to, void* user);
