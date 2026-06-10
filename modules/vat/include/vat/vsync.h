#pragma once

#include <vat/vat.h>

typedef struct Mel_Vat_Vsync Mel_Vat_Vsync;
typedef void (*Mel_Vat_Vsync_Fn)(void* user);

Mel_Vat_Vsync* mel_vat_vsync_open(Mel_Vat* vat, const Mel_Alloc* alloc, Mel_Vat_Vsync_Fn fn, void* user);
void           mel_vat_vsync_close(Mel_Vat_Vsync* vsync);
i64            mel_vat_vsync_interval(const Mel_Vat_Vsync* vsync);
