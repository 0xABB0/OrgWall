#include <core/platform.h>
#include <app/app.h>
#include <gui/gui.h>

#include "monitor.h"

void mel_app_setup(Mel_Reactor* reactor)
{
    mel_gui_init(reactor);
    mel_app_register_screen(S8("main"), build_monitor, reactor);
    mel_app_present(S8("main"));
}
