#pragma once

#include <reactor/reactor.fwd.h>


typedef void (*Mel_Reactor_Init_Fn)   (Mel_Reactor reactor, void* user);
typedef void (*Mel_Reactor_Event_Fn)  (Mel_Reactor reactor, void* user, void* message);
typedef void (*Mel_Reactor_Update_Fn) (Mel_Reactor reactor, void* user);
typedef void (*Mel_Reactor_Destroy_Fn)(Mel_Reactor reactor, void* user);

Mel_Reactor_Listener mel_reactor_on_init   (Mel_Reactor, Mel_Reactor_Init_Fn,    void* user);
Mel_Reactor_Listener mel_reactor_on_event  (Mel_Reactor, Mel_Reactor_Event_Fn,   void* user);
Mel_Reactor_Listener mel_reactor_on_update (Mel_Reactor, Mel_Reactor_Update_Fn,  void* user);
Mel_Reactor_Listener mel_reactor_on_destroy(Mel_Reactor, Mel_Reactor_Destroy_Fn, void* user);
void                 mel_reactor_off       (Mel_Reactor_Listener);
