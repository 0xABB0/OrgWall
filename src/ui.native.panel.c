#include "ui.native.panel.h"

extern const Mel_NCtrl_VTable* mel__npanel_vtable_osx(void);

void mel_npanel_init_opt(Mel_NPanel* panel, Mel_NPanel_Opt opt)
{
    assert(panel != nullptr);

    mel_nctrl_init(&panel->base, mel__npanel_vtable_osx());

    f32 w = opt.width  > 0 ? opt.width  : 0.0f;
    f32 h = opt.height > 0 ? opt.height : 0.0f;
    panel->base.size = mel_vec2(w, h);
}
