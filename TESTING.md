# Testing

Tests live in `tests/` and are compiled into a single binary (`build/tests`). Tests auto-register via `__attribute__((constructor))` — no manual registration needed.

## Writing tests

```c
#include "../melody/test.harness.h"
#include "../melody/whatever.you.need.h"

MEL_TEST(my_test_name, .tags = "mytag")
{
    MEL_ASSERT(true);
    MEL_ASSERT_EQ(1, 1);
}
```

That's it. No `main()`, no `MEL_RUN_TEST()`, no registration boilerplate. The `MEL_TEST` macro generates a constructor that registers the test into a global linked list before `main()` runs.

A test passes if it returns without incrementing the failure counter. A test fails if any `MEL_ASSERT_*` / `MEL_FAIL` fires (which increments the counter and returns early).

## Tags

Tags are optional. A test can have multiple tags separated by commas:

```c
MEL_TEST(my_test, .tags = "allocator, performance")
```

Tests tagged `"visual"` are excluded by default (they need a GPU). Use `--visual` to include them.

## Available assertions

- `MEL_ASSERT(cond)` — generic boolean
- `MEL_ASSERT_EQ(a, b)` / `MEL_ASSERT_NEQ(a, b)` — equality
- `MEL_ASSERT_LT`, `MEL_ASSERT_LE`, `MEL_ASSERT_GT`, `MEL_ASSERT_GE` — comparisons
- `MEL_ASSERT_FLOAT_EQ(a, b, eps)` — float with epsilon
- `MEL_ASSERT_NULL(ptr)` / `MEL_ASSERT_NOT_NULL(ptr)`
- `MEL_ASSERT_STR_EQ(a, b)` — string comparison
- `MEL_FAIL(msg)` — unconditional fail

## Running tests

```bash
./nob test                     # build + run all tests (visual excluded)
./nob test --list              # list all tests with IDs, files, tags
./nob test --filter arena      # run tests with "arena" in the name
./nob test --tag allocator     # run tests matching a tag
./nob test --id 5              # run a single test by ID
./nob test --visual            # include visual tests
```

Args after `test` are forwarded to the test binary. You can also run `./build/tests` directly.

## Existing tags

allocator, collection, math, anim, async, hash, gpu, render, string, ui, event, sim, visual

## Adding a new test file

Tests live in `melody/` alongside the modules they test, using the naming convention `domain.module.test.spec.c`.

1. Create `melody/domain.module.test.yourspec.c`
2. Write `MEL_TEST(...)` functions with appropriate tags
3. That's it — nob discovers all `.test.*.c` files in `melody/` automatically

Legacy tests in `tests/` still work but new tests should use the colocated convention.
