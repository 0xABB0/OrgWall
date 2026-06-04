#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"
SLANG_DIR="$REPO_ROOT/apps/hello-gpu/shaders/slang"
OUT_DIR="$REPO_ROOT/apps/hello-gpu/src"

SLANG_PREFIX="${MEL_SLANG_PREFIX:-}"
if [ -z "$SLANG_PREFIX" ]; then
    for cand in \
        "$REPO_ROOT/third-party/slang/build/macos-debug/prefix" \
        "$REPO_ROOT/third-party/slang/build/macos-release/prefix" \
        "$REPO_ROOT/third-party/slang/build/linux-debug/prefix" \
        "$REPO_ROOT/third-party/slang/build/linux-release/prefix"; do
        if [ -x "$cand/bin/slangc" ]; then SLANG_PREFIX="$cand"; break; fi
    done
fi
if [ -z "$SLANG_PREFIX" ] || [ ! -x "$SLANG_PREFIX/bin/slangc" ]; then
    echo "gen_bundles: slangc not found. Build the 'slang' third-party first:" >&2
    echo "    ./nob build slang-compile <platform>" >&2
    echo "or set MEL_SLANG_PREFIX to a slang install prefix." >&2
    exit 1
fi

SLANGC="$SLANG_PREFIX/bin/slangc"
export DYLD_LIBRARY_PATH="$SLANG_PREFIX/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export LD_LIBRARY_PATH="$SLANG_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

LOCK="$REPO_ROOT/tools/build/vendor/slang/SLANG_VERSION.lock"
SLANG_VERSION="$("$SLANGC" -v 2>&1 | head -1 | tr -d '[:space:]')"

emit_u32_array()
{
    local name="$1" file="$2" out="$3"
    {
        printf 'static const uint32_t %s[] = {\n' "$name"
        od -An -tu4 -v "$file" | awk '
            { for (i = 1; i <= NF; i++) { printf "    %su,\n", $i } }'
        printf '};\n'
    } >>"$out"
}

emit_u8_array()
{
    local name="$1" file="$2" out="$3"
    {
        printf 'static const uint8_t %s[] = {\n' "$name"
        od -An -tu1 -v "$file" | awk '
            { for (i = 1; i <= NF; i++) { printf "    %su,", $i; n++; if (n % 16 == 0) printf "\n" } }
            END { if (n % 16 != 0) printf "\n" }'
        printf '};\n'
    } >>"$out"
}

DXIL_PROFILE="${MEL_DXIL_PROFILE:-sm_6_0}"
DXIL_ONLY="${MEL_GEN_DXIL_ONLY:-0}"

compile_one()
{
    local src="$1" entry="$2" target="$3" out="$4"
    case "$target" in
        spirv) "$SLANGC" "$src" -target spirv -profile spirv_1_5 -fvk-use-entrypoint-name -entry "$entry" -o "$out" ;;
        metal) "$SLANGC" "$src" -target metal -entry "$entry" -o "$out" ;;
        wgsl)  "$SLANGC" "$src" -target wgsl -entry "$entry" -o "$out" ;;
        dxil)  "$SLANGC" "$src" -target dxil -profile "$DXIL_PROFILE" -entry "$entry" -o "$out" ;;
    esac
}

try_compile()
{
    local src="$1" entry="$2" target="$3" out="$4"
    if compile_one "$src" "$entry" "$target" "$out" >/dev/null 2>&1 && [ -s "$out" ]; then
        return 0
    fi
    rm -f "$out"
    return 1
}

