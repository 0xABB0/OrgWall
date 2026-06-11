#include "build.h"

static Mel_Target* codec_lib(Mel_Build* b, const char* name, const char* sources)
{
    Mel_Target* lib = mel_add_library(b, name);
    mel_sources(lib, ALWAYS, sources);
    mel_depends(lib, "compress");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    return lib;
}

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "compress");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/registry.c", "src/oneshot.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "log");

    codec_lib(b, "compress-rle", "src/rle/*.c");

    Mel_Target* deflate = codec_lib(b, "compress-deflate", "src/miniz/*.c");
    mel_depends(deflate, "miniz");

    Mel_Target* lz4 = codec_lib(b, "compress-lz4", "src/lz4/*.c");
    mel_depends(lz4, "lz4");

    Mel_Target* zstd = codec_lib(b, "compress-zstd", "src/zstd/*.c");
    mel_depends(zstd, "zstd");

    Mel_Target* brotli = codec_lib(b, "compress-brotli", "src/brotli/*.c");
    mel_depends(brotli, "brotli");

    Mel_Target* zip = codec_lib(b, "compress-zip", "src/zip/*.c");
    mel_depends(zip, "miniz");
    mel_depends(zip, "collection");
    mel_depends(zip, "string");

    Mel_Target* t = mel_add_test(b, "compress-roundtrip");
    mel_sources(t, ALWAYS, "test/roundtrip_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "compress");
    mel_depends(t, "compress-rle");
    mel_depends(t, "compress-deflate");
    mel_depends(t, "compress-lz4");
    mel_depends(t, "compress-zstd");
    mel_depends(t, "compress-brotli");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "string");
    mel_depends(t, "log");

    Mel_Target* zt = mel_add_test(b, "compress-zip-test");
    mel_sources(zt, ALWAYS, "test/zip_test.c");
    mel_sources(zt, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(zt, "test");
    mel_depends(zt, "compress");
    mel_depends(zt, "compress-zip");
    mel_depends(zt, "core");
    mel_depends(zt, "allocator");
    mel_depends(zt, "collection");
    mel_depends(zt, "string");
    mel_depends(zt, "log");
}
