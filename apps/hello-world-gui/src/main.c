#include <core/platform.h>
#include <app/app.h>
#include <gui/gui.h>

#include "mainscreen.h"
#include "detailscreen.h"
#include "replacescreen.h"


void mel_app_setup(Mel_Reactor* reactor)
{
    mel_gui_init(reactor);
    mel_app_register_screen(S8("main"),     build_main,    NULL);
    mel_app_register_screen(S8("details"),  build_details, NULL);
    mel_app_register_screen(S8("replaced"), build_replace, NULL);
    mel_app_present(S8("main"));
}
