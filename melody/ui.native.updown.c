#include "ui.native.updown.h"

void mel_nupdown_init_opt(Mel_NUpDown* updown, Mel_NUpDown_Opt opt)
{
    assert(updown != nullptr);

    mel_nctrl_init(&updown->base, mel__nupdown_vtable());

    updown->min       = opt.max != 0.0 ? opt.min : 0.0;
    updown->max       = opt.max != 0.0 ? opt.max : 100.0;
    updown->increment = opt.increment != 0.0 ? opt.increment : 1.0;
    updown->value     = opt.value;
    updown->on_change = nullptr;
    updown->user_data = nullptr;

    mel_nctrl_create_backing(&updown->base);
}

void mel_nupdown_set_value(Mel_NUpDown* updown, f64 value)
{
    assert(updown != nullptr);
    updown->value = value;
    if (updown->base.backing)
        mel__nupdown_set_value_platform(updown, value);
}

void mel_nupdown_set_range(Mel_NUpDown* updown, f64 min, f64 max)
{
    assert(updown != nullptr);
    updown->min = min;
    updown->max = max;
    if (updown->base.backing)
        mel__nupdown_set_range_platform(updown, min, max);
}
