#!/usr/bin/bash
# Build Xwayland (Arch's current version) with the mali-gl patches, inside
# menci/archlinuxarm:base-devel.  Output: out-xwayland/Xwayland + summary.
set -euxo pipefail
WORK=${WORK:-/work}
cd "$WORK"
grep -q '^DisableSandbox' /etc/pacman.conf || sed -i 's/^\[options\]/[options]\nDisableSandbox/' /etc/pacman.conf
{ for m in https://ca.us.mirror.archlinuxarm.org https://fl.us.mirror.archlinuxarm.org https://de3.mirror.archlinuxarm.org https://nl.mirror.archlinuxarm.org; do echo "Server = $m/\$arch/\$repo"; done; cat /etc/pacman.d/mirrorlist; } > /etc/pacman.d/mirrorlist.new
mv /etc/pacman.d/mirrorlist.new /etc/pacman.d/mirrorlist
pacman -Syu --noconfirm --needed git meson ninja pkgconf xorgproto xtrans libxkbfile dbus xorg-font-util \
  wayland-protocols mesa libglvnd systemd nettle libepoxy libxfont2 pixman libxcvt libxau wayland \
  libdrm libtirpc libei libxshmfence libdecor
ver=$(pacman -Si xorg-xwayland | sed -n 's/^Version *: *\([0-9.]*\).*/\1/p' | head -1)
echo "xwayland $ver" | tee out-summary.txt
rm -rf xw && mkdir xw && cd xw
curl -fsSL "https://xorg.freedesktop.org/archive/individual/xserver/xwayland-$ver.tar.xz" | tar xJ
cd "xwayland-$ver"
for p in "$WORK"/mali-gl/patches-xwayland/*.patch; do
  patch -p1 --no-backup-if-mismatch < "$p"
  echo "applied $(basename "$p")" | tee -a "$WORK/out-summary.txt"
done
meson setup build --prefix=/usr --buildtype=plain -Db_ndebug=true \
  -Dipv6=true -Dxvfb=false -Dxdmcp=false -Dxcsecurity=true -Ddri3=true -Dglamor=true \
  -Dlibdecor=true -Dxkb_dir=/usr/share/X11/xkb -Dxkb_output_dir=/var/lib/xkb
ninja -C build
cd "$WORK"
rm -rf out-xwayland && mkdir out-xwayland
cp "xw/xwayland-$ver/build/hw/xwayland/Xwayland" out-xwayland/Xwayland
strip --strip-unneeded out-xwayland/Xwayland
readelf -d out-xwayland/Xwayland | sed -n 's/.*NEEDED.*\[\(.*\)\]/\1/p' | tr '\n' ' ' | sed 's/^/NEEDED: /' | tee -a out-summary.txt
echo | tee -a out-summary.txt
