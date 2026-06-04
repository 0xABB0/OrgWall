#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* tp = mel_add_third_party(b, "alsa");
    mel_unavailable(tp, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS) | MEL_ON(WIN32) | MEL_ON(ANDROID) | MEL_ON(WASM)));
    mel_includes(tp, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "include");
    mel_link(tp, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "-Lthird-party/alsa/lib", "-lasound", "-Wl,--allow-shlib-undefined");
}
