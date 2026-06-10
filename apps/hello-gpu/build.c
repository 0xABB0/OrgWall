#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "hello-gpu");
    mel_subsystem(app, "gui");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_cflags(app, MEL_PRIVATE, ALWAYS, "--embed-dir=apps/hello-gpu");
    mel_depends(app, "boot");
    mel_depends(app, "vat");
    mel_depends(app, "gpu");
    mel_depends(app, "gui");
    mel_depends(app, "core");
    mel_depends(app, "log");
    mel_manifest(app, "APP_LABEL", "Hello GPU");
    mel_manifest(app, "BUNDLE_ID", "orgwall.hellogpu");
}
