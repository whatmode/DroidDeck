#!/usr/bin/env bash
# build-guest-gl.sh <out dir> - the GL part of the venus bridge driver: Zink (OpenGL on Vulkan)
# and Mesa's GLX/EGL, from the same Mesa source and patches as the venus ICD (build-guest.sh,
# which must have run first with the same VENUS_WORK). It goes into the driver zip under gl/:
#   libGLX_droiddeck.so.0, libEGL_droiddeck.so.0  glvnd vendor libraries (vendor "droiddeck",
#                                                 so they never clash with the system's Mesa)
#   libgallium-*.so and the rest Mesa needs        found next to them (RUNPATH $ORIGIN)
#   50_droiddeck.json                              glvnd EGL vendor file
#   gl.json                                        what the session needs to know
# The session uses it only when the selected driver carries it.
set -euo pipefail
OUT=$(mkdir -p "$1" && cd "$1" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/source.env"
WORK=${VENUS_WORK:?VENUS_WORK must point at build-guest.sh\'s work dir}
[ -d "$WORK/mesa" ] || { echo "no Mesa source in $WORK (run build-guest.sh first)"; exit 1; }
B="$WORK/build-gl"
rm -rf "$B"
meson setup "$B" "$WORK/mesa" --buildtype release --prefix /usr \
  -Dvulkan-drivers= -Dgallium-drivers=zink -Dplatforms=x11,wayland \
  -Dopengl=true -Dglx=dri -Dglvnd=enabled -Dglvnd-vendor-name=droiddeck \
  -Degl=enabled -Dgles1=disabled -Dgles2=enabled -Dgbm=disabled \
  -Dllvm=disabled -Dzstd=disabled -Dlibunwind=disabled -Dvalgrind=disabled \
  -Dlmsensors=disabled -Dbuild-tests=false
ninja -C "$B"
D="$OUT/venus-gl"
rm -rf "$D"; mkdir -p "$D"
DESTDIR="$WORK/gl-install" ninja -C "$B" install >/dev/null
I="$WORK/gl-install/usr"
echo "== installed GL files:"; (cd "$I" && find . -type f -o -type l | sort)
# Every shared library Mesa installed, flattened into gl/ (symlinks resolved).
find "$I" \( -name '*.so' -o -name '*.so.*' \) \( -type f -o -type l \) | while read -r f; do
  cp -L "$f" "$D/$(basename "$f")"
done
# One copy each: the names glvnd and the loader look for.
rm -f "$D"/lib*_droiddeck.so "$D"/lib*_droiddeck.so.0.0.0
for f in "$D"/*.so*; do patchelf --set-rpath '$ORIGIN' "$f"; done
egl_json=$(find "$I" -name '*droiddeck*.json' | head -1)
python3 - "$D" "$MESA_REF" "${egl_json:-}" <<'PY'
import json, os, sys
d, ref, egl = sys.argv[1:]
json.dump({"file_format_version": "1.0.0", "ICD": {"library_path": "libEGL_droiddeck.so.0"}},
          open(os.path.join(d, "50_droiddeck.json"), "w"), indent=2)
libs = sorted(os.listdir(d))
json.dump({
    "schemaVersion": 1,
    "kind": "linux-gl",
    "glvndVendor": "droiddeck",
    "glx": "libGLX_droiddeck.so.0",
    "egl": "50_droiddeck.json",
    "galliumDriver": "zink",
    "mesa": ref,
    "files": libs,
}, open(os.path.join(d, "gl.json"), "w"), indent=2)
PY
# What the session sets when this driver is selected (read by the app; @GL@ is this folder).
cat > "$D/env" <<'ENV'
# DroidDeck reads this at session start when the driver is selected. @GL@ is this folder.
# GL (GLX and EGL) from this driver's Mesa instead of the system's: glvnd vendor "droiddeck".
__GLX_VENDOR_LIBRARY_NAME=droiddeck
__EGL_VENDOR_LIBRARY_FILENAMES=@GL@/50_droiddeck.json
LD_LIBRARY_PATH+=@GL@
BL_DRIVER_GL=@GL@
ENV
echo "== GL part:"; ls -la "$D"
for f in "$D"/libGLX_droiddeck.so.0 "$D"/libEGL_droiddeck.so.0; do
  [ -e "$f" ] || { echo "missing $(basename "$f")"; exit 1; }
  echo "== $(basename "$f") needs:"; readelf -d "$f" | grep -E "NEEDED|RUNPATH"
done
