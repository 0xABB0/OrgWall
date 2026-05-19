#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <reactor/reactor.cfg.h>
#include <reactor/reactor.fwd.h>
#include <reactor/lifecycle.h>

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
