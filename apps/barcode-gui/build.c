#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *app = mel_add_executable(b, "barcode-gui");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_depends(app, "app");
    mel_depends(app, "barcode");
    mel_depends(app, "gui");
    mel_depends(app, "core");
    mel_depends(app, "allocator");
    mel_manifest(app, "APP_LABEL", "Barcode GUI");
    mel_manifest(app, "BUNDLE_ID", "orgwall.barcodegui");
}
