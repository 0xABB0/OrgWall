# Testing

Tests auto-register via `__attribute__((constructor))`. No manual registration.

## Writing tests

```c
#include "test.harness.h"
#include "whatever.you.need.h"

MEL_TEST(my_test_name, .tags = "mytag")
{
    MEL_ASSERT(true);
    MEL_ASSERT_EQ(1, 1);
}
```

## Tags

Optional. Comma-separated: `.tags = "allocator, performance"`.
Tests tagged `"visual"` are excluded by default (need GPU). Use `--visual` to include.

## Assertions

- `MEL_ASSERT(cond)` — boolean
- `MEL_ASSERT_EQ(a, b)` / `MEL_ASSERT_NEQ(a, b)` — equality
- `MEL_ASSERT_LT`, `MEL_ASSERT_LE`, `MEL_ASSERT_GT`, `MEL_ASSERT_GE` — comparisons
- `MEL_ASSERT_FLOAT_EQ(a, b, eps)` — float with epsilon
- `MEL_ASSERT_NULL(ptr)` / `MEL_ASSERT_NOT_NULL(ptr)`
- `MEL_ASSERT_STR_EQ(a, b)` — string comparison
- `MEL_FAIL(msg)` — unconditional fail

## Running

```bash
./nob test                     # all tests (visual excluded)
./nob test --filter arena      # name filter
./nob test --tag allocator     # tag filter
./nob test --id 5              # single test by ID
./nob test --list              # list all tests
./nob test --visual            # include visual tests
```

## Adding tests

Create `melody/domain.module.test.yourspec.c`. Write `MEL_TEST(...)` functions. Done — nob discovers `.test.*.c` files automatically.

Legacy tests in `tests/` still work but new tests go in `melody/`.
