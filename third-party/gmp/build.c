#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *t = mel_add_third_party(b, "gmp");
    mel_includes(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "/opt/homebrew/include");
    mel_link(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-L/opt/homebrew/lib", "-lgmp");
}
