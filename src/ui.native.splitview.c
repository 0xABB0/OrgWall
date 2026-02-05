#include "ui.native.splitview.h"

extern const Mel_NCtrl_VTable* mel__nsplitview_vtable_osx(void);
extern void mel__nsplitview_set_divider_platform(Mel_NSplitView* sv, f32 position);

void mel_nsplitview_init_opt(Mel_NSplitView* sv, Mel_NSplitView_Opt opt)
{
    assert(sv != nullptr);

    mel_nctrl_init(&sv->base, mel__nsplitview_vtable_osx());

    sv->orientation      = opt.orientation;
    sv->divider_position = opt.divider_position > 0.0f ? opt.divider_position : 0.5f;
}

void mel_nsplitview_set_divider_position(Mel_NSplitView* sv, f32 position)
{
    assert(sv != nullptr);
    sv->divider_position = position;
    if (sv->base.backing)
        mel__nsplitview_set_divider_platform(sv, position);
}
