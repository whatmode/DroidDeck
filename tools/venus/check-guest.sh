#!/usr/bin/env bash
# check-guest.sh <mesa-src> [build-dir] - compile-check the Venus guest ICD in about a minute,
# with the same flags CI uses, instead of waiting 30-60 minutes for a build round.
#
# <mesa-src> is a Mesa checkout with tools/venus/guest-patches applied (the same tree
# build-guest.sh builds). The script configures a build dir the same way build-guest.sh does and
# compiles every guest driver object with the build's own flags, so the check cannot drift from
# what CI compiles. It prints the compiler output and a final PASS/FAIL, so a driver edit can be
# checked before pushing.
#
# The build dir is reused: the first run pays for meson setup, later runs after editing one file
# take a few seconds. Set VENUS_WORK to keep the tree somewhere stable; otherwise a temp dir is
# used.
set -euo pipefail
MESA=$(cd "$1" && pwd)
WORK=${VENUS_WORK:-$(mktemp -d)}
B=${2:-$WORK/build-mesa}
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/source.env"

if [ ! -f "$B/build.ninja" ]; then
  echo "== configuring $B"
  meson setup "$B" "$MESA" --buildtype release \
    -Dvulkan-drivers=virtio -Dgallium-drivers= -Dplatforms=x11,wayland \
    -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dopengl=false -Dglx=disabled -Dgbm=disabled \
    -Dllvm=disabled -Dzstd=disabled -Dlibunwind=disabled -Dvalgrind=disabled > "$WORK/check-configure.log" 2>&1 \
    || { echo "configure FAILED"; tail -30 "$WORK/check-configure.log"; exit 1; }
fi

# The guest driver objects, from the build's own target list: vn_android.c is Android-only
# (meson adds it under with_platform_android) and needs the NDK's vndk/hardware_buffer.h, so it
# is not part of the Linux-side build this checks.
OBJS=$(ninja -C "$B" -t targets all \
  | grep -oE "src/virtio/vulkan/libvulkan_virtio\.so\.p/vn_[a-z_]+\.c\.o" | sort -u)
[ -n "$OBJS" ] || { echo "no guest driver objects found in $B"; exit 1; }
echo "== compile-checking $(printf '%s\n' "$OBJS" | wc -l) guest driver files"

# Remove the objects so ninja recompiles them all, not just the ones touched since last time.
rm -f $(printf '%s\n' "$OBJS" | sed "s|^|$B/|")
if ninja -C "$B" $OBJS > "$WORK/check-build.log" 2>&1; then
  echo "== guest driver compile-check: all passed"
else
  grep -E "error:|fatal error" "$WORK/check-build.log" | head -30
  echo "== guest driver compile-check: FAILED (see $WORK/check-build.log)"
  exit 1
fi
