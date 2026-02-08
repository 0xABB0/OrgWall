#include "ui.native.slider.h"

void mel_nslider_init_opt(Mel_NSlider* slider, Mel_NSlider_Opt opt)
{
    assert(slider != nullptr);

    mel_nctrl_init(&slider->base, mel__nslider_vtable());

    slider->min       = opt.max != 0.0 ? opt.min : 0.0;
    slider->max       = opt.max != 0.0 ? opt.max : 100.0;
    slider->value     = opt.value;
    slider->on_change = nullptr;
    slider->user_data = nullptr;

    mel_nctrl_create_backing(&slider->base);
}

void mel_nslider_set_value(Mel_NSlider* slider, f64 value)
{
    assert(slider != nullptr);
    slider->value = value;
    if (slider->base.backing)
        mel__nslider_set_value_platform(slider, value);
}

void mel_nslider_set_range(Mel_NSlider* slider, f64 min, f64 max)
{
    assert(slider != nullptr);
    slider->min = min;
    slider->max = max;
    if (slider->base.backing)
        mel__nslider_set_range_platform(slider, min, max);
}
