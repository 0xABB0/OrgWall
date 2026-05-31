#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *lib = mel_add_library(b, "continuation");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_depends(lib, "core");

    Mel_Target *gen = mel_add_host_tool(b, "continuation-gen");
    mel_sources(gen, ALWAYS, "codegen/continuation_gen.c");
    mel_cflags(gen, MEL_PRIVATE, ALWAYS, "-I/opt/homebrew/opt/llvm/include");
    mel_link(gen, MEL_PRIVATE, ALWAYS, "-L/opt/homebrew/opt/llvm/lib", "-lclang",
             "-Wl,-rpath,/opt/homebrew/opt/llvm/lib");

    Mel_Target *ex = mel_add_executable(b, "continuation-example");
    mel_sources(ex, ALWAYS, "example/app.c");
    mel_includes(ex, MEL_PRIVATE, ALWAYS, "example");
    mel_depends(ex, "continuation");
    mel_depends(ex, "core");
    mel_codegen(ex, "continuation-gen", "ticker.gen.c", "$dir/example/ticker.cont.h", "$out",
                "-DMEL_CONT_CODEGEN", "$cflags", "$hostclang");
}
