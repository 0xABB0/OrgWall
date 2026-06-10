#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "hello-vibration");
    mel_subsystem(app, "gui");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_depends(app, "boot");
    mel_depends(app, "vat");
    mel_depends(app, "gui");
    mel_depends(app, "vibration");
    mel_depends(app, "string");
    mel_depends(app, "log");
    mel_depends(app, "core");

    mel_manifest(app, "APP_LABEL", "Vibration");
    mel_manifest(app, "BUNDLE_ID", "orgwall.vibration");
}
