#!/bin/bash
args=()
skip=0
for a in "$@"; do
    if [ "$skip" = 1 ]; then
        skip=0
        continue
    fi
    case "$a" in
        -isysroot | -target) skip=1 ;;
        --target=* | -mmacos*version-min=* | -miphoneos-version-min=* | -mios-* ) ;;
        *) args+=("$a") ;;
    esac
done
exec /usr/bin/cc -isysroot "$(xcrun --show-sdk-path --sdk macosx)" "${args[@]}"
