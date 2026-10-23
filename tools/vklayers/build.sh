#!/bin/bash
# Builds DroidDeck's small Vulkan layers for aarch64 Linux into OUT_DIR.
# Usage: build.sh OUT_DIR VULKAN_INCLUDE_DIR [cc]
set -euo pipefail
OUT=$(realpath -m "$1"); INC=$2; CC=${3:-aarch64-linux-gnu-gcc}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
"$CC" -std=c11 -shared -fPIC -O2 -Wall -Wextra -Werror -Wno-unused-parameter -fvisibility=hidden -pthread \
  -Wl,-z,defs -I"$INC" -o "$OUT/libVkLayer_droiddeck_noycbcr.so" "$HERE/vk_layer_droiddeck_noycbcr.c"
"$CC" -std=c11 -shared -fPIC -O2 -Wall -Wextra -Werror -Wno-unused-parameter -fvisibility=hidden -pthread \
  -Wl,-z,defs -I"$INC" -o "$OUT/libVkLayer_droiddeck_hideexts.so" "$HERE/vk_layer_droiddeck_hideexts.c"
"${CC%gcc}strip" --strip-unneeded "$OUT/libVkLayer_droiddeck_noycbcr.so" 2>/dev/null || true
"${CC%gcc}strip" --strip-unneeded "$OUT/libVkLayer_droiddeck_hideexts.so" 2>/dev/null || true
cp "$HERE/VK_LAYER_DROIDDECK_noycbcr.json" "$HERE/VK_LAYER_DROIDDECK_hideexts.json" "$OUT/"
ls -l "$OUT"
