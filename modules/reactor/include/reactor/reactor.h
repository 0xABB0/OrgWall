#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>

typedef struct Mel_Reactor                  Mel_Reactor;
typedef struct Mel_Reactor_Source           Mel_Reactor_Source;
typedef struct Mel_Reactor_Source_Callbacks Mel_Reactor_Source_Callbacks;
typedef struct Mel_Reactor_Poll             Mel_Reactor_Poll;
typedef struct Mel_Reactor_Spawn_Opt        Mel_Reactor_Spawn_Opt;

typedef enum {
    MEL_REACTOR_THREADED = 1,
    MEL_REACTOR_ATTACHED = 2,
} Mel_Reactor_Mode;

enum {
    MEL_REACTOR_NOWAIT  =  0,
    MEL_REACTOR_FOREVER = -1,
};

enum {
    MEL_REACTOR_READY_TIME_NEVER = -1,
    MEL_REACTOR_READY_TIME_NOW   =  0,
};

enum {
    MEL_REACTOR_PRIORITY_HIGH    = -100,
    MEL_REACTOR_PRIORITY_DEFAULT =    0,
    MEL_REACTOR_PRIORITY_LOW     =  100,
    MEL_REACTOR_PRIORITY_IDLE    =  200,
};

enum {
    MEL_REACTOR_POLL_IN  = 1u << 0,
    MEL_REACTOR_POLL_OUT = 1u << 1,
    MEL_REACTOR_POLL_ERR = 1u << 2,
    MEL_REACTOR_POLL_HUP = 1u << 3,
};

struct Mel_Reactor_Poll {
    i64 handle;
    u32 events;
    u32 revents;
};

typedef bool (*Mel_Reactor_Source_Proc)(void* user);
typedef bool (*Mel_Reactor_Init_Proc)  (Mel_Reactor* reactor, void* user);
typedef void (*Mel_Reactor_Post_Proc)  (void* user);

typedef bool (*Mel_Reactor_Source_Prepare_Proc)  (Mel_Reactor_Source* source, i32* timeout);
typedef bool (*Mel_Reactor_Source_Check_Proc)    (Mel_Reactor_Source* source);
typedef bool (*Mel_Reactor_Source_Dispatch_Proc) (Mel_Reactor_Source* source, Mel_Reactor_Source_Proc callback, void* user);
typedef void (*Mel_Reactor_Source_Finalize_Proc) (Mel_Reactor_Source* source);

struct Mel_Reactor_Source_Callbacks {
    Mel_Reactor_Source_Prepare_Proc  prepare;
    Mel_Reactor_Source_Check_Proc    check;
    Mel_Reactor_Source_Dispatch_Proc dispatch;
    Mel_Reactor_Source_Finalize_Proc finalize;
};

struct Mel_Reactor_Source {
    const Mel_Reactor_Source_Callbacks* cb;
    Mel_Reactor_Source_Proc             callback;
    void*                               user;

    Mel_Reactor*        reactor;
    Mel_Reactor_Source* prev;
    Mel_Reactor_Source* next;

    Mel_Reactor_Poll**  polls;
    u16                 poll_count;
    u16                 poll_cap;

    i32                 priority;
    i64                 ready_time;

    bool                ready;
    bool                attached;
    bool                destroyed;
    bool                detach_pending;
    bool                external_storage;
};

struct Mel_Reactor_Spawn_Opt {
    const Mel_Alloc* alloc;
    const Mel_Alloc* post_alloc;
};

int  mel_reactor_spawn_opt (Mel_Reactor_Mode mode, Mel_Reactor_Init_Proc init, void* user, Mel_Reactor_Spawn_Opt opt);
#define mel_reactor_spawn(mode, init, user, ...) mel_reactor_spawn_opt((mode), (init), (user), (Mel_Reactor_Spawn_Opt){__VA_ARGS__})

void mel_reactor_quit      (Mel_Reactor* reactor);
void mel_reactor_post      (Mel_Reactor* reactor, Mel_Reactor_Post_Proc callback, void* user);
bool mel_reactor_is_running(const Mel_Reactor* reactor);
bool mel_reactor_is_owner  (const Mel_Reactor* reactor);

Mel_Reactor_Source* mel_reactor_source_new    (const Mel_Reactor_Source_Callbacks* cb, usize struct_size);
void                mel_reactor_source_init   (Mel_Reactor_Source* source, const Mel_Reactor_Source_Callbacks* cb);
void                mel_reactor_source_attach (Mel_Reactor* reactor, Mel_Reactor_Source* source);
void                mel_reactor_source_detach (Mel_Reactor_Source* source);
void                mel_reactor_source_destroy(Mel_Reactor_Source* source);

void         mel_reactor_source_set_callback  (Mel_Reactor_Source* source, Mel_Reactor_Source_Proc callback, void* user);
void         mel_reactor_source_set_priority  (Mel_Reactor_Source* source, i32 priority);
i32          mel_reactor_source_get_priority  (const Mel_Reactor_Source* source);
void         mel_reactor_source_set_ready_time(Mel_Reactor_Source* source, i64 monotonic_ns);
i64          mel_reactor_source_get_ready_time(const Mel_Reactor_Source* source);
Mel_Reactor* mel_reactor_source_reactor       (const Mel_Reactor_Source* source);

void mel_reactor_source_add_poll   (Mel_Reactor_Source* source, Mel_Reactor_Poll* poll);
void mel_reactor_source_remove_poll(Mel_Reactor_Source* source, Mel_Reactor_Poll* poll);

Mel_Reactor_Source* mel_reactor_idle_new          (Mel_Reactor_Source_Proc callback, void* user);
Mel_Reactor_Source* mel_reactor_timer_new         (i64 interval_ns, Mel_Reactor_Source_Proc callback, void* user);
void                mel_reactor_timer_set_interval(Mel_Reactor_Source* source, i64 interval_ns);
