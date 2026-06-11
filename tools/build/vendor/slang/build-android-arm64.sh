#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../../../.." && pwd)"

SLANG_VERSION="$(sed -n 's/^SLANG_VERSION=//p' "$HERE/SLANG_VERSION.lock")"
[ -n "$SLANG_VERSION" ] || { echo "build-android-arm64: cannot read SLANG_VERSION from $HERE/SLANG_VERSION.lock" >&2; exit 1; }

NDK="${ANDROID_NDK_HOME:-}"
if [ -z "$NDK" ]; then
    NDK_BASE="$HOME/Library/Android/sdk/ndk"
    NDK="$NDK_BASE/$(ls "$NDK_BASE" | sort -V | tail -1)"
fi
[ -d "$NDK" ] || { echo "build-android-arm64: Android NDK not found (set ANDROID_NDK_HOME)" >&2; exit 1; }

PY="${MEL_SLANG_PYTHON:-/usr/bin/python3}"
WORK="${MEL_SLANG_BUILD_DIR:-${TMPDIR:-/tmp}/melody-slang-android-$SLANG_VERSION}"
OUT_ZIP="$HERE/slang-$SLANG_VERSION-android-aarch64.zip"

mkdir -p "$WORK"
cd "$WORK"

if [ ! -d slang/.git ]; then
    git clone --depth 1 --branch "v$SLANG_VERSION" --recurse-submodules --shallow-submodules \
        https://github.com/shader-slang/slang slang
fi
cd slang

cmake --workflow --preset generators --fresh
rm -rf generators && mkdir generators
cmake --install build --prefix generators --component generators --config Release

export ANDROID_NDK_HOME="$NDK"
cmake --preset android-arm64 --fresh \
    -DSLANG_GENERATORS_PATH="$PWD/generators/bin" \
    -DPython3_EXECUTABLE="$PY"
cmake --build --preset android-arm64-release

LIBDIR="build-android-arm64-v8a/Release/lib"
[ -f "$LIBDIR/libslang-compiler.so" ] || { echo "build-android-arm64: libslang-compiler.so not produced" >&2; exit 1; }

STAGE="$WORK/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/lib" "$STAGE/include"
for so in "$LIBDIR"/libslang-compiler.so "$LIBDIR"/libslang-glslang-*.so "$LIBDIR"/libslang-glsl-module-*.so; do
    cp "$so" "$STAGE/lib/"
    "$NDK/toolchains/llvm/prebuilt/"*/bin/llvm-strip --strip-unneeded "$STAGE/lib/$(basename "$so")"
done
cp include/slang.h include/slang-com-helper.h include/slang-com-ptr.h \
   include/slang-deprecated.h include/slang-image-format-defs.h "$STAGE/include/"
[ -f build-android-arm64-v8a/slang-tag-version.h ] && cp build-android-arm64-v8a/slang-tag-version.h "$STAGE/include/" || true

rm -f "$OUT_ZIP"
(cd "$STAGE" && zip -rq "$OUT_ZIP" lib include)
echo "build-android-arm64: wrote $OUT_ZIP"
