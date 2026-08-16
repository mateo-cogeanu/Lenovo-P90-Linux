#!/bin/sh
set -eu

: "${P90_UNSIGNED_IMAGE:?Set P90_UNSIGNED_IMAGE to the stitched command-line/bootstub/kernel/initrd image}"
: "${P90_OSIP_OUT:?Set P90_OSIP_OUT to the desired OSIP image path}"

case "$P90_UNSIGNED_IMAGE" in
    /*) unsigned=$P90_UNSIGNED_IMAGE ;;
    *) unsigned=$(pwd)/$P90_UNSIGNED_IMAGE ;;
esac
case "$P90_OSIP_OUT" in
    /*) output=$P90_OSIP_OUT ;;
    *) output=$(pwd)/$P90_OSIP_OUT ;;
esac

test -s "$unsigned"

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 "$script_dir/make_intel_osip.py" \
    --input "$unsigned" \
    --output "$output" \
    --load-address 0x01100000 \
    --entry-point 0x01101000 \
    --attribute 1

test -s "$output"
sha256sum "$output"
