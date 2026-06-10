#include <routine/routine.h>
#include <allocator/allocator.h>

#include <string.h>

#define MEL_ROUTINE_RET_NONE  0
#define MEL_ROUTINE_RET_END   1
#define MEL_ROUTINE_RET_YIELD 2
#define MEL_ROUTINE_RET_WAIT  3

typedef union
{
    f32 tm;
    i32 n;
} Mel__Routine_Counter;

typedef struct Mel__Routine_State
{
    Mel_Fiber                  fiber;
    Mel_Fiber_Stack            stack_mem;
    Mel_Fiber_Cb               callback;
    void*                      user;
    i32                        ret_state;
    Mel__Routine_Counter       arg;
    Mel__Routine_Counter       counter;
    struct Mel__Routine_State* next;
    struct Mel__Routine_State* prev;
    bool                       init;
} Mel__Routine_State;

struct Mel_Routine_Context
{
    const Mel_Alloc*    alloc;
    Mel__Routine_State* run_list;
    Mel__Routine_State* run_list_last;
    Mel__Routine_State* free_list;
    Mel__Routine_State* cur_routine;
    u32                 stack_sz;
};

static inline void mel__routine_add_list(Mel__Routine_State** pfirst, Mel__Routine_State** plast, Mel__Routine_State* node)
{
    if (*plast)
    {
        (*plast)->next = node;
        node->prev = *plast;
    }
    *plast = node;
    if (*pfirst == NULL)
        *pfirst = node;
}

static inline void mel__routine_remove_list(Mel__Routine_State** pfirst, Mel__Routine_State** plast, Mel__Routine_State* node)
{
    if (node->prev)
        node->prev->next = node->next;
    if (node->next)
        node->next->prev = node->prev;
    if (*pfirst == node)
        *pfirst = node->next;
    if (*plast == node)
        *plast = node->prev;
    node->prev = node->next = NULL;
}

Mel_Routine_Context* mel_routine_create_opt(const Mel_Alloc* alloc, Mel_Routine_Create_Opt opt)
{
    if (opt.num_initial <= 0)
        opt.num_initial = MEL_ROUTINE_DEFAULT_INITIAL;
    if (opt.stack_size == 0)
        opt.stack_size = MEL_FIBER_DEFAULT_STACK_SIZE;

    Mel_Routine_Context* ctx = mel_alloc_type(alloc, Mel_Routine_Context);
    if (!ctx)
        return NULL;

    memset(ctx, 0, sizeof(Mel_Routine_Context));
    ctx->alloc = alloc;
    ctx->stack_sz = opt.stack_size;

    for (i32 i = 0; i < opt.num_initial; i++)
    {
        Mel__Routine_State* state = mel_alloc_type(alloc, Mel__Routine_State);
        if (!state)
            break;

        memset(state, 0, sizeof(Mel__Routine_State));

        if (!mel_fiber_stack_init(&state->stack_mem, ctx->stack_sz))
        {
            mel_dealloc(alloc, state);
            break;
        }
        state->init = true;

        state->next = ctx->free_list;
        ctx->free_list = state;
    }

    return ctx;
}

void mel_routine_destroy(Mel_Routine_Context* ctx)
{
    assert(ctx);

    const Mel_Alloc* alloc = ctx->alloc;

    Mel__Routine_State* fs = ctx->run_list;
    while (fs)
    {
        Mel__Routine_State* next = fs->next;
        if (fs->init)
            mel_fiber_stack_release(&fs->stack_mem);
        mel_dealloc(alloc, fs);
        fs = next;
    }

    fs = ctx->free_list;
    while (fs)
    {
        Mel__Routine_State* next = fs->next;
        if (fs->init)
            mel_fiber_stack_release(&fs->stack_mem);
        mel_dealloc(alloc, fs);
        fs = next;
    }

    mel_dealloc(alloc, ctx);
}

void mel__routine_invoke(Mel_Routine_Context* ctx, Mel_Fiber_Cb cb, void* user)
{
    Mel__Routine_State* fs = ctx->free_list;

    if (fs)
    {
        ctx->free_list = fs->next;
        if (fs->next)
            fs->next = NULL;
    }
    else
    {
        fs = mel_alloc_type(ctx->alloc, Mel__Routine_State);
        if (!fs)
            return;
        memset(fs, 0, sizeof(Mel__Routine_State));

        if (!mel_fiber_stack_init(&fs->stack_mem, ctx->stack_sz))
        {
            mel_dealloc(ctx->alloc, fs);
            return;
        }
        fs->init = true;
    }

    fs->prev = NULL;
    fs->next = NULL;
    fs->ret_state = MEL_ROUTINE_RET_NONE;
    fs->counter.n = 0;
    fs->arg.n = 0;

    fs->fiber = mel_fiber_create(fs->stack_mem, cb);
    fs->callback = cb;
    fs->user = user;

    mel__routine_add_list(&ctx->run_list, &ctx->run_list_last, fs);

    ctx->cur_routine = fs;
    fs->fiber = mel_fiber_switch(fs->fiber, user).from;
}

void mel_routine_update(Mel_Routine_Context* ctx, f32 dt)
{
    assert(ctx->cur_routine == NULL);

    Mel__Routine_State* fs = ctx->run_list;
    while (fs)
    {
        Mel__Routine_State* next = fs->next;

        switch (fs->ret_state)
        {
        case MEL_ROUTINE_RET_YIELD:
            ++fs->counter.n;
            if (fs->counter.n >= fs->arg.n)
            {
                ctx->cur_routine = fs;
                fs->fiber = mel_fiber_switch(fs->fiber, fs->user).from;
            }
            break;

        case MEL_ROUTINE_RET_WAIT:
            fs->counter.tm += dt;
            if (fs->counter.tm >= fs->arg.tm)
            {
                ctx->cur_routine = fs;
                fs->fiber = mel_fiber_switch(fs->fiber, fs->user).from;
            }
            break;

        default:
            assert(0 && "invalid ret_state in update loop");
            break;
        }

        fs = next;
    }
}

static inline void mel__routine_return(Mel_Routine_Context* ctx, Mel_Fiber* pfrom, i32 type, i32 arg)
{
    assert(ctx->cur_routine);
    assert(type != MEL_ROUTINE_RET_NONE);

    Mel__Routine_State* fs = ctx->cur_routine;

    if (type == MEL_ROUTINE_RET_END)
    {
        mel__routine_remove_list(&ctx->run_list, &ctx->run_list_last, fs);
        fs->next = ctx->free_list;
        ctx->free_list = fs;
    }
    else
    {
        fs->ret_state = type;
        fs->counter.n = 0;
        if (type == MEL_ROUTINE_RET_WAIT)
            fs->arg.tm = ((f32)arg) * 0.001f;
        else if (type == MEL_ROUTINE_RET_YIELD)
            fs->arg.n = arg;
    }

    ctx->cur_routine = NULL;
    *pfrom = mel_fiber_switch(*pfrom, NULL).from;
}

void mel__routine_end(Mel_Routine_Context* ctx, Mel_Fiber* pfrom) { mel__routine_return(ctx, pfrom, MEL_ROUTINE_RET_END, 0); }

void mel__routine_wait(Mel_Routine_Context* ctx, Mel_Fiber* pfrom, i32 msecs) { mel__routine_return(ctx, pfrom, MEL_ROUTINE_RET_WAIT, msecs); }

void mel__routine_yield(Mel_Routine_Context* ctx, Mel_Fiber* pfrom, i32 nupdates) { mel__routine_return(ctx, pfrom, MEL_ROUTINE_RET_YIELD, nupdates); }
