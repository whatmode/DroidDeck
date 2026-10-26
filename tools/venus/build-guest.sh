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
# DroidDeck changes to the guest ICD (vtest dma-buf import, so a compositor can show other
# programs' windows).
for p in "$HERE"/guest-patches/*.patch; do
  [ -e "$p" ] || continue
  echo "== applying $(basename "$p")"
  git -C "$WORK/mesa" apply --whitespace=nowarn "$p"
done
# Also the GL side (GLX, EGL, the DRI frontend, Zink, the loader): read when patching it.
tar -C "$WORK/mesa" -czf "$OUT/mesa-virtio-src.tar.gz" src/virtio src/vulkan/runtime src/vulkan/wsi include/drm-uapi \
  src/glx src/egl src/gallium/frontends/dri src/gallium/drivers/zink src/gallium/auxiliary/pipe-loader src/loader src/util src/vulkan/util include/vulkan src/x11 meson.options meson.build
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
# Shipped under a name DroidDeck's importer already accepts (APKs before the bridge only take
# libvulkan_freedreno/panvk/panfrost/lvp); meta.json's backend marks it as the venus ICD.
cp "$SO" "$D/libvulkan_panvk.so"
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
    "libraryName": "libvulkan_panvk.so",
    "abi": "linux-aarch64-glibc",
    "backend": "venus-vtest",
    "sourceCommit": sha,
}, open(out, "w"), indent=2)
PY
# The GL part (build-guest-gl.sh) goes in under gl/. A failed GL build leaves the Vulkan
# driver as it was.
if VENUS_WORK="$WORK" bash "$HERE/build-guest-gl.sh" "$OUT" > "$OUT/gl-build.log" 2>&1; then
  echo "== GL part built"
else
  echo "== GL BUILD FAILED"; tail -40 "$OUT/gl-build.log"; rm -rf "$OUT/venus-gl"
fi
if [ -d "$OUT/venus-gl" ]; then rm -rf "$D/gl"; cp -r "$OUT/venus-gl" "$D/gl"; fi
(cd "$D" && sha256sum libvulkan_panvk.so > SHA256SUMS.txt && zip -qr "$OUT/Venus-Bridge-guest-${MESA_REF}-${SHA:0:8}.zip" .)
ls -la "$OUT"
