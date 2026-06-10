#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "display-gui");
    mel_subsystem(app, "gui");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_sources(app, WHEN(.platforms = MEL_ON(MACOS)), "src/macos/*.m");
    mel_link(app, MEL_PRIVATE, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "Metal");
    mel_depends(app, "boot");
    mel_depends(app, "vat");
    mel_depends(app, "allocator");
    mel_depends(app, "gui");
    mel_depends(app, "display");
    mel_depends(app, "core");
    mel_depends(app, "color");
    mel_depends(app, "paint");

    mel_manifest(app, "APP_LABEL", "Display GUI");
    mel_manifest(app, "BUNDLE_ID", "orgwall.displaygui");
}
