#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "music-companion");
    mel_subsystem(app, "gui");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_depends(app, "boot");
    mel_depends(app, "vat");
    mel_depends(app, "allocator");
    mel_depends(app, "core");
    mel_depends(app, "gui");
    mel_depends(app, "midi");
    mel_depends(app, "tuning");
    mel_depends(app, "musictheory");
    mel_depends(app, "notation");
    mel_depends(app, "collection");
    mel_depends(app, "string");

    mel_manifest(app, "APP_LABEL", "Music Companion");
    mel_manifest(app, "BUNDLE_ID", "orgwall.musiccompanion");
}
