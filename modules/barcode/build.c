#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "barcode");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, ALWAYS, "src/decode/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "image");
    mel_depends(lib, "math");

    Mel_Target* t = mel_add_test(b, "barcode-decode");
    mel_sources(t, ALWAYS, "test/test.decode.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "barcode");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "image");
    mel_depends(t, "math");

    Mel_Target* found = mel_add_test(b, "barcode-foundations");
    mel_sources(found, ALWAYS, "test/test.foundations.c");
    mel_sources(found, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(found, "test");
    mel_depends(found, "barcode");
    mel_depends(found, "core");
    mel_depends(found, "allocator");
    mel_depends(found, "collection");

    Mel_Target* qr = mel_add_test(b, "barcode-qr");
    mel_sources(qr, ALWAYS, "test/test.qr.c");
    mel_sources(qr, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(qr, "test");
    mel_depends(qr, "barcode");
    mel_depends(qr, "core");
    mel_depends(qr, "allocator");
    mel_depends(qr, "collection");

    Mel_Target* scan = mel_add_test(b, "barcode-qr-scan");
    mel_sources(scan, ALWAYS, "test/test.qr_scan.c");
    mel_sources(scan, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(scan, "test");
    mel_depends(scan, "barcode");
    mel_depends(scan, "core");
    mel_depends(scan, "allocator");
    mel_depends(scan, "collection");
    mel_depends(scan, "image");
    mel_depends(scan, "math");
}
