#include <async.coroutine/coroutine.h>
#include <allocator/allocator.h>

#include <string.h>

#define MEL_CORO_RET_NONE  0
#define MEL_CORO_RET_END   1
#define MEL_CORO_RET_YIELD 2
#define MEL_CORO_RET_WAIT  3

typedef union { f32 tm; i32 n; } Mel__Coro_Counter;

typedef struct Mel__Coro_State {
    Mel_Fiber            fiber;
    Mel_Fiber_Stack      stack_mem;
    Mel_Fiber_Cb         callback;
    void*                user;
    i32                  ret_state;
    Mel__Coro_Counter    arg;
    Mel__Coro_Counter    counter;
    struct Mel__Coro_State* next;
    struct Mel__Coro_State* prev;
    bool                 init;
} Mel__Coro_State;

struct Mel_Coro_Context {
    const Mel_Alloc*    alloc;
    Mel__Coro_State*    run_list;
    Mel__Coro_State*    run_list_last;
    Mel__Coro_State*    free_list;
    Mel__Coro_State*    cur_coro;
    u32                 stack_sz;
};

static inline void mel__coro_add_list(Mel__Coro_State** pfirst, Mel__Coro_State** plast,
                                      Mel__Coro_State* node)
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

static inline void mel__coro_remove_list(Mel__Coro_State** pfirst, Mel__Coro_State** plast,
                                         Mel__Coro_State* node)
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

Mel_Coro_Context* mel_coro_create_opt(const Mel_Alloc* alloc, Mel_Coro_Create_Opt opt)
{
    if (opt.num_initial <= 0)
        opt.num_initial = MEL_CORO_DEFAULT_INITIAL;
    if (opt.stack_size == 0)
        opt.stack_size = MEL_FIBER_DEFAULT_STACK_SIZE;

    Mel_Coro_Context* ctx = mel_alloc_type(alloc, Mel_Coro_Context);
    if (!ctx)
        return NULL;

    memset(ctx, 0, sizeof(Mel_Coro_Context));
    ctx->alloc    = alloc;
    ctx->stack_sz = opt.stack_size;

    for (i32 i = 0; i < opt.num_initial; i++)
    {
        Mel__Coro_State* state = mel_alloc_type(alloc, Mel__Coro_State);
        if (!state)
            break;

        memset(state, 0, sizeof(Mel__Coro_State));

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

void mel_coro_destroy(Mel_Coro_Context* ctx)
{
    assert(ctx);

    const Mel_Alloc* alloc = ctx->alloc;

    Mel__Coro_State* fs = ctx->run_list;
    while (fs)
    {
        Mel__Coro_State* next = fs->next;
        if (fs->init)
            mel_fiber_stack_release(&fs->stack_mem);
        mel_dealloc(alloc, fs);
        fs = next;
    }

    fs = ctx->free_list;
    while (fs)
    {
        Mel__Coro_State* next = fs->next;
        if (fs->init)
            mel_fiber_stack_release(&fs->stack_mem);
        mel_dealloc(alloc, fs);
        fs = next;
    }

    mel_dealloc(alloc, ctx);
}

void mel__coro_invoke(Mel_Coro_Context* ctx, Mel_Fiber_Cb cb, void* user)
{
    Mel__Coro_State* fs = ctx->free_list;

    if (fs)
    {
        ctx->free_list = fs->next;
        if (fs->next)
            fs->next = NULL;
    }
    else
    {
        fs = mel_alloc_type(ctx->alloc, Mel__Coro_State);
        if (!fs)
            return;
        memset(fs, 0, sizeof(Mel__Coro_State));

        if (!mel_fiber_stack_init(&fs->stack_mem, ctx->stack_sz))
        {
            mel_dealloc(ctx->alloc, fs);
            return;
        }
        fs->init = true;
    }

    fs->prev = NULL;
    fs->next = NULL;
    fs->ret_state = MEL_CORO_RET_NONE;
    fs->counter.n = 0;
    fs->arg.n = 0;

    fs->fiber    = mel_fiber_create(fs->stack_mem, cb);
    fs->callback = cb;
    fs->user     = user;

    mel__coro_add_list(&ctx->run_list, &ctx->run_list_last, fs);

    ctx->cur_coro = fs;
    fs->fiber = mel_fiber_switch(fs->fiber, user).from;
}

void mel_coro_update(Mel_Coro_Context* ctx, f32 dt)
{
    assert(ctx->cur_coro == NULL);

    Mel__Coro_State* fs = ctx->run_list;
    while (fs)
    {
        Mel__Coro_State* next = fs->next;

        switch (fs->ret_state)
        {
        case MEL_CORO_RET_YIELD:
            ++fs->counter.n;
            if (fs->counter.n >= fs->arg.n)
            {
                ctx->cur_coro = fs;
                fs->fiber = mel_fiber_switch(fs->fiber, fs->user).from;
            }
            break;

        case MEL_CORO_RET_WAIT:
            fs->counter.tm += dt;
            if (fs->counter.tm >= fs->arg.tm)
            {
                ctx->cur_coro = fs;
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

static inline void mel__coro_return(Mel_Coro_Context* ctx, Mel_Fiber* pfrom, i32 type, i32 arg)
{
    assert(ctx->cur_coro);
    assert(type != MEL_CORO_RET_NONE);

    Mel__Coro_State* fs = ctx->cur_coro;

    if (type == MEL_CORO_RET_END)
    {
        mel__coro_remove_list(&ctx->run_list, &ctx->run_list_last, fs);
        fs->next = ctx->free_list;
        ctx->free_list = fs;
    }
    else
    {
        fs->ret_state = type;
        fs->counter.n = 0;
        if (type == MEL_CORO_RET_WAIT)
            fs->arg.tm = ((f32)arg) * 0.001f;
        else if (type == MEL_CORO_RET_YIELD)
            fs->arg.n = arg;
    }

    ctx->cur_coro = NULL;
    *pfrom = mel_fiber_switch(*pfrom, NULL).from;
}

void mel__coro_end(Mel_Coro_Context* ctx, Mel_Fiber* pfrom)
{
    mel__coro_return(ctx, pfrom, MEL_CORO_RET_END, 0);
}

void mel__coro_wait(Mel_Coro_Context* ctx, Mel_Fiber* pfrom, i32 msecs)
{
    mel__coro_return(ctx, pfrom, MEL_CORO_RET_WAIT, msecs);
}

void mel__coro_yield(Mel_Coro_Context* ctx, Mel_Fiber* pfrom, i32 nupdates)
{
    mel__coro_return(ctx, pfrom, MEL_CORO_RET_YIELD, nupdates);
}
