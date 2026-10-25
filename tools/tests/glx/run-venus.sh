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
pacman -Syu --noconfirm --needed gcc meson ninja pkgconf libdrm libepoxy python-yaml vulkan-swrast vulkan-tools vulkan-headers vulkan-icd-loader \
  glslang spirv-tools unzip >/tmp/pacman.log 2>&1 || { echo "pacman failed"; tail -15 /tmp/pacman.log; exit 1; }
OUT=/tmp/vn; mkdir -p $OUT
zip=$(ls drv/Venus-Bridge-guest-*.zip | head -1)
unzip -q -o "$zip" libvulkan_panvk.so virtio_icd.aarch64.json -d $OUT
sed -i "s|\"library_path\": *\"[^\"]*\"|\"library_path\": \"$OUT/libvulkan_panvk.so\"|" $OUT/virtio_icd.aarch64.json
# the phone's server: the same virglrenderer source and patches, built the same way
mkdir -p $OUT/vr && tar -xzf drv/virglrenderer-src.tar.gz -C $OUT/vr
VR=$(dirname $(find $OUT/vr -name meson_options.txt | head -1))
meson setup $OUT/vrb $VR -Dplatforms= -Dvenus=true -Drender-server=false -Dvideo=false -Dtests=false \
  -Dfuzzer=false -Dminigbm_allocation=false -Dcheck-gl-errors=false > $OUT/vr-setup.log 2>&1 \
  && ninja -C $OUT/vrb > $OUT/vr-build.log 2>&1 || { echo "FAIL building the server"; grep -E "error|Error|undefined" $OUT/vr-build.log | head -20; tail -n 15 $OUT/vr-build.log; exit 1; }
SERVER_BIN=$(find $OUT/vrb -name virgl_test_server -type f | head -1)
export LD_LIBRARY_PATH=$(dirname $(find $OUT/vrb -name 'libvirglrenderer.so*' | head -1))
echo "== server: $SERVER_BIN (built from the release's virglrenderer source)"
export VK_DRIVER_FILES=$(ls /usr/share/vulkan/icd.d/lvp_icd*.json | head -1)
echo "== host Vulkan for the server: $VK_DRIVER_FILES"; vulkaninfo --summary 2>&1 | grep -E "deviceName|apiVersion" | head -3
export VKR_DEBUG=all
$SERVER_BIN --venus --no-virgl --multi-clients > $OUT/server.log 2>&1 &
SERVER=$!
for i in $(seq 1 50); do [ -S /tmp/.virgl_test ] && break; sleep 0.2; done
unset VK_DRIVER_FILES LD_LIBRARY_PATH
[ -S /tmp/.virgl_test ] || { echo "FAIL no vtest socket"; cat $OUT/server.log | tail -20; exit 1; }
export VK_ICD_FILENAMES=$OUT/virtio_icd.aarch64.json VN_DEBUG=vtest,init STEAM_COMPAT_CLIENT_INSTALL_PATH=/tmp/steamroot
mkdir -p /tmp/steamroot/logs
vulkaninfo --summary > $OUT/vi.txt 2>&1
grep -E "deviceName|driverName|apiVersion" $OUT/vi.txt | head -6
if grep -q "Venus" $OUT/vi.txt; then echo "PASS the bridge lists lavapipe through venus"; else echo "FAIL the bridge lists no GPU"; tail -20 $OUT/vi.txt; tail -20 $OUT/server.log; fi
# DXVK's features on a GPU without them (as Mali): the driver told to emulate them all
gcc -O2 -Wall -o $OUT/vkemu tools/tests/glx/vkemu/vkemu.c -lvulkan || exit 1
spirv-as --target-env spv1.0 tools/tests/glx/vkemu/clip.spvasm -o $OUT/clip-spv10.spv || exit 1
spirv-as --target-env spv1.5 tools/tests/glx/vkemu/clip.spvasm -o $OUT/clip-spv15.spv || exit 1
glslangValidator -V tools/tests/glx/vkemu/green.frag -o $OUT/green.spv >/dev/null || exit 1
echo "---- DXVK's features emulated (DROIDDECK_VN_EMULATE=mali)"
rm -rf /tmp/dump; mkdir -p /tmp/dump
DROIDDECK_VN_EMULATE=mali DROIDDECK_VN_DUMP_SPIRV=/tmp/dump timeout 120 $OUT/vkemu $OUT/clip-spv10.spv $OUT/clip-spv15.spv $OUT/green.spv > $OUT/emu.txt 2>&1; rc=$?
grep "vkemu:" $OUT/emu.txt
grep "emulated for DXVK\|SPIR-V" /tmp/steamroot/logs/droiddeck_vulkan.txt | tail -4 | cut -c1-200
[ $rc = 0 ] && echo "PASS DXVK's features on a GPU without them" || { echo "FAIL DXVK's features on a GPU without them (rc $rc)"; tail -15 $OUT/emu.txt; }
v=0; for f in /tmp/dump/*.spv; do
  [ -f "$f" ] || continue
  if spirv-val "$f" > $OUT/val.txt 2>&1; then v=$((v+1)); else echo "FAIL rewritten SPIR-V $f: $(head -3 $OUT/val.txt)"; spirv-dis "$f" | head -40; fi
done
[ $v -ge 2 ] && echo "PASS rewritten shaders valid ($v checked by spirv-val)" || echo "FAIL rewritten shaders checked: $v"
spirv-dis $(ls /tmp/dump/*.spv | head -1) 2>/dev/null | grep -E "Capability|Variable|EntryPoint|Pointer" | head -14
echo "---- the same without emulation (the GPU here draws wireframe; the check must notice)"
DROIDDECK_VN_EMULATE=0 timeout 120 $OUT/vkemu $OUT/clip-spv10.spv $OUT/green.spv > $OUT/noemu.txt 2>&1
grep "vkemu:" $OUT/noemu.txt | head -6
echo "== server log:"; grep -v "^$" $OUT/server.log | tail -25
kill $SERVER 2>/dev/null
