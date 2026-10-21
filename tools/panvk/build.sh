#!/bin/bash
# build.sh - Build the patched PanVK kbase glibc ICD and package it as a DroidDeck-importable
# driver zip. DroidDeck ships this ICD as a user-imported Linux Vulkan driver component, so it
# needs its own build: mali-gl/scripts/build.sh builds the GL stack only (-Dvulkan-drivers=).
# Usage: build.sh <zenithblue checkout> <out dir>
# The vendor series is applied first, then the PanVK patches from mali-gl/patches, so the ICD is
# the shipped driver plus the fixes below.
set -uo pipefail
ZB=$(cd "$1" && pwd)
OUT=$(mkdir -p "$2" && cd "$2" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
PROFILE=${PROFILE:-g615-v11-csf}
WORK=$OUT/../work
mkdir -p "$WORK"

# PanVK-side patches from the mali-gl series, in order. The GL-only patches are deliberately
# left out: they touch loader/EGL/kmod code this build does not compile.
PANVK_PATCHES=(
  "$ROOT/mali-gl/patches/0002-panvk-csf-compute-tls-size.patch"
)

log() { echo "== $*" | tee -a "$OUT/summary.txt"; }
: > "$OUT/summary.txt"

log "zenithblue $(git -C "$ZB" rev-parse HEAD) profile $PROFILE"
"$ZB/scripts/fetch-mesa.sh" "$WORK" > "$OUT/fetch.log" 2>&1 || { log "FETCH FAILED"; tail -20 "$OUT/fetch.log"; exit 1; }
MESA=$WORK/mesa
log "mesa $(git -C "$MESA" rev-parse HEAD)"
"$ZB/scripts/apply-patches.sh" --profile "$PROFILE" --mesa "$MESA" > "$OUT/apply.log" 2>&1 \
  || { log "zb PATCHES FAILED: $(grep -m1 PATCH-DRIFT "$OUT/apply.log")"; exit 1; }
git -C "$MESA" add -A >/dev/null 2>&1
git -C "$MESA" -c user.name=ci -c user.email=ci@localhost commit -qm "kbase series" >/dev/null 2>&1

for p in "${PANVK_PATCHES[@]}"; do
  if git -C "$MESA" apply "$p" >> "$OUT/apply.log" 2>&1; then
    log "panvk patch: $(basename "$p") applied"
  else
    log "panvk patch: $(basename "$p") FAILED"
    git -C "$MESA" apply --reject "$p" >> "$OUT/apply.log" 2>&1 || true
    tail -30 "$OUT/apply.log" | tee -a "$OUT/summary.txt"
    exit 1
  fi
done

B=$WORK/build-icd
rm -rf "$B"
if ! meson setup "$B" "$MESA" \
    -Dbuildtype=release \
    -Dplatforms=x11,wayland \
    -Dgallium-drivers= \
    -Dvulkan-drivers=panfrost \
    -Dpanfrost-kmds=kbase \
    -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dopengl=false \
    -Dglx=disabled -Dgbm=disabled -Dzstd=disabled -Dlibunwind=disabled \
    > "$OUT/configure.log" 2>&1; then
  log "configure: FAILED"
  grep -nE "ERROR|error" "$OUT/configure.log" | head -20 | tee -a "$OUT/summary.txt"
  exit 1
fi
log "configure: OK"

ninja -C "$B" > "$OUT/build.log" 2>&1
rc=$?
grep -nE "error:|undefined reference" "$OUT/build.log" > "$OUT/errors.txt" || true
log "build: ninja rc=$rc, $(wc -l < "$OUT/errors.txt") error lines"
[ $rc -eq 0 ] || { grep -nE -B3 -A8 "error:" "$OUT/build.log" | head -400 > "$OUT/errors-context.txt"; exit 1; }

SO="$B/src/panfrost/vulkan/libvulkan_panfrost.so"
ICD="$B/src/panfrost/vulkan/panfrost_icd.aarch64.json"
[ -f "$SO" ] || { log "BUILD-FAIL: $SO missing"; exit 1; }
[ -s "$ICD" ] || { log "BUILD-FAIL: ICD manifest missing"; exit 1; }
file "$SO" | grep -q 'ELF 64-bit.*ARM aarch64' || { log "BUILD-FAIL: ICD is not AArch64 ELF"; exit 1; }
readelf --version-info "$SO" | grep -q 'GLIBC_' || { log "BUILD-FAIL: ICD is not glibc-linked"; exit 1; }
readelf -s "$SO" 2>/dev/null | grep -q "kbase_kmod_ops" || { log "BUILD-FAIL: kbase_kmod_ops missing"; exit 1; }

MESA_SHA=$(git -C "$MESA" rev-parse HEAD)
PATCH_ID=$(sha256sum "${PANVK_PATCHES[@]}" | sha256sum | cut -c1-12)

D="$OUT/panvk-glibc-$PROFILE"
rm -rf "$D"
mkdir -p "$D"
cp "$SO" "$D/libvulkan_panfrost.so"
cp "$ICD" "$D/panfrost_icd.aarch64.json"
python3 - "$D/meta.json" "$PROFILE" "$MESA_SHA" "$PATCH_ID" <<'EOF'
import json, sys
out, profile, sha, patch_id = sys.argv[1:]
json.dump({
    "schemaVersion": 1,
    "kind": "linux-vulkan-icd",
    "name": "DroidDeck-panvk-glibc",
    "driverVersion": "Mesa 26.3.0-devel",
    "libc": "glibc",
    "libraryName": "libvulkan_panfrost.so",
    "abi": "linux-aarch64-glibc",
    "backend": "panvk-kbase",
    "kernelInterface": "mali_kbase",
    "profile": profile,
    "sourceCommit": sha,
    "patchSeriesId": patch_id,
}, open(out, "w"), indent=2)
open(out, "a").write("\n")
EOF
(cd "$D" && sha256sum libvulkan_panfrost.so > SHA256SUMS.txt)

ZIP="$OUT/PanVK-Kbase-$PROFILE-${MESA_SHA:0:8}.zip"
rm -f "$ZIP"
(cd "$D" && zip -q "$ZIP" libvulkan_panfrost.so panfrost_icd.aarch64.json meta.json SHA256SUMS.txt)
log "package: $(basename "$ZIP") $(du -h "$ZIP" | cut -f1) sha256 $(sha256sum "$ZIP" | cut -d' ' -f1)"
exit 0
