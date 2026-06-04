#pragma once

#include <reactor/reactor.h>

typedef void (*Showcase_Line_Fn)(const char* module, const char* fmt, ...);

int  showcase_smoke(void);
void showcase_window_setup(Mel_Reactor* reactor);
