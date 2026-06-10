#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "barcode-gui");
    mel_subsystem(app, "gui");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_depends(app, "boot");
    mel_depends(app, "vat");
    mel_depends(app, "allocator");
    mel_depends(app, "barcode");
    mel_depends(app, "gui");
    mel_depends(app, "core");
    mel_depends(app, "allocator");
    mel_depends(app, "color");
    mel_depends(app, "paint");
    mel_manifest(app, "APP_LABEL", "Barcode GUI");
    mel_manifest(app, "BUNDLE_ID", "orgwall.barcodegui");
}
