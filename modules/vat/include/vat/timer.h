#pragma once

#include <vat/vat.h>

typedef struct Mel_Vat_Timers Mel_Vat_Timers;

Mel_Vat_Timers* mel_vat_timers_open(Mel_Vat* vat, const Mel_Alloc* alloc);
void            mel_vat_timers_close(Mel_Vat_Timers* timers);
void            mel_vat_timers_add(Mel_Vat_Timers* timers, i64 deadline_ns, Mel_Task* task);
usize           mel_vat_timers_pending(const Mel_Vat_Timers* timers);
