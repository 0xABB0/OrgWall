#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SLANG_VERSION="$(sed -n 's/^SLANG_VERSION=//p' "$HERE/SLANG_VERSION.lock")"
[ -n "$SLANG_VERSION" ] || { echo "build-ios-sim-arm64: cannot read SLANG_VERSION from $HERE/SLANG_VERSION.lock" >&2; exit 1; }

PY="${MEL_SLANG_PYTHON:-/usr/bin/python3}"
WORK="${MEL_SLANG_BUILD_DIR:-${TMPDIR:-/tmp}/melody-slang-ios-$SLANG_VERSION}"
OUT_ZIP="$HERE/slang-$SLANG_VERSION-ios-sim-aarch64.zip"

mkdir -p "$WORK"
cd "$WORK"

if [ ! -d slang/.git ]; then
    git clone --depth 1 --branch "v$SLANG_VERSION" --recurse-submodules --shallow-submodules \
        https://github.com/shader-slang/slang slang
fi
cd slang

if [ ! -x generators/bin/slang-bootstrap ] && [ ! -d generators/bin ]; then
    cmake --workflow --preset generators --fresh
    rm -rf generators && mkdir generators
    cmake --install build --prefix generators --component generators --config Release
fi

cmake -B build-ios-sim -G Ninja \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_MACOSX_BUNDLE=OFF \
    -DSLANG_LIB_TYPE=SHARED \
    -DSLANG_GENERATORS_PATH="$PWD/generators/bin" \
    -DPython3_EXECUTABLE="$PY" \
    -DSLANG_ENABLE_GFX=OFF -DSLANG_ENABLE_SLANG_RHI=OFF -DSLANG_ENABLE_TESTS=OFF \
    -DSLANG_ENABLE_SLANGD=OFF -DSLANG_ENABLE_REPLAYER=OFF -DSLANG_SLANG_LLVM_FLAVOR=DISABLE \
    -DSLANG_ENABLE_EXAMPLES=OFF -DSLANG_ENABLE_XLIB=OFF -DSLANG_ENABLE_CUDA=OFF \
    -DSLANG_ENABLE_OPTIX=OFF -DSLANG_ENABLE_NVAPI=OFF -DSLANG_ENABLE_AFTERMATH=OFF \
    -DSLANG_ENABLE_SPLIT_DEBUG_INFO=OFF
cmake --build build-ios-sim -j 10

DYLIB="build-ios-sim/Release/lib/libslang-compiler.0.$SLANG_VERSION.dylib"
[ -f "$DYLIB" ] || DYLIB="$(ls build-ios-sim/Release/lib/libslang-compiler.*.dylib | head -1)"
[ -f "$DYLIB" ] || { echo "build-ios-sim-arm64: libslang-compiler dylib not produced" >&2; exit 1; }

STAGE="$WORK/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/lib" "$STAGE/include"
cp "$DYLIB" "$STAGE/lib/libslang-compiler.dylib"
install_name_tool -id @rpath/libslang-compiler.dylib "$STAGE/lib/libslang-compiler.dylib"
strip -x "$STAGE/lib/libslang-compiler.dylib" || true
codesign -f -s - "$STAGE/lib/libslang-compiler.dylib"
cp include/slang.h include/slang-com-helper.h include/slang-com-ptr.h \
   include/slang-deprecated.h include/slang-image-format-defs.h "$STAGE/include/"

rm -f "$OUT_ZIP"
(cd "$STAGE" && zip -rq "$OUT_ZIP" lib include)
echo "build-ios-sim-arm64: wrote $OUT_ZIP"
