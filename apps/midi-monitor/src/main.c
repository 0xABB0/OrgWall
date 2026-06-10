#include <core/platform.h>
#include <vat/vat.h>
#include <gui/gui.h>

#include "monitor.h"

void mel_app_setup(Mel_Vat* root)
{
    mel_gui_init(root);
    mel_app_register_screen(S8("main"), build_monitor, root);
    mel_app_present(S8("main"), NULL);
}
