# Vendored Slang

Pinned Slang shader compiler for the GPU RHI shader pipeline (design/gpu-slang-shaders.md, U12).

- `SLANG_VERSION.lock` — pinned version, URL, sha256. The version string is part of the bundle /
  pipeline-binary cache key (§6.4/§6.5).
- `fetch.sh` — downloads + sha256-verifies + extracts the trimmed runtime into `dist/`.
- `dist/` — gitignored; `bin/slangc` + `lib/` (LLVM and gfx dylibs trimmed) + `include/`.

Run `tools/build/vendor/slang/fetch.sh` once after checkout (the `mel-slangc` host tool errors
loudly if `dist/bin/slangc` is absent). To bump Slang, update `SLANG_VERSION.lock` (version + URL +
sha256) and re-run `fetch.sh`; record source-level deltas the engine consumes in `MIGRATION.md`.
