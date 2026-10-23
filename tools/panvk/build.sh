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
WORK=${PANVK_WORK:-$OUT/../work}
mkdir -p "$WORK"

# PanVK-side patches from the mali-gl series, in order. The GL-only patches are deliberately
# left out: they touch loader/EGL/kmod code this build does not compile.
# 0003-0004 are upstream Mesa value-correctness fixes the pinned tree predates; see each patch
# for the upstream commit it is ported from.
PANVK_PATCHES=(
  "$ROOT/mali-gl/patches/0002-panvk-csf-compute-tls-size.patch"
  "$ROOT/mali-gl/patches/0003-pan-bi-tls-spill-mem-access.patch"
  "$ROOT/mali-gl/patches/0004-pan-bi-tls-store-helper-analysis.patch"
  "$ROOT/mali-gl/patches/0005-panvk-self-configuring-kbase-profile.patch"
)

# 0005 is a refactor of a later vendor tree and does not apply to every ref (it conflicts on
# vendor main, which reworked the same files). Selecting the set explicitly keeps a patch from
# being skipped silently: a patch named here that does not apply still fails the build.
if [ -n "${PANVK_PATCH_NUMBERS:-}" ]; then
  PANVK_PATCHES=()
  for n in $PANVK_PATCH_NUMBERS; do
    f=$(ls "$ROOT"/mali-gl/patches/"$n"-*.patch 2>/dev/null | head -1)
    [ -n "$f" ] || { echo "== no patch for number $n" >&2; exit 1; }
    PANVK_PATCHES+=("$f")
  done
fi

# A/B control: PANVK_BASELINE=1 builds the vendor series with none of the patches above, so the
# two zips differ only by the PanVK fixes. Needed because the shipped ICD was built elsewhere and
# is not byte-reproducible here; without a same-toolchain baseline, a failure to start cannot be
# attributed to the patches instead of to the rebuild.
VARIANT=""
if [ "${PANVK_BASELINE:-0}" = "1" ]; then
  PANVK_PATCHES=()
  VARIANT="-baseline"
fi

log() { echo "== $*" | tee -a "$OUT/summary.txt"; }
: > "$OUT/summary.txt"

ZB_SHA=$(git -C "$ZB" rev-parse HEAD)
log "zenithblue $ZB_SHA profile $PROFILE"
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

# One profile value per build. 0005 moved the old per-game env switches (bc_compute, no_crc,
# no_bc_emul, no_gs, gpu_prerast, ...) into panvk_kbase_profile_for(), so a behaviour bisection is
# now an edit plus a rebuild. PANVK_PROFILE_KNOB names one field of that initializer and the value
# to give it, e.g. bc_mode=PANVK_BC_MODE_COMPUTE or no_crc=true. The knob is folded into the
# variant, so it reaches the zip name and meta.json and two knob builds are separate driver rows.
KNOB=${PANVK_PROFILE_KNOB:-}
if [ -n "$KNOB" ] && [ "${PANVK_BASELINE:-0}" = "1" ]; then
  # The baseline is the control: it must stay at the vendor defaults, so a knob that would
  # mutate it is refused rather than silently applied.
  log "PANVK_PROFILE_KNOB ignored: PANVK_BASELINE=1 is the unpatched control"
  KNOB=""
fi
if [ -n "$KNOB" ]; then
  HDR="$MESA/src/panfrost/vulkan/panvk_kbase_profile.h"
  [ -f "$HDR" ] || { log "PANVK_PROFILE_KNOB: $HDR missing (needs patch 0005)"; exit 1; }
  python3 "$HERE/profile-knob.py" "$HDR" "$KNOB" || { log "PANVK_PROFILE_KNOB FAILED"; exit 1; }
  log "profile knob: $KNOB"
  VARIANT="$VARIANT-knob-$(printf '%s' "$KNOB" | tr -c 'A-Za-z0-9._-' '-')"