gen_graphics_dxil()
{
    local stem="$1" upper="$2" vs="$3" fs="$4"
    local src="$SLANG_DIR/$stem.slang"
    local hdr="$OUT_DIR/${stem}_bundle.h"
    local tmp; tmp="$(mktemp -d)"

    echo "  $stem  dxil (vs=$vs fs=$fs)"
    if ! [ -f "$hdr" ]; then
        echo "    ERROR: $hdr missing; run the full (non-DXIL-only) pass first to mint SPIR-V/MSL/WGSL" >&2
        rm -rf "$tmp"
        exit 1
    fi
    if ! try_compile "$src" "$vs" dxil "$tmp/vs.dxil" || ! try_compile "$src" "$fs" dxil "$tmp/fs.dxil"; then
        echo "    ERROR: $stem DXIL emit failed (need slangc with DXC downstream + dxil.dll signer; Windows only)" >&2
        rm -rf "$tmp"
        exit 1
    fi

    grep -v -E "^#define ${upper}_HAS_DXIL " "$hdr" >"$tmp/hdr"
    awk -v u="$upper" '
        /_HAS_WGSL / && $2 == u "_HAS_WGSL" { print; printf "#define %s_HAS_DXIL 1\n", u; next }
        { print }' "$tmp/hdr" >"$hdr"

    printf '\n' >>"$hdr"
    emit_u8_array "${upper}_VERT_DXIL" "$tmp/vs.dxil" "$hdr"; printf '\n' >>"$hdr"
    emit_u8_array "${upper}_FRAG_DXIL" "$tmp/fs.dxil" "$hdr"

    rm -rf "$tmp"
}

gen_graphics()
{
    local stem="$1" upper="$2" vs="$3" fs="$4"
    local src="$SLANG_DIR/$stem.slang"
    local hdr="$OUT_DIR/${stem}_bundle.h"
    local tmp; tmp="$(mktemp -d)"

    if [ "$DXIL_ONLY" = 1 ]; then
        rm -rf "$tmp"
        gen_graphics_dxil "$stem" "$upper" "$vs" "$fs"
        return
    fi

    echo "  $stem  (vs=$vs fs=$fs)"
    compile_one "$src" "$vs" spirv "$tmp/vs.spv" >/dev/null
    compile_one "$src" "$fs" spirv "$tmp/fs.spv" >/dev/null
    "$SLANGC" "$src" -target spirv -profile spirv_1_5 -fvk-use-entrypoint-name \
        -entry "$vs" -entry "$fs" -reflection-json "$tmp/refl.json" -o "$tmp/all.spv" >/dev/null

    local has_msl=0
    if try_compile "$src" "$vs" metal "$tmp/vs.metal" && try_compile "$src" "$fs" metal "$tmp/fs.metal"; then
        has_msl=1
    else
        echo "    WARN: $stem MSL unsupported by slangc (bindless non-uniform indexing needs the melody.binding mixin); MSL arrays omitted" >&2
    fi

    local has_wgsl=0
    if try_compile "$src" "$vs" wgsl "$tmp/vs.wgsl" && try_compile "$src" "$fs" wgsl "$tmp/fs.wgsl"; then
        has_wgsl=1
    else
        echo "    WARN: $stem WGSL unsupported by slangc (bindless non-uniform indexing needs the melody.binding mixin); WGSL arrays omitted" >&2
    fi

    {
        printf '#pragma once\n\n'
        printf '#include <stdint.h>\n\n'
        printf 'static const char %s_SLANG_VERSION[] = "%s";\n' "$upper" "$SLANG_VERSION"
        printf 'static const char %s_VERT_ENTRY[] = "%s";\n' "$upper" "$vs"
        printf 'static const char %s_FRAG_ENTRY[] = "%s";\n' "$upper" "$fs"
        printf '#define %s_HAS_MSL %d\n' "$upper" "$has_msl"
        printf '#define %s_HAS_WGSL %d\n' "$upper" "$has_wgsl"
        printf '#define %s_HAS_DXIL 0\n\n' "$upper"
    } >"$hdr"

    emit_u32_array "${upper}_VERT_SPV" "$tmp/vs.spv" "$hdr"; printf '\n' >>"$hdr"
    emit_u32_array "${upper}_FRAG_SPV" "$tmp/fs.spv" "$hdr"
    if [ "$has_msl" = 1 ]; then
        printf '\n' >>"$hdr"
        emit_u8_array "${upper}_VERT_MSL" "$tmp/vs.metal" "$hdr"; printf '\n' >>"$hdr"
        emit_u8_array "${upper}_FRAG_MSL" "$tmp/fs.metal" "$hdr"
    fi
    if [ "$has_wgsl" = 1 ]; then
        printf '\n' >>"$hdr"
        emit_u8_array "${upper}_VERT_WGSL" "$tmp/vs.wgsl" "$hdr"; printf '\n' >>"$hdr"
        emit_u8_array "${upper}_FRAG_WGSL" "$tmp/fs.wgsl" "$hdr"
    fi

    rm -rf "$tmp"
}

