#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *t = mel_add_third_party(b, "gmp");
    mel_configure(t, ".", "--disable-assembly");
    mel_link(t, MEL_PUBLIC, ALWAYS, "-lgmp");
}
