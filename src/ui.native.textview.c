#include "ui.native.textview.h"

extern const Mel_NCtrl_VTable* mel__ntextview_vtable_osx(void);
extern void mel__ntextview_set_text_platform(Mel_NTextView* tv, const char* text);
extern const char* mel__ntextview_get_text_platform(Mel_NTextView* tv);
extern void mel__ntextview_set_editable_platform(Mel_NTextView* tv, bool editable);

void mel_ntextview_init_opt(Mel_NTextView* tv, Mel_NTextView_Opt opt)
{
    assert(tv != nullptr);

    mel_nctrl_init(&tv->base, mel__ntextview_vtable_osx());

    tv->editable  = true;
    tv->on_change = nullptr;
    tv->user_data = nullptr;
}

void mel_ntextview_set_text(Mel_NTextView* tv, const char* text)
{
    assert(tv != nullptr);
    if (tv->base.backing)
        mel__ntextview_set_text_platform(tv, text);
}

const char* mel_ntextview_get_text(Mel_NTextView* tv)
{
    assert(tv != nullptr);
    if (tv->base.backing)
        return mel__ntextview_get_text_platform(tv);
    return "";
}

void mel_ntextview_set_editable(Mel_NTextView* tv, bool editable)
{
    assert(tv != nullptr);
    tv->editable = editable;
    if (tv->base.backing)
        mel__ntextview_set_editable_platform(tv, editable);
}