fi
# PANVK_REPORT_LOST=1 makes a lost device print why (tools/panvk/report-lost.py): the reason
# vk_queue_set_lost() records, plus every kernel CSF notification still pending on the fd.
if [ "${PANVK_REPORT_LOST:-0}" = "1" ] && [ "${PANVK_BASELINE:-0}" != "1" ]; then
  python3 "$HERE/report-lost.py" "$MESA" | tee -a "$OUT/summary.txt" || { log "REPORT-LOST FAILED"; exit 1; }
  VARIANT="$VARIANT-lostlog"
fi
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
# On failure, echo the context to stdout as well as the file: the step log is what a `gh run view
# --log` shows, and without this the only copy is an artifact that a failed run never uploads.
[ $rc -eq 0 ] || { grep -nE -B3 -A8 "error:" "$OUT/build.log" | head -400 > "$OUT/errors-context.txt"; cat "$OUT/errors-context.txt"; exit 1; }

SO="$B/src/panfrost/vulkan/libvulkan_panfrost.so"
ICD="$B/src/panfrost/vulkan/panfrost_icd.aarch64.json"
[ -f "$SO" ] || { log "BUILD-FAIL: $SO missing"; exit 1; }
[ -s "$ICD" ] || { log "BUILD-FAIL: ICD manifest missing"; exit 1; }
file "$SO" | grep -q 'ELF 64-bit.*ARM aarch64' || { log "BUILD-FAIL: ICD is not AArch64 ELF"; exit 1; }
readelf --version-info "$SO" | grep -q 'GLIBC_' || { log "BUILD-FAIL: ICD is not glibc-linked"; exit 1; }
readelf -s "$SO" 2>/dev/null | grep -q "kbase_kmod_ops" || { log "BUILD-FAIL: kbase_kmod_ops missing"; exit 1; }

MESA_SHA=$(git -C "$MESA" rev-parse HEAD)
PATCH_ID=$(printf '%s\n' "${PANVK_PATCHES[@]}" | sha256sum | cut -c1-12)

D="$OUT/panvk-glibc-$PROFILE$VARIANT"
rm -rf "$D"
mkdir -p "$D"
cp "$SO" "$D/libvulkan_panfrost.so"
cp "$ICD" "$D/panfrost_icd.aarch64.json"
python3 - "$D/meta.json" "$PROFILE" "$MESA_SHA" "$PATCH_ID" "$VARIANT" "$ZB_SHA" <<'EOF'
import json, sys
out, profile, sha, patch_id, variant, vendor = sys.argv[1:]
# DroidDeck names the driver row from meta.json, not the zip filename, so the variant and the
# vendor ref both have to be here: otherwise the patched/baseline pair, or two refs, are
# indistinguishable rows and the A/B is unreadable.
json.dump({
    "schemaVersion": 1,
    "kind": "linux-vulkan-icd",
    "name": "DroidDeck-panvk-glibc" + variant + " zb" + vendor[:8],
    "driverVersion": "Mesa 26.3.0-devel " + sha[:8],
    "libc": "glibc",
    "libraryName": "libvulkan_panfrost.so",
    "abi": "linux-aarch64-glibc",
    "backend": "panvk-kbase",
    "kernelInterface": "mali_kbase",
    "profile": profile,
    "sourceCommit": sha,
    "patchSeriesId": patch_id,
    "vendorRef": vendor,
}, open(out, "w"), indent=2)
open(out, "a").write("\n")
EOF
(cd "$D" && sha256sum libvulkan_panfrost.so > SHA256SUMS.txt)

# The Mesa pin is the same across vendor refs, so the ref has to be in the name or a
# ref A/B publishes two assets under one name and clobbers the first.
ZIP="$OUT/PanVK-Kbase-$PROFILE$VARIANT-${MESA_SHA:0:8}-zb${ZB_SHA:0:8}.zip"
rm -f "$ZIP"
(cd "$D" && zip -q "$ZIP" libvulkan_panfrost.so panfrost_icd.aarch64.json meta.json SHA256SUMS.txt)
log "package: $(basename "$ZIP") $(du -h "$ZIP" | cut -f1) sha256 $(sha256sum "$ZIP" | cut -d' ' -f1)"
exit 0
