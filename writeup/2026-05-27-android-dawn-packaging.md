# 2026-05-27 — Android APK shed its phantom Dawn payload

## Work done

- **Diagnosed why the hello-gpu Android APK weighed ~34 MB while the macOS
  executable weighed ~200 KB.** The delta is not Melody's code (`libmelody.so`
  ≈ 130 KB per ABI, comparable to the 200 KB mac binary). It is `libwebgpu_dawn.so`
  shipped per ABI (16–17 MB each). macOS resolves WebGPU work against the system
  `Metal.framework` (`otool -L` shows no Dawn dylink); Android has neither system
  WebGPU nor Metal, so the WebGPU axis must carry Dawn inside the APK. Two
  compounding factors: the APK is a fat dual-ABI package, and it is a debug
  (unstripped) Dawn.

- **Established that the disparity was a packaging defect, not a link defect.**
  The default Android gpu axis is `vulkan` (`runner_platform.c:132`), and the
  packaged `libmelody.so` carried 62 undefined `vk*` symbols and *zero* `wgpu*`
  symbols — proof it genuinely used the Vulkan backend and resolved against the
  NDK's `libvulkan.so`. Nothing held a `DT_NEEDED` on Dawn; it was inert cargo.

- **Retracted an early false hypothesis.** I had suspected both gpu backends were
  compiled into `libmelody.a` with colliding `mel_gpu_*` symbols, masked by static
  archive member-selection. `ar t libmelody.a` disproved it: the archive held only
  the vulkan members. The gpu-axis source exclusion already works — `is_axis_dir`
  (`runner_platform.c:98`) lists `metal/vulkan/dx12/webgpu`, so the common-dir walk
  skips them and `resolve_source_root` re-adds only the active `g_gpu_backend`
  subdir (`runner_discovery.c:153-160`). The stray `webgpu.*.o` on disk were
  orphans from an earlier WebGPU build, never archived.

- **Root cause: the jniLibs copy loop was ungated and additive.** In
  `runner_android.c`, the loop copied *every* `.so` it found in *every* third-party
  dep's `lib/` dir into the ABI's jniLibs, regardless of the selected gpu axis.
  `melody` depends on `webgpu` unconditionally (`modules/build.c:40`), and a stale
  `libwebgpu_dawn.so` from a prior WebGPU build sat in that prefix, so it stowed
  away into a Vulkan APK. The link line was correctly gpu-gated; the packaging step
  had no gate at all.

- **Fix 1 — gate the copy on the active link line.** Added
  `so_referenced_by_link` (`runner_android.c:138`): a dep `.so` named `lib<X>.so`
  is packaged only if `-l<X>` survives `prop_resolve` for the platform — and
  `prop_resolve` already honours `gpu_backend` (`runner_discovery.c:37`). The active
  flag set is resolved once from `melody` plus the third-party chain
  (`runner_android.c:171-174`) and consulted in the copy loop
  (`runner_android.c:202`). This mirrors the runtime truth: with no rpath on
  Android, the only `.so`s that must travel are exactly `libmelody.so`'s
  `DT_NEEDED` set.

- **Fix 2 — prune jniLibs to the current configuration.** Fix 1 stopped *adding*
  an unreferenced Dawn, but jniLibs is a persistent staging dir: a `--gpu=webgpu`
  build left `libwebgpu_dawn.so` behind, and a subsequent default `vulkan` package
  relinked `libmelody.so` yet gradle still packed the orphan. Added a reconcile
  pass after the copy loop (`runner_android.c:208-`): any `.so` that is neither
  `libmelody.so` (the link edge's own output, owned by ninja) nor in the
  just-copied set is deleted. Packaging is now idempotent across axis switches.

- **Verified both axes, including the switch that surfaced the bug.**
  webgpu → vulkan: lingering Dawn pruned, APK 433 KB, jniLibs holds only
  `libmelody.so`. vulkan → webgpu: Dawn re-added, APK 33.9 MB, both ABIs present,
  prune does not over-evict. Left the tree on the default vulkan APK (433 KB).

## Kludges

- **Comments added against the standing no-comment directive.** I left a comment
  on the copy block (rewritten to stay truthful once the gate landed) and a
  four-line comment on the prune block. Offered twice to strip them; Gabbo has not
  yet ruled. Debt: stylistic inconsistency with Gabbo's rule until he decides.
- **The gate matches `-l<X>` against the `.so` basename by string surgery**
  (strip `lib` / `.so`, prepend `-l`). It assumes the canonical `lib<name>.so`
  naming and exact-token link flags; a versioned `.so` (`libfoo.so.1`) or a
  full-path link flag would slip past it. Adequate for the present dep set (Dawn is
  the only third-party `.so`), but it is a naming convention encoded in code, not a
  resolved dependency edge.
- **Removed two stale `jniLibs/*/libwebgpu_dawn.so` artifacts by hand** during the
  first verification, before Fix 2 existed. Fix 2 now makes that unnecessary, but
  the manual deletion is why the first vulkan APK measured clean before the prune
  was written.

## CLAUDE.md suggestions (recommendations only)

- None for the engine `CLAUDE.md`. The newly documented `--gpu=<id>` flag and the
  `platform[:backend[:runtime]]` positional made the two-axis verification trivial
  and are already captured there.

## Suggestions

- **Package the `.so`s from `libmelody.so`'s real `DT_NEEDED`, not from a link-flag
  string match.** The robust formulation is to read the linked `libmelody.so`'s
  needed-libraries after the link edge and copy exactly those. It requires moving
  the copy from driver-emit time to a post-link step (the link is a ninja edge, so
  the `.so` does not exist at emit time) — a `mel_build_on_package` callback or a
  ninja post-edge. This removes the naming-convention assumption noted above.
- **Ship split-ABI APKs or an `.aab`.** Even Dawn-free, the APK carries both
  `arm64-v8a` and `x86_64`; a device installs one. Per-ABI splits roughly halve the
  on-device payload and are orthogonal to the gpu axis. (MEL-ENGINE-VI.)
- **Strip the Android Dawn `.so` in release.** The 312/326 MB unstripped
  `libwebgpu_dawn.so` shrinks to ~16 MB packed in debug; a stripped release Dawn
  drops further. Confirm the release packaging path strips third-party `.so`s.
- **Garbage-collect stale per-axis object trees.** `build/android-<abi>/obj`
  accumulates orphaned backend objects (`webgpu.*.o` after a vulkan build). They
  never link, but they mislead inspection — they cost me a wrong hypothesis this
  session. A `./nob cache gc`-style prune of objects absent from the current
  resolved source set would keep the tree honest.
