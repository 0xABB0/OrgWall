#pragma once

#include <reactor/reactor.fwd.h>


typedef void (*Mel_Reactor_Init_Fn)   (Mel_Reactor reactor, void* user);
typedef void (*Mel_Reactor_Event_Fn)  (Mel_Reactor reactor, void* user, void* message);
typedef void (*Mel_Reactor_Update_Fn) (Mel_Reactor reactor, void* user);
typedef void (*Mel_Reactor_Destroy_Fn)(Mel_Reactor reactor, void* user);

