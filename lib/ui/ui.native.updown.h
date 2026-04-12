#pragma once

#include "ui.native.ctrl.h"

typedef void (*Mel_NUpDown_Change_Cb)(f64 value, void* user);

typedef struct {
    Mel_NCtrl base;
    f64 value;
    f64 min;
    f64 max;
    f64 increment;
    Mel_NUpDown_Change_Cb on_change;
    void* user_data;
} Mel_NUpDown;

typedef struct {
    f64 min;
    f64 max;
    f64 value;
    f64 increment;
} Mel_NUpDown_Opt;

void mel_nupdown_init_opt(Mel_NUpDown* updown, Mel_NUpDown_Opt opt);
#define mel_nupdown_init(updown, ...) mel_nupdown_init_opt((updown), (Mel_NUpDown_Opt){__VA_ARGS__})

void mel_nupdown_set_value(Mel_NUpDown* updown, f64 value);
void mel_nupdown_set_range(Mel_NUpDown* updown, f64 min, f64 max);

extern const Mel_NCtrl_VTable* mel__nupdown_vtable(void);
extern void mel__nupdown_set_value_platform(Mel_NUpDown* updown, f64 value);
extern void mel__nupdown_set_range_platform(Mel_NUpDown* updown, f64 min, f64 max);
