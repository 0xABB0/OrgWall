#include "ui.native.popup.h"

extern const Mel_NCtrl_VTable* mel__npopup_vtable_osx(void);
extern void mel__npopup_show_relative_platform(Mel_NPopup* popup, Mel_NCtrl* anchor);
extern void mel__npopup_close_platform(Mel_NPopup* popup);

void mel_npopup_init_opt(Mel_NPopup* popup, Mel_NPopup_Opt opt)
{
    assert(popup != nullptr);

    mel_nctrl_init(&popup->base, mel__npopup_vtable_osx());

    popup->side = opt.side;

    f32 w = opt.width  > 0.0f ? opt.width  : 200.0f;
    f32 h = opt.height > 0.0f ? opt.height : 150.0f;
    popup->base.size = mel_vec2(w, h);
}

void mel_npopup_show_relative_to(Mel_NPopup* popup, Mel_NCtrl* anchor)
{
    assert(popup != nullptr);
    assert(anchor != nullptr);
    mel__npopup_show_relative_platform(popup, anchor);
}

void mel_npopup_close(Mel_NPopup* popup)
{
    assert(popup != nullptr);
    mel__npopup_close_platform(popup);
}
