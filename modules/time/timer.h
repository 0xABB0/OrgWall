#pragma once

#include <stdint.h>

typedef uint32_t Mel_Timer;
typedef uint32_t ms;

typedef ms (*Mel_Timer_Callback)(void *user, Mel_Timer timer, ms interval);

Mel_Timer mel_schedule(ms interval, Mel_Timer_Callback cb, void* user);
