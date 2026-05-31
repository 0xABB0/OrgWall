#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "hello-window");
    mel_subsystem(app, "gui");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_depends(app, "window");
    mel_depends(app, "reactor");
    mel_depends(app, "string");
    mel_depends(app, "core");

    mel_manifest(app, "APP_LABEL", "Hello Window");
    mel_manifest(app, "BUNDLE_ID", "orgwall.hellowindow");
}
