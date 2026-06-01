#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "reflect");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_depends(lib, "string");

    Mel_Target* gen = mel_add_host_tool(b, "enum-str-gen");
    mel_sources(gen, ALWAYS, "codegen/enum_str_gen.c");
    mel_cflags(gen, MEL_PRIVATE, ALWAYS, "-I/opt/homebrew/opt/llvm/include");
    mel_link(gen, MEL_PRIVATE, ALWAYS, "-L/opt/homebrew/opt/llvm/lib", "-lclang", "-Wl,-rpath,/opt/homebrew/opt/llvm/lib");
}
