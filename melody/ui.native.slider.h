#pragma once

#include "ui.native.ctrl.h"

typedef void (*Mel_NSlider_Change_Cb)(f64 value, void* user);

typedef struct {
    Mel_NCtrl base;
    f64 value;
    f64 min;
    f64 max;
    Mel_NSlider_Change_Cb on_change;
    void* user_data;
} Mel_NSlider;

typedef struct {
    f64 min;
    f64 max;
    f64 value;
} Mel_NSlider_Opt;

void mel_nslider_init_opt(Mel_NSlider* slider, Mel_NSlider_Opt opt);
#define mel_nslider_init(slider, ...) mel_nslider_init_opt((slider), (Mel_NSlider_Opt){__VA_ARGS__})

void mel_nslider_set_value(Mel_NSlider* slider, f64 value);
void mel_nslider_set_range(Mel_NSlider* slider, f64 min, f64 max);

extern const Mel_NCtrl_VTable* mel__nslider_vtable(void);
extern void mel__nslider_set_value_platform(Mel_NSlider* slider, f64 value);
extern void mel__nslider_set_range_platform(Mel_NSlider* slider, f64 min, f64 max);
