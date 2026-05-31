#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *t = mel_add_third_party(b, "mpfr");
    mel_configure(t, ".");
    mel_configure_cstd(t, "gnu17");
    mel_link(t, MEL_PUBLIC, ALWAYS, "-lmpfr");
    mel_depends(t, "gmp");
}
