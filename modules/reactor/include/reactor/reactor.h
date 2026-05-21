#pragma once

#include <core/types.h>

typedef struct Mel_Reactor                  Mel_Reactor;
typedef struct Mel_Reactor_Source           Mel_Reactor_Source;
typedef struct Mel_Reactor_Source_Callbacks Mel_Reactor_Source_Callbacks;
typedef struct Mel_Reactor_Poll             Mel_Reactor_Poll;

enum {
    MEL_REACTOR_NOWAIT  =  0,
    MEL_REACTOR_FOREVER = -1,
};

enum {
    MEL_REACTOR_POLL_IN  = 1u << 0,
    MEL_REACTOR_POLL_OUT = 1u << 1,
    MEL_REACTOR_POLL_ERR = 1u << 2,
    MEL_REACTOR_POLL_HUP = 1u << 3,
};

enum {
    MEL_REACTOR_SOURCE_READY = 1u << 0,
};

struct Mel_Reactor_Poll {
    i64 handle;
    u32 events;
    u32 revents;
};

typedef bool (*Mel_Reactor_Source_Proc)(void* user);

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
    Mel_Reactor_Source_Callbacks cb;
    Mel_Reactor_Source_Proc      callback;
    void*                        user;
    u32                          flags;

    Mel_Reactor*        reactor;
    Mel_Reactor_Source* prev;
    Mel_Reactor_Source* next;

    Mel_Reactor_Poll**  polls;
    usize               poll_count;
    usize               poll_cap;

    bool ready;
    bool attached;
    bool destroyed;
    bool detach_pending;
};

bool         mel_reactor_init(void);
void         mel_reactor_shutdown(void);
Mel_Reactor* mel_reactor_system(void);

void mel_reactor_run(Mel_Reactor* reactor);
bool mel_reactor_iterate(Mel_Reactor* reactor, bool may_block);
void mel_reactor_quit(Mel_Reactor* reactor);
void mel_reactor_wake(Mel_Reactor* reactor);
bool mel_reactor_is_running(Mel_Reactor* reactor);

Mel_Reactor_Source* mel_reactor_source_new(const Mel_Reactor_Source_Callbacks* cb, usize struct_size);
void                mel_reactor_source_attach(Mel_Reactor* reactor, Mel_Reactor_Source* source);
void                mel_reactor_source_detach(Mel_Reactor_Source* source);
void                mel_reactor_source_destroy(Mel_Reactor_Source* source);

void         mel_reactor_source_set_callback(Mel_Reactor_Source* source, Mel_Reactor_Source_Proc callback, void* user);
Mel_Reactor* mel_reactor_source_reactor(const Mel_Reactor_Source* source);

void mel_reactor_source_add_poll(Mel_Reactor_Source* source, Mel_Reactor_Poll* poll);
void mel_reactor_source_remove_poll(Mel_Reactor_Source* source, Mel_Reactor_Poll* poll);

Mel_Reactor_Source* mel_reactor_idle_new(Mel_Reactor_Source_Proc callback, void* user);
Mel_Reactor_Source* mel_reactor_timer_new(i64 interval_ns, Mel_Reactor_Source_Proc callback, void* user);
void                mel_reactor_timer_set_interval(Mel_Reactor_Source* source, i64 interval_ns);
