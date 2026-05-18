#pragma once

#include <reactor/reactor.h>
#include <collection.array/array.h>

enum {
    MEL__REACTOR_KIND_INIT    = 0,
    MEL__REACTOR_KIND_EVENT   = 1,
    MEL__REACTOR_KIND_UPDATE  = 2,
    MEL__REACTOR_KIND_DESTROY = 3,
    MEL__REACTOR_KIND_COUNT   = 4,
};

typedef struct {
    void* fn;
    void* user;
    u32   generation;
    bool  alive;
} Mel_Reactor_Listener_Slot;

typedef Mel_Array(Mel_Reactor_Listener_Slot) Mel_Reactor_Listener_Array;

typedef struct Mel_Reactor_Backend Mel_Reactor_Backend;

typedef struct Mel_Reactor_Internal {
    Mel_Reactor                self;
    bool                       is_system;
    u32                        next_listener_gen;
    Mel_Reactor_Listener_Array listeners[MEL__REACTOR_KIND_COUNT];
    Mel_Reactor_Backend*       backend;
} Mel_Reactor_Internal;

Mel_Reactor_Internal* mel__reactor_lookup(Mel_Reactor handle);

void mel__reactor_dispatch_init   (Mel_Reactor_Internal* r);
void mel__reactor_dispatch_event  (Mel_Reactor_Internal* r, void* message);
void mel__reactor_dispatch_update (Mel_Reactor_Internal* r);
void mel__reactor_dispatch_destroy(Mel_Reactor_Internal* r);

bool mel__reactor_backend_adopt_system  (Mel_Reactor_Internal* r);
void mel__reactor_backend_release_system(Mel_Reactor_Internal* r);
bool mel__reactor_backend_create_user   (Mel_Reactor_Internal* r);
void mel__reactor_backend_destroy_user  (Mel_Reactor_Internal* r);

void mel__reactor_backend_run (Mel_Reactor_Internal* r);
void mel__reactor_backend_stop(Mel_Reactor_Internal* r);
void mel__reactor_backend_post(Mel_Reactor_Internal* r, void* message);

bool mel__reactor_backend_owns_caller(Mel_Reactor_Internal* r);
