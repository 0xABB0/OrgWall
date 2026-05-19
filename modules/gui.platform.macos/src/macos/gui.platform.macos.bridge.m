#import <Cocoa/Cocoa.h>

#include <gui.platform.macos/gui.platform.macos.h>
#include <gui/gui.h>
#include <string/str8.h>

extern void mel_gui_app_build_activity(str8 activity_name);

static void mel__macos_setup(void)
{
    if (!mel_gui_init()) return;
    mel_gui_app_build_activity(S8("main"));
}

int main(int argc, const char* argv[])
{
    (void)argc;
    (void)argv;
    mel_gui_macos_dispatch_main(mel__macos_setup);
    return 0;
}
