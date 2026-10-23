#!/bin/bash
# Builds DroidDeck's small Vulkan layers for aarch64 Linux into OUT_DIR.
# Usage: build.sh OUT_DIR VULKAN_INCLUDE_DIR [cc]
set -euo pipefail
OUT=$(realpath -m "$1"); INC=$2; CC=${3:-aarch64-linux-gnu-gcc}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
for name in noycbcr hideext malimem; do
  "$CC" -std=c11 -shared -fPIC -O2 -Wall -Wextra -Werror -Wno-unused-parameter -fvisibility=hidden -pthread \
    -Wl,-z,defs -I"$INC" -o "$OUT/libVkLayer_droiddeck_$name.so" "$HERE/vk_layer_droiddeck_$name.c"
  "${CC%gcc}strip" --strip-unneeded "$OUT/libVkLayer_droiddeck_$name.so" 2>/dev/null || true
  cp "$HERE/VK_LAYER_DROIDDECK_$name.json" "$OUT/"
done
ls -l "$OUT"
