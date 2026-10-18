#!/bin/bash
# probe.sh - Phase 0: how far is Panfrost GL/EGL/GBM from running on kbase?
# Usage: probe.sh <zenithblue checkout> <report dir>
# Fetches the pinned Mesa, applies the PanVK kbase series, scans the GL side for DRM/panthor
# dependencies, then configures and builds Panfrost gallium + EGL + GBM + GLX with only the
# kbase KMD enabled (ninja -k 0, so every compile error is collected).
set -uo pipefail
ZB=$(cd "$1" && pwd)
OUT=$(mkdir -p "$2" && cd "$2" && pwd)
PROFILE=${PROFILE:-g615-v11-csf}
WORK=$OUT/../work
mkdir -p "$WORK"

log() { echo "== $*" | tee -a "$OUT/summary.txt"; }
: > "$OUT/summary.txt"

log "zenithblue $(git -C "$ZB" rev-parse HEAD) profile $PROFILE"
"$ZB/scripts/fetch-mesa.sh" "$WORK" > "$OUT/fetch.log" 2>&1 || { log "FETCH FAILED"; tail -20 "$OUT/fetch.log"; exit 1; }
MESA=$WORK/mesa
log "mesa $(git -C "$MESA" rev-parse HEAD)"
if "$ZB/scripts/apply-patches.sh" --profile "$PROFILE" --mesa "$MESA" > "$OUT/apply.log" 2>&1; then
  log "patches: $(tail -1 "$OUT/apply.log")"
else
  log "PATCHES FAILED: $(grep -m1 PATCH-DRIFT "$OUT/apply.log")"
  exit 1
fi
git -C "$MESA" add -A >/dev/null 2>&1
git -C "$MESA" -c user.name=probe -c user.email=probe@localhost commit -qm "kbase series" >/dev/null 2>&1

# --- static scan -------------------------------------------------------------------------
{
  echo "### kbase backend files"
  find "$MESA/src/panfrost" -iname '*kbase*' | sed "s#$MESA/##" | sort
  echo
  echo "### pan_kmod backend selection (how a fd picks its backend)"
  grep -rn "pan_kmod_dev_create\|kbase_kmod_ops\|panthor_kmod_ops\|drmGetVersion" \
    "$MESA/src/panfrost/lib/kmod" | sed "s#$MESA/##"
  echo
  echo "### Direct DRM / panthor uAPI use outside pan_kmod in the GL driver (gallium)"
  grep -rnE "drmIoctl|DRM_IOCTL_|drmSyncobj|drmPrime|panthor_|PANTHOR_|drm_panthor|drm_panfrost|DRM_PANFROST" \
    "$MESA/src/gallium/drivers/panfrost" | sed "s#$MESA/##"
  echo
  echo "### Same, in src/panfrost/lib outside kmod"
  grep -rnE "drmIoctl|DRM_IOCTL_|drmSyncobj|drmPrime" "$MESA/src/panfrost/lib" | grep -v "/kmod/" | sed "s#$MESA/##"
  echo
  echo "### How PanVK's kbase series replaced its direct submission (for reuse in gallium)"
  ls "$ZB/patches/csf" "$ZB/patches/kbase-common"
  echo
  echo "### Gallium winsys entry for panfrost and loader driver mapping"
  grep -rn "panfrost" "$MESA/src/gallium/winsys" "$MESA/src/loader" 2>/dev/null | grep -iE "create|driver|kmsro|name" | sed "s#$MESA/##" | head -60
  echo
  echo "### pipe-loader / loader: how a driver name is derived from a fd"
  grep -rn "loader_get_driver_for_fd\|drmGetVersion" "$MESA/src/loader" "$MESA/src/gallium/auxiliary/pipe-loader" | sed "s#$MESA/##"
  echo
  echo "### GBM dri backend device open"
  grep -rn "dri_screen_create\|loader_get_driver_for_fd\|driver_name" "$MESA/src/gbm/backends/dri" 2>/dev/null | sed "s#$MESA/##" | head -30
} > "$OUT/scan.txt" 2>&1
log "scan: $(grep -cE 'drmIoctl|DRM_IOCTL_|drmSyncobj' "$OUT/scan.txt") direct DRM call sites listed in scan.txt"

# --- configure + build -------------------------------------------------------------------
B=$WORK/build-gl
rm -rf "$B"
if meson setup "$B" "$MESA" \
    -Dbuildtype=debugoptimized \
    -Dplatforms=x11,wayland \
    -Dgallium-drivers=panfrost \
    -Dvulkan-drivers=panfrost \
    -Dpanfrost-kmds=kbase \
    -Degl=enabled -Dgbm=enabled -Dopengl=true -Dgles2=enabled -Dgles1=disabled \
    -Dglx=dri -Dllvm=enabled -Dmesa-clc=enabled -Dprecomp-compiler=enabled \
    -Dzstd=disabled -Dlibunwind=disabled -Dvalgrind=disabled \
    > "$OUT/configure.log" 2>&1; then
  log "configure: OK"
else
  log "configure: FAILED"
  grep -nE "ERROR|error" "$OUT/configure.log" | head -20 | tee -a "$OUT/summary.txt"
  exit 0
fi
ninja -C "$B" -k 0 > "$OUT/build.log" 2>&1
rc=$?
grep -nE "error:|undefined reference" "$OUT/build.log" > "$OUT/errors.txt" || true
log "build: ninja rc=$rc, $(wc -l < "$OUT/errors.txt") error lines (errors.txt)"
{
  echo "### error lines grouped by file"
  grep -oE "\.\./[^:]+:[0-9]+:[0-9]+: error" "$OUT/build.log" | sed 's/:[0-9]*:[0-9]*: error//' | sort | uniq -c | sort -rn
  echo
  echo "### undefined references"
  grep -oE "undefined reference to \`[^']+'" "$OUT/build.log" | sort | uniq -c | sort -rn
} > "$OUT/errors-by-file.txt"
for f in libgallium libEGL libgbm libGLX_mesa dri_gbm libvulkan_panfrost; do
  hit=$(find "$B" -name "$f*.so*" -type f 2>/dev/null | head -1)
  log "artifact $f: ${hit:+built}${hit:-missing}"
done
exit 0
