#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SLANG_VERSION="$(sed -n 's/^SLANG_VERSION=//p' "$HERE/SLANG_VERSION.lock")"
[ -n "$SLANG_VERSION" ] || { echo "build-wasm-mt: cannot read SLANG_VERSION from $HERE/SLANG_VERSION.lock" >&2; exit 1; }

PY="${MEL_SLANG_PYTHON:-/usr/bin/python3}"
WORK="${MEL_SLANG_BUILD_DIR:-${TMPDIR:-/tmp}/melody-slang-wasm-$SLANG_VERSION}"
OUT_ZIP="$HERE/slang-$SLANG_VERSION-wasm-mt.zip"

command -v emcmake >/dev/null || { echo "build-wasm-mt: emscripten (emcmake) not on PATH" >&2; exit 1; }

mkdir -p "$WORK"
cd "$WORK"

if [ ! -d slang/.git ]; then
    git clone --depth 1 --branch "v$SLANG_VERSION" --recurse-submodules --shallow-submodules \
        https://github.com/shader-slang/slang slang
fi
cd slang

if [ ! -d generators/bin ]; then
    cmake --workflow --preset generators --fresh
    rm -rf generators && mkdir generators
    cmake --install build --prefix generators --component generators --config Release
fi

emcmake cmake -B build-wasm-mt -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSLANG_LIB_TYPE=STATIC \
    -DSLANG_GENERATORS_PATH="$PWD/generators/bin" \
    -DPython3_EXECUTABLE="$PY" \
    -DSLANG_SLANG_LLVM_FLAVOR=DISABLE \
    -DSLANG_ENABLE_AFTERMATH=OFF -DSLANG_ENABLE_CUDA=OFF -DSLANG_ENABLE_GFX=OFF \
    -DSLANG_ENABLE_OPTIX=OFF -DSLANG_ENABLE_REPLAYER=OFF -DSLANG_ENABLE_SLANG_RHI=OFF \
    -DSLANG_ENABLE_TESTS=OFF -DSLANG_ENABLE_SLANGD=OFF -DSLANG_ENABLE_EXAMPLES=OFF \
    -DSLANG_ENABLE_SPLIT_DEBUG_INFO=OFF \
    -DCMAKE_C_FLAGS="-pthread -fwasm-exceptions -Os" \
    -DCMAKE_CXX_FLAGS="-pthread -fwasm-exceptions -Os" \
    -DCMAKE_EXE_LINKER_FLAGS="-pthread -fwasm-exceptions"
cmake --build build-wasm-mt -j 10

LLVM_BIN="$(dirname "$(command -v emcc)")/../libexec/llvm/bin"
[ -x "$LLVM_BIN/llvm-strip" ] || LLVM_BIN="$(brew --prefix emscripten 2>/dev/null)/libexec/llvm/bin"

STAGE="$WORK/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/lib" "$STAGE/include"
cp build-wasm-mt/Release/lib/libslang-compiler.a \
   build-wasm-mt/Release/lib/libcompiler-core.a \
   build-wasm-mt/Release/lib/libcore.a \
   build-wasm-mt/external/cmark/src/libcmark-gfm.a \
   build-wasm-mt/external/miniz/libminiz.a \
   build-wasm-mt/external/lz4/build/cmake/liblz4.a \
   "$STAGE/lib/"
for a in "$STAGE"/lib/*.a; do "$LLVM_BIN/llvm-strip" --strip-debug "$a"; done
cp include/slang.h include/slang-com-helper.h include/slang-com-ptr.h \
   include/slang-deprecated.h include/slang-image-format-defs.h "$STAGE/include/"

rm -f "$OUT_ZIP"
(cd "$STAGE" && zip -rq "$OUT_ZIP" lib include)
echo "build-wasm-mt: wrote $OUT_ZIP"
