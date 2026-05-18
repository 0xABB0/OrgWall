#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <reactor/reactor.cfg.h>
#include <reactor/reactor.fwd.h>

typedef void (*Mel_Reactor_Init_Fn)   (Mel_Reactor reactor, void* user);
typedef void (*Mel_Reactor_Event_Fn)  (Mel_Reactor reactor, void* user, void* message);
typedef void (*Mel_Reactor_Update_Fn) (Mel_Reactor reactor, void* user);
typedef void (*Mel_Reactor_Destroy_Fn)(Mel_Reactor reactor, void* user);

bool mel_reactor_init(void);
void mel_reactor_shutdown(void);

Mel_Reactor mel_reactor_system(void);
Mel_Reactor mel_reactor_create(void);
void        mel_reactor_destroy(Mel_Reactor);

Mel_Reactor mel_reactor_current(void);
bool        mel_reactor_is_on  (Mel_Reactor);
void        mel_reactor_assert_on(Mel_Reactor);

void mel_reactor_run (Mel_Reactor);
void mel_reactor_stop(Mel_Reactor);
void mel_reactor_post(Mel_Reactor, void* message);

Mel_Reactor_Listener mel_reactor_on_init   (Mel_Reactor, Mel_Reactor_Init_Fn,    void* user);
Mel_Reactor_Listener mel_reactor_on_event  (Mel_Reactor, Mel_Reactor_Event_Fn,   void* user);
Mel_Reactor_Listener mel_reactor_on_update (Mel_Reactor, Mel_Reactor_Update_Fn,  void* user);
Mel_Reactor_Listener mel_reactor_on_destroy(Mel_Reactor, Mel_Reactor_Destroy_Fn, void* user);
void                 mel_reactor_off       (Mel_Reactor_Listener);
