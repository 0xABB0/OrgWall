#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *lib = mel_add_library(b, "sqlite3");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "sqlite");
    mel_sources(lib, ALWAYS, "sqlite/sqlite3.c");
}
