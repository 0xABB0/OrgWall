#!/bin/zsh
set -e
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"

CC=/opt/homebrew/opt/llvm/bin/clang
OUT="$ROOT/modules/port/build/tsan"
mkdir -p "$OUT"

INC=(
  -Imodules/core/include
  -Imodules/allocator/include
  -Imodules/hash/include
  -Imodules/thread/include
  -Imodules/collection/include
  -Imodules/executor/include
  -Imodules/future/include
  -Imodules/string/include
  -Imodules/time/include
  -Imodules/vat/include
  -Imodules/debug/include
  -Imodules/log/include
  -Imodules/port/include
)

FLAGS=(-std=c23 -g -O1 -fsanitize=thread -DMEL_LOG_DISABLED=1 -arch arm64 $INC)

SRCS=(
  modules/port/src/port.c
  modules/port/src/apple/port_backend.c
  modules/vat/src/vat.c
  modules/vat/src/driver_fair.c
  modules/vat/src/timer.c
  modules/vat/src/darwin/waiter_kqueue.c
  modules/vat/src/macos/waiter_cocoa.c
  modules/future/src/future.c
  modules/executor/src/executor.c
  modules/collection/src/slotmap.c
  modules/collection/src/collection.mpsc.c
  modules/allocator/src/allocator.c
  modules/allocator/src/heap.c
  modules/thread/src/apple/thread.c
  modules/thread/src/posix/mutex.c
  modules/thread/src/apple/cond.c
  modules/thread/src/apple/sem.c
  modules/thread/src/apple/futex.c
  modules/thread/src/once.c
  modules/time/src/nano.unix.c
  modules/time/src/clock.c
  modules/debug/src/assert.c
  modules/debug/src/stacktrace.c
  modules/debug/src/macos/stacktrace.c
  modules/debug/src/posix/assert_backend.c
  modules/debug/src/macos/assert_dialog.m
  modules/string/src/str8.c
  modules/hash/src/xxh.c
)

OBJS=()
for s in $SRCS; do
  o="$OUT/$(echo $s | tr '/.' '__').o"
  $CC $FLAGS -c "$s" -o "$o"
  OBJS+=("$o")
done

TESTSRC="${1:-modules/port/test/tsan_cancel_race.c}"
to="$OUT/test.o"
$CC $FLAGS -c "$TESTSRC" -o "$to"
OBJS+=("$to")

$CC -fsanitize=thread -arch arm64 $OBJS -o "$OUT/race" \
  -framework CoreFoundation -framework Foundation -framework AppKit

echo "built $OUT/race"
