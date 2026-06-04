# JIT — vendoring LLVM + Clang (all-dynamic, first-class)

Goal: a single shared LLVM linked dynamically by **both** the `llvm` module (ORC) and the `clang` module (incl. `clang::Interpreter`), so there is exactly one LLVM image with one `TargetRegistry`. This is what makes `clang::Interpreter` usable: its JIT and ours register/look up targets in the same registry.

## Why all-dynamic (the walls that forced it)
- **Static link (official LLVM release archives)** crashes at startup: the prebuilt is clang21/libc++21 with typed-new (TMO); its global constructors run inside our Apple-clang-17 binary before libc++abi's initializer → `libc++abi: typed operator new ... before its static initializer`. We cannot recompile the prebuilt archives.
- **Static LLVM + dynamic `libclang-cpp` (embedding its own LLVM)** gives *two* LLVMs with *two* `TargetRegistry`s. `InitializeNativeTarget()` populates ours; `clang::Interpreter` reads libclang-cpp's (empty) → "Unable to find target for this triple". libclang-cpp doesn't export the C target-init symbols, so the second registry can't be populated.
- **All-dynamic against a shared `libLLVM`** dissolves both: one image, one registry, C API exported, and dynamic global-ctor ordering is self-consistent at load (no TMO hazard).

## macOS source
Homebrew LLVM (`brew install llvm`, currently 22.1.6): `lib/libLLVM.dylib` (exports the full C API — `LLVMInitialize*Target*`, `LLVMOrc*`) and `lib/libclang-cpp.dylib`, where `libclang-cpp.dylib` links `@rpath/libLLVM.dylib` (one LLVM copy). `third-party/llvm/build.c`:
```c
mel_includes(rt, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "/opt/homebrew/opt/llvm/include");
mel_link(rt, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)),
         "-L/opt/homebrew/opt/llvm/lib", "-Wl,-rpath,/opt/homebrew/opt/llvm/lib", "-lLLVM", "-lclang-cpp");
```

## Cross-platform (follow-up)
- **Linux**: distro `libLLVM-NN.so` + `libclang-cpp.so` (same shape; libclang-cpp links libLLVM). Discover via `llvm-config --libdir`/`--includedir`.
- **Windows**: official LLVM installer ships `LLVM-C.dll` + `libclang`; `clang::Interpreter` via `libclang-cpp` on Windows is less proven — validate before promising the REPL there (the JIT/`mel_clang_compile` paths are fine via the C API). This is the platform Gabbo flagged; treat as its own task.
- **Path discovery**: the macOS prefix is currently pinned. Replace with `llvm-config`/`brew --prefix llvm` probing at configure (the build framework needs a "run a command, capture flags" hook — currently absent; see the zstd note in the original design).

## Notes
- No download/from-source step (was a 7.8 GB static prebuilt; now a system dynamic lib). Reproducibility now depends on the system LLVM version; pin/validate the major version (headers vs lib).
- The dual-LLVM IR→text→IR roundtrip in `mel_clang_compile` is no longer needed for ABI safety (one LLVM now) and can be dropped to hand modules to ORC directly — a follow-up optimization.
