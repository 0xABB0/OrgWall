#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "midi-monitor");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_depends(app, "boot");
    mel_depends(app, "vat");
    mel_depends(app, "allocator");
    mel_depends(app, "gui");
    mel_depends(app, "midi");
    mel_depends(app, "core");
    mel_manifest(app, "APP_LABEL", "MIDI Monitor");
    mel_manifest(app, "BUNDLE_ID", "orgwall.midimonitor");
}
