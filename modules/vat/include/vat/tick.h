#pragma once

#include <vat/vat.h>

typedef struct Mel_Vat_Tick Mel_Vat_Tick;
typedef bool (*Mel_Vat_Tick_Fn)(void* user);

Mel_Vat_Tick* mel_vat_tick_open(Mel_Vat* vat, const Mel_Alloc* alloc, i64 interval_ns, Mel_Vat_Tick_Fn fn, void* user);
void          mel_vat_tick_close(Mel_Vat_Tick* tick);
void          mel_vat_tick_set_interval(Mel_Vat_Tick* tick, i64 interval_ns);
void          mel_vat_tick_pause(Mel_Vat_Tick* tick);
