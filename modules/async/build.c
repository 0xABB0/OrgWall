#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "async");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS), .arch = "arm64"), "src/asm/*_arm64_aapcs_macho_gas.S");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS), .arch = "x86_64"), "src/asm/*_x86_64_sysv_macho_gas.S");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX) | MEL_ON(ANDROID), .arch = "arm64"), "src/asm/*_arm64_aapcs_elf_gas.S");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX), .arch = "x86_64"), "src/asm/*_x86_64_sysv_elf_gas.S");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32), .arch = "x86_64"), "src/asm/*_x86_64_ms_pe_clang_gas.S");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "thread");
}
