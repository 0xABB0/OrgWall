# Android SIGBUS — `alignas` dropped under `-std=c23`

## Work done

`apps/hello-vibration` crashed on the physical Pixel 4a (sunfish, arm64) at launch with
`SIGBUS / BUS_ADRALN`. The fault was not in the vibration module; vibration merely happened to be
the first gui+reactor app run on the connected handset.

**Root cause.** The crash backtrace ran through a load-time constructor:
`__dl__ZN6soinfo17call_constructorsEv` → `mel__log_init` → `mel_log_level_register`
→ `mel_mutex_lock(&level_lock)` → bionic `pthread_mutex_lock` → `__aarch64_cas2_acq` →
`SIGBUS`, fault addr `…d7` (odd). bionic's `pthread_mutex_lock` issues a 2-byte atomic CAS on the
mutex's `_Atomic(uint16_t) state`; arm64 atomic/CAS instructions raise an alignment fault on a
non-naturally-aligned address. `level_lock` sat at vaddr `0x33ad7` — alignment 1, not 8.

The sync-primitive storage structs in `modules/thread/include/thread/*.h` declared
`alignas(MEL_*_STORAGE_ALIGN) byte _storage[…]`, pulling `alignas` from `<stdalign.h>`. Under the
repo's `-std=c23` with NDK r28's clang, the **C23 `alignas` keyword is silently miscompiled to
alignment 1** on a struct member — no diagnostic. `_Alignof(Mel_Mutex)` returned 1, so every global
`Mel_Mutex`/`Mel_RWLock`/`Mel_Cond`/etc. was under-aligned. Isolated repro:

- `_Alignas(8) char s[64]` → alignof 8 (correct)
- `alignas(8)  char s[64]` → alignof 1 (broken) — bare C23 keyword, no `<stdalign.h>` needed
- under `-std=c11/c17/gnu17` both forms → 8

Every Mel sync primitive was a latent landmine on any arm64 target with LSE/strict atomics; the
mutex was simply the first one touched, at `dlopen` time.

**Fix.** Switched all nine `thread/*.h` storage structs from the bare `alignas` to the repo's own
`MEL_ALIGNAS` macro (`core/compiler.h`), which resolves to `_Alignas` for C — the form the toolchain
compiles correctly. Dropped the now-unused `<stdalign.h>` include, added `<core/compiler.h>`.
Added a load-bearing guard in `thread/src/posix/mutex.c`:
`static_assert(_Alignof(Mel_Mutex) == MEL_MUTEX_STORAGE_ALIGN, …)` — the existing asserts only
checked the *constant* was large enough, never that the struct *achieved* the alignment. This new
assert fires at compile time on exactly this failure mode (MEL-ENGINE-VIII: no silent corruption).

**Verification.** `_Alignof` static asserts pass under `-std=c23`; rebuilt `libmelody.so` places
`level_lock`/`flush_mutex`/`sink_lock` at 8-aligned vaddrs (`0x33b18`/`0x33a98`/`0x33a40`). On the
physical Pixel 4a the app now launches and stays alive, enumerates `Android Vibrator (amp=1 sharp=0
pause=1)` — matching real hardware with amplitude control — and the full Play/Pause/Resume/Abort
lifecycle runs with playback resolving `0x0` (OK), under `-Xcheck:jni`, with no SIGBUS and no JNI
errors.

## Kludges (MEL-ENGINE-VIII — confess all)

- None. The fix is in `modules/thread/`, not the vibration module — but that is where the defect
  lives, not a shortcut. No fixed arrays, no enums, no `mel_malloc`, no comments added.
- The guard was added to `posix/mutex.c` only (the primitive that crashed and the platform that
  miscompiles), not symmetrically to all eight sibling implementation TUs. The header fix already
  covers all nine; the asymmetric guard is a proportionate regression tripwire, not full coverage.

## CLAUDE.md suggestions (recommendations only — not applied)

- The bare C23 `alignas` keyword is unsafe on the project toolchain (NDK r28 clang). A coding-guideline
  note — "never use bare `alignas`/`_Alignas`; use `MEL_ALIGNAS`" — plus a grep/CI tripwire for bare
  `alignas(` would prevent reintroduction.

## Suggestions

- Extend the `_Alignof(...) == MEL_*_STORAGE_ALIGN` guard to the other eight storage structs (cond,
  rwlock, sem, barrier, event, once, tls, thread handle); they share the identical bug surface.
- A logcat sink for the gui android runtime would have surfaced `mel_log_*` output during this
  hunt; on-device diagnosis relied on the tombstone and `uiautomator`, since Melody logs do not reach
  logcat (already noted in the vibration writeup).
