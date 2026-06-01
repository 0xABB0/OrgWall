#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "hello-gpu");
    mel_subsystem(app, "gui");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_depends(app, "app");
    mel_depends(app, "gpu");
    mel_depends(app, "gui");
    mel_depends(app, "core");
    mel_manifest(app, "APP_LABEL", "Hello GPU");
    mel_manifest(app, "BUNDLE_ID", "orgwall.hellogpu");
}
