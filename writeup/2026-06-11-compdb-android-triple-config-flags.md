# compdb: android triple API suffix + config flags dedup

## Work done

Two fixes to the compile commands DB generator (`modules/build/`).

**Fix 1 — android `-target` API suffix in compdb**

`toolchain.c` constructed `tc.triple` as `aarch64-linux-android` (no API level) while `tc.cc` already embedded `-target aarch64-linux-android26`. `compdb.c`'s `build_prefix` uses `tc.triple` when emitting the `-target` flag for cross targets, so LSP-invoked clang saw a bare triple without the API suffix — meaning it used a different sysroot than the real NDK build. Fixed by appending `26` to `tc.triple` in `toolchain.c` for the android case, keeping it in sync with the level embedded in `tc.cc`.

**Fix 2 — config flags deduplication**

`emit.c` had `static config_base()` and `compdb.c` had a private `static config_cflags()` — identical logic (push `-std=c23`, then `-O2 -DNDEBUG` for release or `-g -O0` for debug). Removed `config_cflags` from `compdb.c`, promoted `config_base` in `emit.c` to `mel_config_base_flags` (non-static), declared it in `runner.h`, and updated both call sites.

## Kludges

None.

## CLAUDE.md suggestions

None.

## Suggestions

The android API level (26) is hardcoded in two places: `tc.cc` string and `tc.triple`. They are now consistent, but a named constant or a field on `Mel_Toolchain` for `android_api` would prevent them drifting again if the API level is ever bumped.
