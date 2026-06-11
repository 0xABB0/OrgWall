#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "http");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/url.c");
    mel_sources(lib, ALWAYS, "src/wire.c");
    mel_sources(lib, ALWAYS, "src/fetch.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "executor");
    mel_depends(lib, "future");
    mel_depends(lib, "vat");
    mel_depends(lib, "port");
    mel_depends(lib, "io");
    mel_depends(lib, "net");
    mel_depends(lib, "time");
    mel_depends(lib, "log");

    Mel_Target* t = mel_add_test(b, "http-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "test/test_http.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "http");
    mel_depends(t, "net");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "string");
    mel_depends(t, "executor");
    mel_depends(t, "future");
    mel_depends(t, "vat");
    mel_depends(t, "port");
    mel_depends(t, "io");
    mel_depends(t, "thread");
    mel_depends(t, "time");
    mel_depends(t, "log");
}
