#include "ui.native.progress.h"

void mel_nprogress_init_opt(Mel_NProgress* progress, Mel_NProgress_Opt opt)
{
    assert(progress != nullptr);

    mel_nctrl_init(&progress->base, mel__nprogress_vtable());

    progress->min           = opt.max != 0.0 ? opt.min : 0.0;
    progress->max           = opt.max != 0.0 ? opt.max : 100.0;
    progress->value         = opt.value;
    progress->indeterminate = opt.indeterminate;

    mel_nctrl_create_backing(&progress->base);
}

void mel_nprogress_set_value(Mel_NProgress* progress, f64 value)
{
    assert(progress != nullptr);
    progress->value = value;
    if (progress->base.backing)
        mel__nprogress_set_value_platform(progress, value);
}

void mel_nprogress_set_indeterminate(Mel_NProgress* progress, bool indeterminate)
{
    assert(progress != nullptr);
    progress->indeterminate = indeterminate;
    if (progress->base.backing)
        mel__nprogress_set_indeterminate_platform(progress, indeterminate);
}
