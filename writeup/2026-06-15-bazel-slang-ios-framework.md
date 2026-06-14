# 2026-06-15 — Bazel migration: slang iOS dynamic-framework embed (D2)

## Work done — what changed, and why

Closed the iOS column to **16/16** by landing **Track D2** — embedding the slang
shader-compiler dylib into the `.ipa` as a dynamic framework. This was the single
remaining blocker for the last two iOS GPU apps (`ballgame`, `hello-gpu`); the prior
session proved compile+link clean but the apps crashed at launch because the dylib was
not bundled.

### Root cause (re-confirmed)

rules_apple 4.5.3 auto-embeds bare `CcInfo` `.dylib`s into `Frameworks/` for
`macos_application` (the `cc_info_dylibs_partial` is wired into `macos_rules.bzl`) but the
equivalent is **not** wired into `ios_rules.bzl`. So the macOS path embeds slang for free;
iOS does not. `AppleFrameworkImportInfo` does not propagate through `cc_library` deps, so a
framework import cannot ride the `gpu`→`slang` cc graph — it must attach at the
`ios_application` level directly.

### Facts established before authoring (not assumed)

- The vendored `tools/build/vendor/slang/slang-2026.10.2-ios-sim-aarch64.zip` ships a single
  **self-contained** `libslang-compiler.dylib` (`otool -L`: only `libc++`/`libSystem` — no
  sibling glslang/glsl-module/llvm plugin dylibs), arm64, platform 7 (iOS-Simulator),
  minos 14.0. So one framework binary suffices; no plugin co-location needed (the macOS D2
  `MH_BUNDLE` problem does not arise here).
- The zip **is git-tracked** (`tools/build/vendor/slang/.gitignore` only excludes `dist/`),
  so a `genrule` consuming it is portable across checkouts/CI — no repo rule, no `file://`.
- `third-party/slang/src/compile.cpp` **direct-links** `slang::createGlobalSession`
  (`#include <slang.h>`, flat include) — a hard link, so the dylib must be present at launch.
- Only `ballgame` and `hello-gpu` pull `//modules/gpu`→slang; `gui` does **not**, so making
  iOS `slang-runtime` headers-only cannot regress the other 14 green iOS apps.

### Implementation

- **`tools/build/vendor/slang/BUILD.bazel`** (new) — `exports_files` the ios-sim zip.
- **`third-party/slang/BUILD.bazel`** — one `genrule` (`ios_framework`, gated
  `target_compatible_with @platforms//os:ios`) unzips the vendored archive and emits:
  `slang.framework/slang` (the dylib, `install_name_tool -id @rpath/slang.framework/slang`),
  `slang.framework/Info.plist` (from a checked-in template — see below), and the 5 slang
  headers under `ios_include/`. Added `slang_ios_headers` (headers-only cc_library, wired
  into the `slang-runtime` select's new `plat_ios` arm — satisfies `<slang.h>` on the compile
  path) and `slang_ios_framework` (`apple_dynamic_framework_import` over the two framework
  files — provides link + embed). Added `plat_ios: ["-lc++"]` to the `slang` lib's linkopts.
- **`third-party/slang/ios/slang_framework_info.plist`** (new) — a checked-in `FMWK` plist
  the genrule copies to `slang.framework/Info.plist`. (Chosen over a heredoc so both framework
  files land at the same bin-dir `slang.framework/` path — `apple_dynamic_framework_import`
  requires all `framework_imports` under exactly one `.framework` dir.)
- **`apps/{ballgame,hello-gpu}/BUILD.bazel`** — an `ios_application` per app mirroring the
  existing GUI shape, with `deps = [":lib", "//third-party/slang:slang_ios_framework"]` (the
  framework import attached at app level). **`apps/{ballgame,hello-gpu}/ios/Info.plist`** (new),
  the programmatic-UIKit shape already used by the 14 GUI apps.

### Verified — runtime, on `sim_arm64`

- Both `.ipa`s build (`bazel build //apps/{ballgame,hello-gpu}:*_ios --config=ios
  --ios_multi_cpus=sim_arm64`). `ImportedDynamicFrameworkProcessor` runs on `slang.framework`.
