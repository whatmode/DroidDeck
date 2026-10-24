#!/usr/bin/env bash
# build-server.sh <out dir> - virglrenderer's vtest server with Venus, for Android arm64.
# It runs inside DroidDeck, executes the Linux side's Vulkan on Android's own Vulkan driver
# (libvulkan.so) and talks to the guest's Mesa venus ICD over a Unix socket.
set -euo pipefail
OUT=$(mkdir -p "$1" && cd "$1" && pwd)
: "${NDK:?set NDK to the Android NDK root}"
API=29
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/source.env"
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
[ -d "$TC" ] || TC="$NDK/toolchains/llvm/prebuilt/linux-aarch64/bin"
WORK=${VENUS_WORK:-$(mktemp -d)}
PREFIX="$WORK/prefix"
mkdir -p "$PREFIX/lib/pkgconfig"
SYSROOT="$TC/../sysroot"

cat > "$WORK/android.cross" <<CROSS
[binaries]
c = '$TC/aarch64-linux-android$API-clang'
cpp = '$TC/aarch64-linux-android$API-clang++'
ar = '$TC/llvm-ar'
strip = '$TC/llvm-strip'
pkg-config = 'pkg-config'

[built-in options]
c_args = ['-fPIC']
cpp_args = ['-fPIC']

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
CROSS

# The NDK ships libEGL/libGLESv2/libvulkan stubs but no .pc files; describe them so meson finds them.
for lib in egl:EGL glesv2:GLESv2 vulkan:vulkan; do
  name=${lib%%:*}; so=${lib##*:}
  cat > "$PREFIX/lib/pkgconfig/$name.pc" <<PC
Name: $name
Description: Android NDK $so
Version: 1.4.0
Libs: -l$so
Cflags:
PC
done
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=

fetch() { [ -d "$WORK/$1" ] || git clone -q --depth 1 --branch "$3" "$2" "$WORK/$1"; }
setup() { meson setup "$WORK/build-$1" "$WORK/$1" --cross-file "$WORK/android.cross" --prefix "$PREFIX" \
            --libdir lib --buildtype release --default-library shared "${@:2}"; }

fetch libdrm "$LIBDRM_REPO" "$LIBDRM_REF"
setup libdrm -Dintel=disabled -Dradeon=disabled -Damdgpu=disabled -Dnouveau=disabled -Dvmwgfx=disabled \
  -Dfreedreno=disabled -Dvc4=disabled -Detnaviv=disabled -Dexynos=disabled -Domap=disabled \
  -Dtegra=disabled -Dcairo-tests=disabled -Dman-pages=disabled -Dvalgrind=disabled -Dtests=false -Dinstall-test-programs=false
ninja -C "$WORK/build-libdrm" install

fetch libepoxy "$LIBEPOXY_REPO" "$LIBEPOXY_REF"
setup libepoxy -Degl=yes -Dglx=no -Dx11=false -Dtests=false -Ddocs=false
ninja -C "$WORK/build-libepoxy" install

fetch virglrenderer "$VIRGLRENDERER_REPO" "$VIRGLRENDERER_REF"
echo "== virglrenderer meson_options:"; grep -n "option(" "$WORK/virglrenderer/meson_options.txt" | cut -c1-160
echo "== virglrenderer gbm/platform lines:"; grep -n "gbm\|platforms\|with_egl\|with_glx\|epoxy" "$WORK/virglrenderer/meson.build" | cut -c1-160
# Android has no DRM render node or GBM, so no EGL/GLX platform: this server only runs Venus.
# On a configure failure, print the meson options and the lines around the error for the CI log.
setup virglrenderer -Dplatforms= -Dvenus=true -Drender-server=false -Dvideo=false -Dtests=false \
  -Dfuzzer=false -Dminigbm_allocation=false -Dcheck-gl-errors=false
ninja -C "$WORK/build-virglrenderer" install || { echo "== virglrenderer build failed"; exit 1; }

mkdir -p "$OUT/venus-server"
cp "$PREFIX/bin/virgl_test_server" "$OUT/venus-server/"
cp "$PREFIX/lib/libvirglrenderer.so" "$PREFIX/lib/libepoxy.so" "$PREFIX/lib/libdrm.so" "$OUT/venus-server/" 2>/dev/null || true
cp "$PREFIX"/lib/libvirglrenderer.so* "$PREFIX"/lib/libepoxy.so* "$PREFIX"/lib/libdrm.so* "$OUT/venus-server/" 2>/dev/null || true
"$TC/llvm-strip" "$OUT/venus-server/"* 2>/dev/null || true
ls -la "$OUT/venus-server"
