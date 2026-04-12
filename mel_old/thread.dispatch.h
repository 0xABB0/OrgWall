#pragma once

#include "core.types.h"

typedef void* (*Mel_Main_Dispatch_Fn)(void*);

void  mel__main_dispatch_init(void);
void  mel__main_dispatch_shutdown(void);
void  mel__main_dispatch_drain(void);
bool  mel__is_main_thread(void);
void* mel__main_dispatch_sync(Mel_Main_Dispatch_Fn fn, void* data);
