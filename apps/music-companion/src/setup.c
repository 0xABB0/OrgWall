#include <core/platform.h>
#include <vat/vat.h>
#include <gui/gui.h>

#include "companion.h"

void mel_app_setup(Mel_Vat* root)
{
    mel_gui_init(root);
    mel_app_register_screen(S8("main"), build_companion, root);
    mel_app_present(S8("main"), NULL);
}