gen_compute()
{
    local stem="$1" upper="$2" cs="$3"
    local src="$SLANG_DIR/$stem.slang"
    local hdr="$OUT_DIR/${stem}_bundle.h"
    local tmp; tmp="$(mktemp -d)"

    echo "  $stem  (cs=$cs)"
    compile_one "$src" "$cs" spirv "$tmp/cs.spv" >/dev/null
    "$SLANGC" "$src" -target spirv -profile spirv_1_5 -fvk-use-entrypoint-name \
        -entry "$cs" -reflection-json "$tmp/refl.json" -o "$tmp/all.spv" >/dev/null

    local has_msl=0
    if try_compile "$src" "$cs" metal "$tmp/cs.metal"; then
        has_msl=1
    else
        echo "    WARN: $stem MSL unsupported by slangc (bindless non-uniform indexing needs the melody.binding mixin); MSL arrays omitted" >&2
    fi

    local has_wgsl=0
    if try_compile "$src" "$cs" wgsl "$tmp/cs.wgsl"; then
        has_wgsl=1
    else
        echo "    WARN: $stem WGSL unsupported by slangc (bindless non-uniform indexing needs the melody.binding mixin); WGSL arrays omitted" >&2
    fi

    {
        printf '#pragma once\n\n'
        printf '#include <stdint.h>\n\n'
        printf 'static const char %s_SLANG_VERSION[] = "%s";\n' "$upper" "$SLANG_VERSION"
        printf 'static const char %s_COMP_ENTRY[] = "%s";\n' "$upper" "$cs"
        printf '#define %s_HAS_MSL %d\n' "$upper" "$has_msl"
        printf '#define %s_HAS_WGSL %d\n\n' "$upper" "$has_wgsl"
    } >"$hdr"

    emit_u32_array "${upper}_COMP_SPV" "$tmp/cs.spv" "$hdr"
    if [ "$has_msl" = 1 ]; then
        printf '\n' >>"$hdr"
        emit_u8_array "${upper}_COMP_MSL" "$tmp/cs.metal" "$hdr"
    fi
    if [ "$has_wgsl" = 1 ]; then
        printf '\n' >>"$hdr"
        emit_u8_array "${upper}_COMP_WGSL" "$tmp/cs.wgsl" "$hdr"
    fi

    rm -rf "$tmp"
}

echo "gen_bundles: slangc $SLANG_VERSION ($SLANGC) dxil_only=$DXIL_ONLY"

if [ "$DXIL_ONLY" = 1 ]; then
    gen_graphics triangle TRIANGLE vs_main fs_main
    gen_graphics gradient GRADIENT vs_main fs_main
    gen_graphics quad     QUAD     vs_main fs_main
    echo "gen_bundles: appended signed DXIL to triangle/gradient/quad bundles in $OUT_DIR"
    exit 0
fi

if [ ! -f "$LOCK" ]; then
    echo "gen_bundles: SLANG_VERSION.lock missing at $LOCK" >&2
    echo "    the slangc version pin is the source of truth and must be committed; refusing to mint a lock implicitly." >&2
    exit 1
fi
LOCKED_VERSION="$(sed -n 's/^SLANG_VERSION=//p' "$LOCK" | head -1 | tr -d '[:space:]')"
if [ -z "$LOCKED_VERSION" ]; then
    echo "gen_bundles: SLANG_VERSION.lock at $LOCK has no SLANG_VERSION= line; lock is malformed." >&2
    exit 1
fi
if [ "$LOCKED_VERSION" != "$SLANG_VERSION" ]; then
    echo "gen_bundles: slangc version mismatch — discovered '$SLANG_VERSION' but $LOCK pins '$LOCKED_VERSION'." >&2
    echo "    The pin is enforced, not regenerated: align the slangc toolchain to the pinned version," >&2
    echo "    or deliberately bump the pin (third-party/slang/build.c SLANG_VERSION macro + $LOCK) and re-mint." >&2
    exit 1
fi

gen_graphics triangle TRIANGLE vs_main fs_main
gen_graphics blit     BLIT     vs_main fs_main
gen_graphics gradient GRADIENT vs_main fs_main
gen_graphics quad     QUAD     vs_main fs_main
gen_compute  clear    CLEAR    cs_main

echo "gen_bundles: slangc matches pin $LOCKED_VERSION; wrote *_bundle.h to $OUT_DIR"
