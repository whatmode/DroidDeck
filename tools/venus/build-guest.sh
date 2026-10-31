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
  src/glx src/egl src/gallium/frontends/dri src/gallium/drivers/zink src/gallium/auxiliary/pipe-loader src/loader src/util src/vulkan/util include/vulkan src/x11 meson.options meson.build \
  include/vk_video src/c11 src/compiler/shader_enums.h src/compiler/spirv/spirv.h \
  include/c99_compat.h include/no_extern_c.h include/renderdoc_app.h
echo "== venus vtest socket handling:"
grep -n "getenv\|SOCKET\|socket_name\|sun_path" "$WORK/mesa/src/virtio/vulkan/vn_renderer_vtest.c" | head -20 || true
grep -rn "VN_DEBUG\|\"vtest\"" "$WORK/mesa/src/virtio/vulkan/vn_common.c" | head -10 || true
meson setup "$WORK/build-mesa" "$WORK/mesa" --buildtype release \
  -Dvulkan-drivers=virtio -Dgallium-drivers= -Dplatforms=x11,wayland \
  -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dopengl=false -Dglx=disabled -Dgbm=disabled \
  -Dllvm=disabled -Dzstd=disabled -Dlibunwind=disabled -Dvalgrind=disabled
ninja -C "$WORK/build-mesa"
# The generated headers (entrypoints, dispatch tables, format tables): with the source above,
# enough to compile-check a driver change outside this build.
(cd "$WORK/build-mesa" && find src -name '*.h' -print0 | tar --null -czf "$OUT/mesa-virtio-gen.tar.gz" -T -)
SO="$WORK/build-mesa/src/virtio/vulkan/libvulkan_virtio.so"
ICD=$(ls "$WORK"/build-mesa/src/virtio/vulkan/virtio_icd.*.json | head -1)
D="$OUT/venus-guest"
rm -rf "$D"; mkdir -p "$D"
# Shipped under a name DroidDeck's importer already accepts (APKs before the bridge only take
# libvulkan_freedreno/panvk/panfrost/lvp); meta.json's backend marks it as the venus ICD.
cp "$SO" "$D/libvulkan_panvk.so"
cp "$ICD" "$D/virtio_icd.aarch64.json"
# The environment this driver needs (present ordering, which decode it carries). The app reads it
# when the driver is selected, so a setting change ships as a driver and not as an APK build or a
# file on the phone. It sits at the zip root, not under gl/, because the GL part below is optional
# and a failed GL build must not take the Vulkan settings with it.
cp "$HERE/guest-env" "$D/env"
[ -s "$D/env" ] || { echo "guest-env is empty"; exit 1; }
SHA=$(git -C "$WORK/mesa" rev-parse HEAD)
# DroidDeck's own commit. The upstream tag alone names every build of this driver the same, so a
# bundle, the driver menu and the session report all show one version for every build and none of
# them can say which one ran. Keep it short: it lands in the driver name, which is the import id.
DD_SHA=$(git -C "$HERE/../.." rev-parse --short=8 HEAD 2>/dev/null || true)
[ -n "$DD_SHA" ] || DD_SHA=${GITHUB_SHA:-}
DD_SHA=${DD_SHA:0:8}
[ -n "$DD_SHA" ] || DD_SHA=unknown
python3 - "$D/meta.json" "$SHA" "$MESA_REF" "$DD_SHA" <<'PY'
import json, sys
out, sha, ref, dd = sys.argv[1:]
json.dump({
    "schemaVersion": 1,
    "kind": "linux-vulkan-icd",
    "name": "DroidDeck-venus-bridge " + ref + " dd." + dd,
    "driverVersion": ref + " " + sha[:8] + " dd." + dd,
    "libc": "glibc",
    "libraryName": "libvulkan_panvk.so",
    "abi": "linux-aarch64-glibc",
    "backend": "venus-vtest",
    "sourceCommit": sha,
    "droiddeckCommit": dd,
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
# The GL part's env file is the one channel every shipped APK already reads, and it only exists
# when the GL build succeeded. Repeat the driver's own settings there so a driver-only install
# applies them on an APK that predates the root env file; the root copy stays the source of truth.
if [ -f "$D/gl/env" ]; then
  {
    echo "# --- the Vulkan driver's own settings (see env at the driver root) ---"
    grep -vE '^[[:space:]]*(#|$)' "$HERE/guest-env" || true
  } >> "$D/gl/env"
fi
(cd "$D" && sha256sum libvulkan_panvk.so > SHA256SUMS.txt && zip -qr "$OUT/Venus-Bridge-guest-${MESA_REF}-${SHA:0:8}-dd${DD_SHA}.zip" .)
ls -la "$OUT"
