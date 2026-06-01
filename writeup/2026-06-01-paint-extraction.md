# 2026-06-01 — paint module, slice 1 (owned pixmap, quartz)

## Work done

New `modules/paint/` — immediate-mode 2D extracted from `gui`, per `design/paint.md` §7
items 1–2. Slice 1 is the owned-offscreen path (the only genuinely new capability; the rest
of `paint.md` is extraction): `Mel_Pixmap` → painter → CPU readback, on the `quartz` backend.

- Public contract: `handle.h` / `pixmap.h` / `painter.h` / `paint.h`. `Mel_Pixmap`,
  `Mel_Drawable` are `Mel_SlotMap_Handle` aliases; color is `mel_color8` verbatim (decision:
  single source of truth, MEL-ENGINE-IX); the seven ops unchanged in shape.
- `src/registry.c` — one module-global drawable slotmap, eagerly built in a `MEL_CONSTRUCTOR`
  (decision: not lazy). Table on `mel_alloc_heap()`; pixmap pixel buffers on the caller's
  `Mel_Alloc` (MEL-CODE-003).
- `src/quartz/quartz.c` — pure-C CoreGraphics/CoreText (no AppKit/UIKit, no ObjC runtime).
  Pixmap = `CGBitmapContextCreate` over our buffer, DeviceRGB, premultiplied, CTM-flipped to
  y-down drawing + top-down memory. `draw_text` rewritten onto `CTLineDraw` with the baseline
  at `pos.y + ascent`. Painter is `thread_local`, alloc-free.
- Verified: `example/paint_example.c` self-checks three interior pixels (bg corner proves
  y-down→row-0; red interior; ellipse centre) — all pass — and dumps a PPM. Eyeballed: text
  upright, geometry y-down, premultiplied alpha composites correctly. Drawable `alive()`
  flips false after destroy.
- Specs tidied (MEL-SPEC-002): `modules/paint/spec.md` (contract+conventions, terse) and
  `todo.md` (deferred slices); removed the design/ slice spec I'd authored.

Decisions taken with Gabbo via the brainstorm: first slice = pixmap+readback+quartz; registry
= module-global, constructor-initialized; color = `mel_color8` verbatim.

## Kludges / debt (MEL-ENGINE-VIII — confess all)

- **No unit test.** The repo's `MEL_TEST` harness is unimplemented — `libtest.a` exports
  `mel_test_register/fail/abort` undefined, no generated `main`, no `test/` auto-discovery; the
  running tests in-repo (continuation) ship their own `main`. Verified via a runnable example
  instead. `test/pixmap_test.c` is kept as an orphan for when the harness lands (Gabbo is on it).
- **`mel_assert` is a no-op** (both branches of `debug/assert.h` empty), so paint's liveness /
  `owns` / `painting` asserts compile away. `alive()` still works via slotmap generation, but
  contract violations won't fire — including the `thread_local` single-active-painter guard,
  which would silently clobber `g_painter` on accidental nesting rather than asserting.
- **`Mel_Drawable`/`Mel_Pixmap` are typedef aliases**, not nominal types — no compile-time
  catch for passing one where the other is meant; only the runtime `owns` flag distinguishes.
- **Single backend** — module builds only on macos/ios; winui/dom/androidnative/soft deferred.
- **Wrong restore.** I restored `design/gui-window-seam.md` and
  `design/thermal-sensor-augmentation.md` from git after they appeared deleted, not realising
  they were intentionally retired (implemented; their content now in the modules). Re-deleted
  on Gabbo's correction. Lesson: a `D` on a doc I read earlier this session is not necessarily
  an accident — query before restoring.

## CLAUDE.md suggestions (recommendations only)

- None for CLAUDE.md itself. Two repo-level items worth a task each (not CLAUDE.md edits):
  implement the `MEL_TEST` runtime + `main` + `test/` discovery; give `mel_assert` a body.
  Both undercut MEL-ENGINE-VIII repo-wide.

## Suggestions

- Next paint slice with the most leverage and on-host verifiability: the borrowed-window
  drawable + gui canvas migration (retires gui's private `Mel_Color`). The other native
  backends are mechanical lifts but only verifiable on their platforms — note winui's DIB is
  BGRA and needs a readback swizzle (flagged in `todo.md`).
- `design/paint.md` (broad design) still holds unbuilt slices; once they land it should fold
  into `modules/paint/spec.md`/`todo.md` and leave `design/` per MEL-SPEC-002. Left untouched
  for now — it's the authored design of record for the deferred work.

## Correction (post-session)

The claim above — "the repo's `MEL_TEST` harness is unimplemented, no runtime/main anywhere" —
was **wrong**, from an incomplete search: I grepped `modules/test`/`modules/build` and never
looked in `tools/`. The runtime + `main` live in `tools/test/src/runner.c` and existed before
this session (commit `121f773`). The harness works: `./nob test paint-pixmap macos` builds,
links the runner, runs `test/pixmap_test.c` in child-process isolation, and passes. The only
real gap is that `mel_add_test` does not auto-link the runner, so `build.c` adds it as a source
explicitly. The `mel_assert` no-op observation stands (re-verified).
