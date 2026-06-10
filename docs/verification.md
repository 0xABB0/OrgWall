# Verification

How to run and trust the repo's gates. Companion to `./nob test`.

## Test runner

`tools/test/src/runner.c` runs every `MEL_TEST` body in a forked child
(POSIX; win32 runs in-process) with a per-test alarm timeout. A signaled child
reports CRASH, a timed-out child reports TIMEOUT; neither takes the harness
down.

`MEL_TEST_NOFORK=1` runs bodies in-process instead. Use it when fork itself is
the problem, at the cost of isolation (one crash kills the run).

## Fork vs the Objective-C runtime (macOS)

Test suites that initialize the Objective-C runtime — anything touching Metal,
AppKit, or CoreVideo (gpu-metal, gpu-stress, gpu-visual, and friends) — crash
under fork-per-test: ObjC and dispatch state do not survive `fork()` into the
child that then re-enters the frameworks. They are not test failures.

Run those suites with:

    MEL_TEST_NOFORK=1 ./nob test gpu-metal

## Vulkan on macOS

MoltenVK lives in homebrew; the loader is not on the default dyld path:

    DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-visual

## ThreadSanitizer drivers

`modules/port/test/tsan_build.sh <driver.c>` builds the port's standalone tsan
harnesses (every TU compiled `-fsanitize=thread`, which the nob target graph
does not express per-target). See the port readme's Tests section for what
each driver pins.

## App smoke gates

`./nob run melody-showcase -- --smoke` runs the module sweep headless and
exits nonzero if any chained step fails. GUI apps are smoke-verified by
launching, confirming the process is alive and parked after a few seconds,
and killing it; haptics, audio output, and camera streaming need an attended
session (TCC prompts, audible output).
