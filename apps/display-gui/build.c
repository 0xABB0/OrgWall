#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "display-gui");
    mel_subsystem(app, "gui");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_sources(app, WHEN(.platforms = MEL_ON(MACOS)), "src/macos/*.m");
    mel_link(app, MEL_PRIVATE, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "Metal");
    mel_depends(app, "app");
    mel_depends(app, "gui");
    mel_depends(app, "display");
    mel_depends(app, "core");

    mel_manifest(app, "APP_LABEL", "Display GUI");
    mel_manifest(app, "BUNDLE_ID", "orgwall.displaygui");
}