- `.ipa` inspection: `Payload/<app>.app/Frameworks/slang.framework/slang` present (23.8 MB);
  app binary `LC_LOAD_DYLIB`→`@rpath/slang.framework/slang`; `LC_RPATH`→
  `@executable_path/Frameworks`; framework re-signed (`_CodeSignature`, `codesign -v` clean).
- Booted an iPhone-16 / iOS-18.6 simulator, installed + launched both. **No dyld crash** (the
  prior failure mode). `ballgame` logs `metal device created` → `metal swapchain ready:
  1179x2160` and **renders its scene** (glowing orange ball — a fragment-shader effect, i.e.
  slang compiled the shader at runtime via `createGlobalSession`; had the dylib been absent it
  would have crashed there). `hello-gpu` launches and its launcher GUI renders (its per-demo
  GPU views are tap-gated and `simctl` has no tap primitive — the slang path is the same code
  proven by ballgame).
- **No macOS regression**: `//apps/{ballgame,hello-gpu}:*_app`, `//third-party/slang:slang`,
  `//modules/gpu` build all-cache-hits (the macOS slang path is byte-identical);
  `//third-party/slang:slang-compile` PASSED (cached).

iOS column: **16/16** GUI/GPU apps build + run on `sim_arm64`. `hello-window` builds+launches
blank by design (desktop windowing stubbed on mobile).

## Kludges & debt (MEL-ENGINE-VIII — confessing all)

- **Simulator slice only.** The vendored zip is the ios-**sim** arm64 slice (platform 7); there
  is no device (`ios-arm64`) slang artifact vendored, so `ballgame_ios`/`hello-gpu_ios` build
  for the simulator only. A device build needs the device slice + an iphoneos genrule arm
  (and real signing). Not done — verification target is the simulator, matching the rest of the
  iOS column. Honest gap, not a silent one (the `ios_framework` genrule names the sim zip
  explicitly).
- **slang headers vendored twice on iOS.** `slang_ios_headers` extracts `slang.h` et al. from
  the ios-sim zip rather than reusing `@slang_macos`'s identical headers — deliberate, to avoid
  coupling the iOS compile path to fetching the macOS http_archive, but it is duplication.
- **Metal bindless path warns on iOS 14.4 deployment.** `modules/gpu/metal/src/macos/binding.m`
  uses `gpuAddress`/`gpuResourceID` (iOS 16+) under a 14.4 deployment target →
  `-Wunguarded-availability-new`. Pre-existing (not D2); harmless on the modern simulator but a
  latent runtime trap on a real <16 device. Flagging, not fixing here.
- **Framework Info.plist is hand-minimal.** `CFBundleExecutable`/`Identifier`/`Package
  Type=FMWK` + version; enough for embedding + re-sign on the simulator. Not asserted sufficient
  for App Store submission metadata.

## CLAUDE.md suggestions (recommendations only — not applied)

- Record the iOS framework-embed idiom for future vendored dylibs: a `genrule` →
  `slang.framework/{binary,Info.plist}` + `apple_dynamic_framework_import` attached at the
  `*_application` level (NOT via cc deps — `AppleFrameworkImportInfo` does not propagate). The
  same shape will serve any future iOS-embedded prebuilt.

## Suggestions

- **Same rule shape unblocks android/wasm slang** (D1-tail): the vendored android-arm64 and
  wasm-mt zips sit beside the ios one in `tools/build/vendor/slang/`. Android needs the dylib
  packaged into the apk's `lib/<abi>/` (or a `cc_import` + jniLibs), wasm needs the `-mt` static
  archive wired with `-sASYNCIFY=2`. Both are now "extract-the-vendored-zip" tasks, not authoring
  from scratch.
- **wasm column remains an engine-backend effort** (display has no wasm axis), unchanged by this
  session — see the prior iOS writeup's A2 re-scoping.
- Next host-verifiable bounded tasks toward nob deletion: **E1** (win32 macOS-exec
  case-sensitivity relaxation — unblocks the whole win32 column from this Mac) and **A5** debt
  (macOS Vulkan pin, musictheory/musicnotation private-include leak).
