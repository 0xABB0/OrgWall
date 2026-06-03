#!/usr/bin/env bash
# Fetch + verify + extract the pinned Slang toolchain into dist/ (gitignored).
# Idempotent: skips if dist/bin/slangc already present.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cd "$here"
# shellcheck disable=SC1091
source SLANG_VERSION.lock
if [ -x dist/bin/slangc ]; then echo "slang: dist already present ($SLANG_VERSION)"; exit 0; fi
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
echo "slang: fetching $SLANG_VERSION"
curl -sSL --connect-timeout 30 -o "$tmp/slang.tgz" "$SLANG_URL"
got="$(shasum -a 256 "$tmp/slang.tgz" | cut -d' ' -f1)"
[ "$got" = "$SLANG_SHA256" ] || { echo "slang: sha256 mismatch (got $got want $SLANG_SHA256)" >&2; exit 1; }
mkdir -p "$tmp/x"; tar -xzf "$tmp/slang.tgz" -C "$tmp/x"
rm -rf dist; mkdir -p dist
cp -R "$tmp/x/bin" dist/bin
cp -R "$tmp/x/include" dist/include
cp -R "$tmp/x/lib" dist/lib
# trim the LLVM + gfx dylibs not needed for -target spirv / -target metal
rm -f dist/lib/libslang-llvm.dylib dist/lib/libgfx*.dylib
echo "slang: ready at dist/ ($SLANG_VERSION)"
