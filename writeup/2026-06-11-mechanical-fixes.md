# 2026-06-11 Mechanical fixes

## Work done

**Fix 1 — `modules/easing/include/easing/easing.h`**
Replaced `#define MEL_EASING_COUNT 34` with a sizeof-derived count:
```c
#define MEL__EASING_ENTRY(n, f) { n, f },
#define MEL_EASING_COUNT (int)(sizeof((Mel_Easing_Entry[]){ MEL_EASING_LIST(MEL__EASING_ENTRY) }) / sizeof(Mel_Easing_Entry))
```
The count is now derived from `MEL_EASING_LIST` via a compound literal inside `sizeof` (zero-cost at runtime). The existing `easing.registry_count_matches_list` test confirms correctness.

Also wired up the easing test target in `modules/easing/build.c` — it existed on disk but was never registered with the build system, making it undiscoverable by `./nob test`.

**Fix 2 — `modules/await/src/await.c`**
The exclusivity assert was already present at the discrimination site (line 37):
```c
assert((step.future != NULL) + (step.channel != NULL) + (step.after_ns > 0) + (int)step.reschedule == 1);
```
No code change was needed. The file uses stdlib `<assert.h>` throughout; migrating to `mel_assert` would require adding a `debug` dependency and converting all eight asserts in the file, which is out of scope here.

**Fix 3 — `modules/hash/src/murmur3.c`, `modules/hash/src/siphash.c`**
Added `#include <core/compiler.h>` and annotated all intentional fallthrough cases with `MEL_FALLTHROUGH;`. Murmur3 has two (cases 3→2→1); SipHash has six (cases 7→6→5→4→3→2→1). All 14 hash vector tests pass after the change.

## Kludges

None.

## CLAUDE.md suggestions

The easing test not being registered in build.c is a pattern risk — modules with test files that aren't wired up in their `build.c` will be silently skipped by `./nob test`. Consider a lint step or documentation note reminding authors to add `mel_add_test` when a `test/` directory is present.

## Suggestions

- `modules/await/src/await.c` uses `<assert.h>` rather than `<debug/assert.h>`. Since `await` is an infrastructure-level module, it would benefit from the richer `mel_assert` machinery (interactive handler, ignore-forever, stacktrace). That requires adding `mel_depends(lib, "debug")` to `await/build.c`.
- `MEL_FALLTHROUGH` is already defined in `<core/compiler.h>` but is not documented in `docs/coding-guidelines.md`. Worth adding a note there since switch fallthrough is an easy source of future warnings.
