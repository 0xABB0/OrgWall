#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "hello-world-gui");
    mel_subsystem(app, "gui");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_depends(app, "boot");
    mel_depends(app, "vat");
    mel_depends(app, "allocator");
    mel_depends(app, "gui");
    mel_depends(app, "core");
    mel_depends(app, "color");
    mel_depends(app, "paint");

    mel_manifest(app, "APP_LABEL", "Hello World GUI");
    mel_manifest(app, "BUNDLE_ID", "orgwall.helloworld");
}
