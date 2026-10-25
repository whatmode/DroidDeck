#!/bin/bash
# The bridge end to end in Arch Linux ARM: the driver's venus guest (as on the phone) talking
# to a vtest server (Arch's virglrenderer) over lavapipe, as the phone's talks to its server
# over Mali. Game fixes (features DXVK needs that Mali lacks) are checked here with the driver
# told to emulate them as on Mali (DROIDDECK_VN_EMULATE=mali).
set -uo pipefail
REPORT=/tmp/venus-report.txt; : > $REPORT
exec 3>&1
exec > >(tee -a $REPORT) 2>&1
annotate() {
  sleep 1
  printf '::notice title=venus summary::%s\n' "$(grep -E '^PASS|^FAIL|^== ' $REPORT | sed 's/%/%25/g' | cut -c1-200 | awk 'BEGIN{ORS="%0A"}{print}')" >&3
  local n=0
  split -l 70 $REPORT /tmp/venus-chunk.
  for f in /tmp/venus-chunk.*; do
    n=$((n+1)); [ $n -gt 9 ] && break
    printf '::notice title=venus report %d::%s\n' $n "$(sed 's/%/%25/g' "$f" | cut -c1-300 | awk 'BEGIN{ORS="%0A"}{print}')" >&3
  done
}
trap annotate EXIT
grep -q '^DisableSandbox' /etc/pacman.conf || sed -i 's/^\[options\]/[options]\nDisableSandbox/' /etc/pacman.conf
{ for m in https://ca.us.mirror.archlinuxarm.org https://fl.us.mirror.archlinuxarm.org https://de3.mirror.archlinuxarm.org https://nl.mirror.archlinuxarm.org; do echo "Server = $m/\$arch/\$repo"; done; cat /etc/pacman.d/mirrorlist; } > /etc/pacman.d/mirrorlist.new
mv /etc/pacman.d/mirrorlist.new /etc/pacman.d/mirrorlist
pacman -Syu --noconfirm --needed gcc virglrenderer vulkan-swrast vulkan-tools vulkan-headers vulkan-icd-loader \
  glslang spirv-tools unzip >/tmp/pacman.log 2>&1 || { echo "pacman failed"; tail -15 /tmp/pacman.log; exit 1; }
OUT=/tmp/vn; mkdir -p $OUT
zip=$(ls drv/Venus-Bridge-guest-*.zip | head -1)
unzip -q -o "$zip" libvulkan_panvk.so virtio_icd.aarch64.json -d $OUT
sed -i "s|\"library_path\": *\"[^\"]*\"|\"library_path\": \"$OUT/libvulkan_panvk.so\"|" $OUT/virtio_icd.aarch64.json
echo "== virglrenderer $(pacman -Q virglrenderer | cut -d' ' -f2); server: $(command -v virgl_test_server); render server: $(ls /usr/lib/virgl_render_server /usr/libexec/virgl_render_server 2>/dev/null)"
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.aarch64.json
virgl_test_server --venus --multi-clients > $OUT/server.log 2>&1 &
SERVER=$!
for i in $(seq 1 50); do [ -S /tmp/.virgl_test ] && break; sleep 0.2; done
unset VK_DRIVER_FILES
[ -S /tmp/.virgl_test ] || { echo "FAIL no vtest socket"; cat $OUT/server.log | tail -20; exit 1; }
export VK_ICD_FILENAMES=$OUT/virtio_icd.aarch64.json VN_DEBUG=vtest,init STEAM_COMPAT_CLIENT_INSTALL_PATH=/tmp/steamroot
mkdir -p /tmp/steamroot/logs
vulkaninfo --summary > $OUT/vi.txt 2>&1
grep -E "deviceName|driverName|apiVersion" $OUT/vi.txt | head -6
if grep -q "Venus" $OUT/vi.txt; then echo "PASS the bridge lists lavapipe through venus"; else echo "FAIL the bridge lists no GPU"; tail -20 $OUT/vi.txt; tail -20 $OUT/server.log; fi
echo "== server log:"; tail -5 $OUT/server.log
kill $SERVER 2>/dev/null
