#!/bin/bash
# build.sh - Phase 1: build Panfrost GL/EGL/GBM for kbase with the mali-gl patches and
# package it with the offscreen GLES test.
# Usage: build.sh <zenithblue checkout> <out dir>
set -uo pipefail
ZB=$(cd "$1" && pwd)
OUT=$(mkdir -p "$2" && cd "$2" && pwd)
HERE=$(cd "$(dirname "$0")/.." && pwd)
PROFILE=${PROFILE:-g615-v11-csf}
PREFIX=/opt/mali-gl
WORK=$OUT/../work
mkdir -p "$WORK"

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

for p in "$HERE"/patches/*.patch; do
  if git -C "$MESA" apply --index "$p" >> "$OUT/apply.log" 2>&1; then
    log "mali-gl patch: $(basename "$p") applied"
  else
    log "mali-gl patch: $(basename "$p") FAILED"
    git -C "$MESA" apply --reject "$p" >> "$OUT/apply.log" 2>&1 || true
    tail -30 "$OUT/apply.log" | tee -a "$OUT/summary.txt"
    exit 1
  fi
done

B=$WORK/build
rm -rf "$B"
if ! meson setup "$B" "$MESA" \
    --prefix=$PREFIX --libdir=lib \
    -Dbuildtype=debugoptimized -Db_ndebug=true \
    -Dplatforms=x11,wayland \
    -Dgallium-drivers=panfrost -Dvulkan-drivers= \
    -Dpanfrost-kmds=kbase \
    -Degl=enabled -Dgbm=enabled -Dopengl=true -Dgles2=enabled -Dgles1=disabled \
    -Dglx=dri -Dglvnd=disabled \
    -Dllvm=enabled -Ddraw-use-llvm=false -Dmesa-clc=enabled -Dprecomp-compiler=enabled \
    -Dzstd=disabled -Dlibunwind=disabled -Dvalgrind=disabled -Dlmsensors=disabled \
    -Dxmlconfig=disabled -Dexpat=disabled \
    > "$OUT/configure.log" 2>&1; then
  log "configure: FAILED"
  grep -nE "ERROR|error" "$OUT/configure.log" | head -20 | tee -a "$OUT/summary.txt"
  exit 1
fi
log "configure: OK"

ninja -C "$B" -k 0 > "$OUT/build.log" 2>&1
rc=$?
grep -nE "error:|undefined reference" "$OUT/build.log" > "$OUT/errors.txt" || true
grep -nE "warning:" "$OUT/build.log" | grep -E "pan_csf|pan_fence|pan_context|pan_device|loader\.c|pan_kmod\.c" > "$OUT/warnings-ours.txt" || true
log "build: ninja rc=$rc, $(wc -l < "$OUT/errors.txt") error lines, $(wc -l < "$OUT/warnings-ours.txt") warnings in patched files"
[ $rc -eq 0 ] || { grep -nE -B3 -A8 "error:" "$OUT/build.log" | head -400 > "$OUT/errors-context.txt"; exit 1; }

D=$WORK/destdir
rm -rf "$D"
DESTDIR=$D ninja -C "$B" install > "$OUT/install.log" 2>&1 || { log "install FAILED"; tail -20 "$OUT/install.log"; exit 1; }
R=$D$PREFIX
log "installed: $(cd "$R" && find . -name '*.so*' -type f | sort | tr '\n' ' ')"

# test binary, linked against the freshly built libraries
mkdir -p "$R/bin"
if gcc -O2 -g -funwind-tables -Wall -o "$R/bin/gltest" "$HERE/test/gltest.c" \
     -I"$R/include" $(pkg-config --cflags libdrm) -L"$R/lib" -Wl,-rpath-link,"$R/lib" -lEGL -lGLESv2 -lgbm > "$OUT/gltest-build.log" 2>&1; then
  log "gltest: built"
else
  log "gltest: BUILD FAILED"; cat "$OUT/gltest-build.log" | tee -a "$OUT/summary.txt"; exit 1
fi
install -m755 "$HERE/test/run-gltest.sh" "$R/run-gltest.sh"
if gcc -O2 -g -funwind-tables -Wall -o "$R/bin/x11test" "$HERE/test/x11test.c" \
     -I"$R/include" -L"$R/lib" -Wl,-rpath-link,"$R/lib" -lEGL -lGLESv2 -lX11 -lX11-xcb -lxcb -lxcb-dri3 >> "$OUT/gltest-build.log" 2>&1; then
  log "x11test: built"
else
  log "x11test: BUILD FAILED"; tail -20 "$OUT/gltest-build.log" | tee -a "$OUT/summary.txt"; exit 1
fi
install -m755 "$HERE/test/run-x11test.sh" "$R/run-x11test.sh"
if gcc -O2 -g -Wall -o "$R/bin/glprobe" "$HERE/test/glprobe.c" \
     -I"$R/include" -L"$R/lib" -Wl,-rpath-link,"$R/lib" -lEGL -lGL -lX11 >> "$OUT/gltest-build.log" 2>&1; then
  log "glprobe: built"
else
  log "glprobe: BUILD FAILED"; tail -20 "$OUT/gltest-build.log" | tee -a "$OUT/summary.txt"; exit 1
fi

# patched gamescope (built in Arch Linux ARM by the gamescope job)
if [ -n "${GAMESCOPE_BIN:-}" ] && [ -f "$GAMESCOPE_BIN" ]; then
  install -m755 "$GAMESCOPE_BIN" "$R/bin/gamescope"
  log "gamescope: packaged"
else
  log "gamescope: not provided"
fi

# patched Xwayland (built in Arch Linux ARM by the xwayland job)
if [ -n "${XWAYLAND_BIN:-}" ] && [ -f "$XWAYLAND_BIN" ]; then
  install -m755 "$XWAYLAND_BIN" "$R/bin/Xwayland"
  [ -f "${XWAYLAND_SUMMARY:-}" ] && sed 's/^/xwayland job: /' "$XWAYLAND_SUMMARY" | tee -a "$OUT/summary.txt"
  log "Xwayland: packaged"
else
  log "Xwayland: not provided"
fi

# runtime dependencies other than the ones we ship (must exist in the DroidDeck rootfs)
{
  for f in $(find "$R" -type f \( -name '*.so*' -o -path '*/bin/*' \)); do
    readelf -d "$f" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'
  done
} | sort -u > "$OUT/needed.txt"
ours=$(cd "$R/lib" && ls)
grep -vxF -f <(printf '%s\n' $ours) "$OUT/needed.txt" > "$OUT/needed-external.txt" || true
log "external sonames: $(tr '\n' ' ' < "$OUT/needed-external.txt")"
grep -q libLLVM "$OUT/needed-external.txt" && log "WARNING: links libLLVM"
log "max GLIBC symbol version: $(for f in $(find "$R" -type f \( -name '*.so*' -o -path '*/bin/*' \)); do objdump -T "$f" 2>/dev/null; done | grep -oE 'GLIBC_[0-9.]+' | sort -V -u | tail -1)"

rm -rf "$R/include" "$R/lib/pkgconfig"
echo "mali-gl ${GITHUB_SHA:-local} mesa $(git -C "$MESA" rev-parse --short HEAD)" > "$R/VERSION"
tar -C "$D/opt" -czf "$OUT/mali-gl-aarch64.tar.gz" mali-gl
log "package: mali-gl-aarch64.tar.gz $(du -h "$OUT/mali-gl-aarch64.tar.gz" | cut -f1)"

# runtime package for the apk: stripped, same layout
RT=$WORK/runtime/mali-gl
rm -rf "$WORK/runtime" && mkdir -p "$WORK/runtime" && cp -a "$R" "$RT"
find "$RT" -type f \( -name '*.so*' -o -path '*/bin/*' \) -exec strip --strip-unneeded {} + 2>/dev/null || true
tar -C "$WORK/runtime" -czf "$OUT/mali-gl-runtime-aarch64.tar.gz" mali-gl
sha256sum "$OUT/mali-gl-runtime-aarch64.tar.gz" | cut -d' ' -f1 > "$OUT/runtime.sha256"
log "runtime package: mali-gl-runtime-aarch64.tar.gz $(du -h "$OUT/mali-gl-runtime-aarch64.tar.gz" | cut -f1) sha256 $(cat "$OUT/runtime.sha256")"
exit 0
