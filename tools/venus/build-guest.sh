#!/usr/bin/env bash
# build-guest.sh <out dir> - Mesa's venus Vulkan ICD for the Linux side (glibc aarch64), packaged
# as a DroidDeck-importable driver zip. It does no GPU work: every call goes over the vtest
# socket to the server, which runs it on Android's driver.
set -euo pipefail
OUT=$(mkdir -p "$1" && cd "$1" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/source.env"
WORK=${VENUS_WORK:-$(mktemp -d)}
[ -d "$WORK/mesa" ] || git clone -q --depth 1 --branch "$MESA_REF" "$MESA_REPO" "$WORK/mesa"
echo "== venus vtest socket handling:"
grep -n "getenv\|SOCKET\|socket_name\|sun_path" "$WORK/mesa/src/virtio/vulkan/vn_renderer_vtest.c" | head -20 || true
grep -rn "VN_DEBUG\|\"vtest\"" "$WORK/mesa/src/virtio/vulkan/vn_common.c" | head -10 || true
meson setup "$WORK/build-mesa" "$WORK/mesa" --buildtype release \
  -Dvulkan-drivers=virtio -Dgallium-drivers= -Dplatforms=x11,wayland \
  -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dopengl=false -Dglx=disabled -Dgbm=disabled \
  -Dllvm=disabled -Dzstd=disabled -Dlibunwind=disabled -Dvalgrind=disabled
ninja -C "$WORK/build-mesa"
SO="$WORK/build-mesa/src/virtio/vulkan/libvulkan_virtio.so"
ICD=$(ls "$WORK"/build-mesa/src/virtio/vulkan/virtio_icd.*.json | head -1)
D="$OUT/venus-guest"
rm -rf "$D"; mkdir -p "$D"
cp "$SO" "$D/libvulkan_virtio.so"
cp "$ICD" "$D/virtio_icd.aarch64.json"
SHA=$(git -C "$WORK/mesa" rev-parse HEAD)
python3 - "$D/meta.json" "$SHA" "$MESA_REF" <<'PY'
import json, sys
out, sha, ref = sys.argv[1:]
json.dump({
    "schemaVersion": 1,
    "kind": "linux-vulkan-icd",
    "name": "DroidDeck-venus-bridge " + ref,
    "driverVersion": ref + " " + sha[:8],
    "libc": "glibc",
    "libraryName": "libvulkan_virtio.so",
    "abi": "linux-aarch64-glibc",
    "backend": "venus-vtest",
    "sourceCommit": sha,
}, open(out, "w"), indent=2)
PY
(cd "$D" && sha256sum libvulkan_virtio.so > SHA256SUMS.txt && zip -q "$OUT/Venus-Bridge-guest-${MESA_REF}-${SHA:0:8}.zip" *)
ls -la "$OUT"
