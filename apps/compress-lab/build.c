#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "compress-lab");
    mel_subsystem(app, "gui");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_includes(app, MEL_PRIVATE, ALWAYS, "src");
    mel_depends(app, "boot");
    mel_depends(app, "vat");
    mel_depends(app, "core");
    mel_depends(app, "allocator");
    mel_depends(app, "collection");
    mel_depends(app, "string");
    mel_depends(app, "log");
    mel_depends(app, "time");
    mel_depends(app, "executor");
    mel_depends(app, "future");
    mel_depends(app, "io");
    mel_depends(app, "dialog");
    mel_depends(app, "gui");
    mel_depends(app, "coro");
    mel_depends(app, "compress");
    mel_depends(app, "compress-rle");
    mel_depends(app, "compress-deflate");
    mel_depends(app, "compress-lz4");
    mel_depends(app, "compress-zstd");
    mel_depends(app, "compress-brotli");
    mel_depends(app, "compress-zip");

    mel_link(app, MEL_PRIVATE, WHEN(.platforms = MEL_ON(WASM)), "-sALLOW_MEMORY_GROWTH=1");

    mel_codegen(app, "coro-gen", "job.gen.c", "$dir/src/job.coro.h", "$out", "-DMEL_CORO_CODEGEN", "$cflags", "$hostclang");
    mel_codegen_input(app, "$dir/src/job.coro.h");
    mel_codegen_depfile(app);

    mel_manifest(app, "APP_LABEL", "Compress Lab");
    mel_manifest(app, "BUNDLE_ID", "orgwall.compresslab");
}
