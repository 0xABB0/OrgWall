#pragma once

#include "ui.native.ctrl.h"

typedef struct {
    Mel_NCtrl base;
    f64 value;
    f64 min;
    f64 max;
    bool indeterminate;
} Mel_NProgress;

typedef struct {
    f64 min;
    f64 max;
    f64 value;
    bool indeterminate;
} Mel_NProgress_Opt;

void mel_nprogress_init_opt(Mel_NProgress* progress, Mel_NProgress_Opt opt);
#define mel_nprogress_init(progress, ...) mel_nprogress_init_opt((progress), (Mel_NProgress_Opt){__VA_ARGS__})

void mel_nprogress_set_value(Mel_NProgress* progress, f64 value);
void mel_nprogress_set_indeterminate(Mel_NProgress* progress, bool indeterminate);

extern const Mel_NCtrl_VTable* mel__nprogress_vtable(void);
extern void mel__nprogress_set_value_platform(Mel_NProgress* progress, f64 value);
extern void mel__nprogress_set_indeterminate_platform(Mel_NProgress* progress, bool indeterminate);
