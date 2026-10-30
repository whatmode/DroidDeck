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
pacman -Syu --noconfirm --needed gcc git meson ninja pkgconf libdrm libepoxy python-yaml vulkan-swrast vulkan-tools vulkan-headers vulkan-icd-loader \
  glslang spirv-tools unzip mesa libglvnd libx11 xorg-server-xvfb xorg-xdpyinfo >/tmp/pacman.log 2>&1 || { echo "pacman failed"; tail -15 /tmp/pacman.log; exit 1; }
OUT=/tmp/vn; mkdir -p $OUT
zip=$(ls drv/Venus-Bridge-guest-*.zip | head -1)
unzip -q -o "$zip" libvulkan_panvk.so virtio_icd.aarch64.json -d $OUT
sed -i "s|\"library_path\": *\"[^\"]*\"|\"library_path\": \"$OUT/libvulkan_panvk.so\"|" $OUT/virtio_icd.aarch64.json
# the phone's server: the same virglrenderer source and patches, built the same way
mkdir -p $OUT/vr && tar -xzf drv/virglrenderer-src.tar.gz -C $OUT/vr
VR=$(dirname $(find $OUT/vr -name meson_options.txt | head -1))
# glibc 2.34+ declares C11 call_once/once_flag in stdlib.h: use its own in the old compat header
python3 - "$VR/src/mesa/compat/c11/threads_posix.h" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
s = s.replace("#include <pthread.h>\n", "#include <pthread.h>\n#include <stdlib.h>\n#if defined(__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 34)\n#define DD_GLIBC_ONCE 1\n#endif\n", 1)
s = s.replace("#define ONCE_FLAG_INIT PTHREAD_ONCE_INIT", "#ifndef DD_GLIBC_ONCE\n#define ONCE_FLAG_INIT PTHREAD_ONCE_INIT\n#endif", 1)
s = s.replace("typedef pthread_once_t  once_flag;", "#ifndef DD_GLIBC_ONCE\ntypedef pthread_once_t  once_flag;\n#endif", 1)
s = s.replace("static inline void\ncall_once(once_flag *flag, void (*func)(void))\n{\n    pthread_once(flag, func);\n}", "#ifndef DD_GLIBC_ONCE\nstatic inline void\ncall_once(once_flag *flag, void (*func)(void))\n{\n    pthread_once(flag, func);\n}\n#endif", 1)
open(p, "w").write(s)
PY
for p in tools/venus/patches/*.patch; do (cd $VR && git init -q . 2>/dev/null; git apply "$OLDPWD/$p") || echo "== server patch $(basename $p) did not apply"; done
meson setup $OUT/vrb $VR -Dplatforms= -Dvenus=true -Drender-server=false -Dvideo=false -Dtests=false \
  -Dfuzzer=false -Dminigbm_allocation=false -Dcheck-gl-errors=false > $OUT/vr-setup.log 2>&1 \
  && ninja -C $OUT/vrb > $OUT/vr-build.log 2>&1 || { echo "FAIL building the server"; grep -E "error|Error|undefined" $OUT/vr-build.log | head -20; tail -n 15 $OUT/vr-build.log; exit 1; }
SERVER_BIN=$(find $OUT/vrb -name virgl_test_server -type f | head -1)
export LD_LIBRARY_PATH=$(dirname $(find $OUT/vrb -name 'libvirglrenderer.so*' | head -1))
echo "== server: $SERVER_BIN (built from the release's virglrenderer source)"
export VK_DRIVER_FILES=$(ls /usr/share/vulkan/icd.d/lvp_icd*.json | head -1)
echo "== host Vulkan for the server: $VK_DRIVER_FILES"; vulkaninfo --summary 2>&1 | grep -E "deviceName|apiVersion" | head -3
# venus contexts run in the render server the vtest server starts (its built-in path is the
# install directory, which a build tree does not have)
export RENDER_SERVER_EXEC_PATH=$(find $OUT/vrb -name virgl_render_server -type f | head -1)
echo "== render server: $RENDER_SERVER_EXEC_PATH"
# validate: the server runs the GPU calls under the validation layer (when Arch has it);
# udmabuf: host-visible memory imported as dma-buf, as the phone's (dma-heap) does
pacman -S --noconfirm --needed vulkan-validation-layers >>/tmp/pacman.log 2>&1 || true
if ls /usr/share/vulkan/explicit_layer.d/*validation* >/dev/null 2>&1; then export VKR_DEBUG=all; else export VKR_DEBUG=udmabuf; fi
[ -e /dev/udmabuf ] || echo "== no /dev/udmabuf: host-visible memory is exported by lavapipe instead"
echo "== VKR_DEBUG=$VKR_DEBUG"
# the phone's server offers a device-local-only memory type (native GPU memory) because its
# host-visible memory comes from a dma-heap; here it is turned on by hand
start_server() {
  rm -f /tmp/.virgl_test
  VK_DRIVER_FILES=$HOST_ICD LD_LIBRARY_PATH=$HOST_LIBS VKR_NATIVE_MEMORY=$1 VKR_MEMORY_STATS_MS=1000 \
    $SERVER_BIN --venus --no-virgl --multi-clients > $OUT/server-$1.log 2>&1 &
  SERVER=$!
  for i in $(seq 1 50); do [ -S /tmp/.virgl_test ] && break; sleep 0.2; done
  [ -S /tmp/.virgl_test ] || { echo "FAIL no vtest socket"; tail -20 $OUT/server-$1.log; exit 1; }
}
HOST_ICD=$VK_DRIVER_FILES HOST_LIBS=$LD_LIBRARY_PATH
unset VK_DRIVER_FILES LD_LIBRARY_PATH
start_server 1
ln -sf server-1.log $OUT/server.log
export VK_ICD_FILENAMES=$OUT/virtio_icd.aarch64.json VN_DEBUG=vtest,init STEAM_COMPAT_CLIENT_INSTALL_PATH=/tmp/steamroot
mkdir -p /tmp/steamroot/logs
vulkaninfo --summary > $OUT/vi.txt 2>&1
grep -E "deviceName|driverName|apiVersion" $OUT/vi.txt | head -6
if grep -q "Venus" $OUT/vi.txt; then echo "PASS the bridge lists lavapipe through venus"; else echo "FAIL the bridge lists no GPU"; tail -20 $OUT/vi.txt; tail -20 $OUT/server.log; fi
# DXVK's features on a GPU without them (as Mali): the driver told to emulate them all
gcc -O2 -Wall -o $OUT/vkemu tools/tests/glx/vkemu/vkemu.c -lvulkan || exit 1
spirv-as --target-env spv1.0 tools/tests/glx/vkemu/clip.spvasm -o $OUT/clip-spv10.spv || exit 1
spirv-as --target-env spv1.5 tools/tests/glx/vkemu/clip.spvasm -o $OUT/clip-spv15.spv || exit 1
spirv-as --target-env spv1.0 tools/tests/glx/vkemu/clipblock.spvasm -o $OUT/clipblock-spv10.spv || exit 1
spirv-as --target-env spv1.5 tools/tests/glx/vkemu/clipblock.spvasm -o $OUT/clipblock-spv15.spv || exit 1
glslangValidator -V tools/tests/glx/vkemu/green.frag -o $OUT/green.spv >/dev/null || exit 1
echo "---- DXVK's features emulated (DROIDDECK_VN_EMULATE=mali)"
rm -rf /tmp/dump; mkdir -p /tmp/dump
DROIDDECK_VN_EMULATE=mali DROIDDECK_VN_DUMP_SPIRV=/tmp/dump timeout 120 $OUT/vkemu $OUT/clip-spv10.spv $OUT/clip-spv15.spv $OUT/clipblock-spv10.spv $OUT/clipblock-spv15.spv $OUT/green.spv > $OUT/emu.txt 2>&1; rc=$?
grep "vkemu:" $OUT/emu.txt
grep "emulated for DXVK\|SPIR-V" /tmp/steamroot/logs/droiddeck_vulkan.txt | tail -8 | cut -c1-200
[ $rc = 0 ] && echo "PASS DXVK's features on a GPU without them" || { echo "FAIL DXVK's features on a GPU without them (rc $rc)"; tail -15 $OUT/emu.txt; }
v=0; for f in /tmp/dump/*.spv; do
  [ -f "$f" ] || continue
  if spirv-val "$f" > $OUT/val.txt 2>&1; then v=$((v+1)); else echo "FAIL rewritten SPIR-V $f: $(head -3 $OUT/val.txt)"; spirv-dis "$f" | head -40; fi
done
[ $v -ge 4 ] && echo "PASS rewritten shaders valid ($v checked by spirv-val)" || echo "FAIL rewritten shaders checked: $v"
spirv-dis $(ls /tmp/dump/*.spv | head -1) 2>/dev/null | grep -E "Capability|Variable|EntryPoint|Pointer" | head -14
echo "---- BC textures emulated (DROIDDECK_VN_EMULATE=mali)"
gcc -O2 -Wall -o $OUT/vkbc tools/tests/glx/vkemu/vkbc.c -lvulkan || exit 1
DROIDDECK_VN_EMULATE=mali timeout 120 $OUT/vkbc > $OUT/bc.txt 2>&1; rc=$?
grep "vkbc:" $OUT/bc.txt
grep "BC textures" /tmp/steamroot/logs/droiddeck_vulkan.txt | grep vkbc | tail -3 | cut -c1-300
[ $rc = 0 ] && echo "PASS BC textures on a GPU without them" || { echo "FAIL BC textures on a GPU without them (rc $rc)"; tail -8 $OUT/bc.txt; }
echo "---- the same without emulation (the GPU here draws wireframe; the check must notice)"
DROIDDECK_VN_EMULATE=0 timeout 120 $OUT/vkemu $OUT/clip-spv10.spv $OUT/green.spv > $OUT/noemu.txt 2>&1
grep "vkemu:" $OUT/noemu.txt | head -6
echo "---- native device-local memory (the server's alias type, VKR_NATIVE_MEMORY=1)"
gcc -O2 -Wall -o $OUT/vkmem tools/tests/glx/vkemu/vkmem.c -lvulkan || exit 1
timeout 120 $OUT/vkmem 1 > $OUT/mem1.txt 2>&1; rc=$?
grep "vkmem:" $OUT/mem1.txt
[ $rc = 0 ] || { echo "FAIL vkmem with the alias (rc $rc)"; tail -8 $OUT/mem1.txt; }
grep -E "memory type|native device-local|allocations in" $OUT/server-1.log | head -12
# The game-session reports (0027): a full descriptor pool refused without a round trip (DXVK fills
# dozens a frame), GPU busy from timestamps around submissions, wait stacks named by function
# and thread. The 10 s windows are shortened so a 2 s run has several.
echo "---- the game-session reports (descriptor pools, GPU busy, named wait stacks)"
gcc -O2 -Wall -o $OUT/vkstats tools/tests/glx/vkemu/vkstats.c -lvulkan || exit 1
L=/tmp/steamroot/logs/droiddeck_vulkan.txt
stats_case() {  # name, env...
  local name=$1; shift
  local before=$(wc -l < $L 2>/dev/null || echo 0)
  env DROIDDECK_VN_STATS_MS=400 DROIDDECK_VN_WAIT_STACKS_MS=400 "$@" timeout 120 $OUT/vkstats 2 > $OUT/stats-$name.txt 2>&1; SRC=$?
  tail -n +$((before + 1)) $L | grep " vkstats" > $OUT/statslog-$name.txt
  grep "vkstats:" $OUT/stats-$name.txt | sort | uniq -c | sed 's/^ *1 //'
  grep -E "gpu over|descriptor pools over|cpu over|wait stack 1 " $OUT/statslog-$name.txt | tail -4 | cut -c1-330
}
stats_case default
[ $SRC = 0 ] && echo "PASS a full descriptor pool is refused" || { echo "FAIL a full descriptor pool is refused (rc $SRC)"; tail -5 $OUT/stats-default.txt; }
grep -q "descriptor pools over .*s: [1-9][0-9]* allocations refused by the guest's accounting (no round trip); 0 sent" $OUT/statslog-default.txt \
  && echo "PASS a full pool costs no round trip" || echo "FAIL a full pool costs no round trip"
grep -q "gpu over .*as the union of [1-9][0-9]* timed" $OUT/statslog-default.txt && echo "PASS GPU busy measured by timestamps" \
  || { echo "FAIL GPU busy measured by timestamps"; grep "gpu" $OUT/statslog-default.txt | head -3; }
grep -q "ring: 4096 KiB" $OUT/statslog-default.txt && echo "PASS the 4 MiB command ring" || { echo "FAIL the 4 MiB command ring"; grep "ring:" $OUT/statslog-default.txt | head -2; }
grep -q "cpu over .*this program" $OUT/statslog-default.txt && echo "PASS this program's CPU use by thread" || echo "FAIL this program's CPU use by thread"
grep "wait stack" $OUT/statslog-default.txt | grep -q "in thread 'vkstats'.* vn_[A-Za-z_]*+0x" \
  && echo "PASS wait stacks name the thread and the driver's functions" \
  || { echo "FAIL wait stacks name the thread and the driver's functions"; grep "wait stack" $OUT/statslog-default.txt | head -2 | cut -c1-300; }
# lavapipe does not hold a pool to its maxSets, so here the real pool says yes (vkstats rc 2)
# the driver's on-disk pipeline cache (0030): the first run saves what it compiled at
# vkDestroyDevice, the second loads it
printf '#version 450\nlayout(local_size_x = 64) in;\nlayout(std430, binding = 0) buffer B { uint v[]; };\nvoid main() { }\n' > $OUT/pc.comp
glslangValidator -V $OUT/pc.comp -o $OUT/pc.spv >/dev/null || exit 1
rm -rf /tmp/pcache-home; mkdir -p /tmp/pcache-home
for run in 1 2; do
  before=$(wc -l < $L)
  HOME=/tmp/pcache-home XDG_CACHE_HOME= timeout 60 $OUT/vkstats 0.1 $OUT/pc.spv > $OUT/pc-$run.txt 2>&1
  tail -n +$((before + 1)) $L | grep " vkstats" | grep "pipeline cache" | cut -c1-240 | tee $OUT/pclog-$run.txt
  grep "compute pipeline" $OUT/pc-$run.txt
done
ls -la /tmp/pcache-home/.cache/droiddeck-venus/ 2>&1 | tail -2
PCLOADED=$(sed -n 's/.*, \([0-9]*\) bytes loaded.*/\1/p' $OUT/pclog-2.txt | tail -1)
grep -q "pipeline cache: saved" $OUT/pclog-1.txt && [ "${PCLOADED:-0}" -gt 64 ] \
  && echo "PASS the pipeline cache is saved and loaded across runs" \
  || { echo "FAIL the pipeline cache across runs"; cat $OUT/pclog-1.txt $OUT/pclog-2.txt | head -6; }
stats_case defer DROIDDECK_VN_POOL_DEFER=1
grep -q "descriptor pools over .* [1-9][0-9]* sent synchronously to the real pool" $OUT/statslog-defer.txt \
  && echo "PASS DROIDDECK_VN_POOL_DEFER=1 still asks the real pool" || echo "FAIL DROIDDECK_VN_POOL_DEFER=1 (rc $SRC)"
echo "== server log:"; grep -v "^$" $OUT/server-1.log | grep -v "memory type" | tail -25
kill $SERVER 2>/dev/null; wait $SERVER 2>/dev/null
# The phone's memory types (Mali: no device-local type without host-visible; a cached type the
# venus guest hides host-visible from) on the server's lavapipe, and GL from the driver's Zink
# making textures as Steam's UI does: they must come from the server's native type.
gcc -shared -fPIC -O2 -Wall -o $OUT/libmalimem.so tools/tests/glx/malimem_layer.c || exit 1
gcc -shared -fPIC -O2 -Wall -o $OUT/libgputype.so tools/tests/glx/gputype_layer.c || exit 1
mkdir -p $OUT/layers && cp tools/tests/glx/malimem_layer.json tools/tests/glx/gputype_layer.json $OUT/layers/
sed -i "s|/tmp/glx/libmalimem.so|$OUT/libmalimem.so|" $OUT/layers/malimem_layer.json
sed -i "s|/tmp/glx/libgputype.so|$OUT/libgputype.so|" $OUT/layers/gputype_layer.json
gcc -O2 -Wall -o $OUT/glmem tools/tests/glx/glmem.c -lGL -lX11 || exit 1
unzip -q -o "$zip" 'gl/*' -d $OUT/drv
G=$OUT/drv/gl; genv=()
while IFS= read -r line; do
  case "$line" in ''|'#'*) continue ;; esac
  k=${line%%=*}; v=${line#*=}; v=${v//@GL@/$G}
  if [ "${k%+}" != "$k" ]; then k=${k%+}; v="${!k:+${!k}:}$v"; fi
  genv+=("$k=$v")
done < $G/env
Xvfb :9 -screen 0 640x480x24 >/dev/null 2>&1 & XVFB=$!; sleep 2
glmem_case() {  # name, server env..., -- client env...
  local name=$1; shift
  local senv=(); while [ $# -gt 0 ] && [ "$1" != -- ]; do senv+=("$1"); shift; done; shift
  kill $SERVER 2>/dev/null; wait $SERVER 2>/dev/null; rm -f /tmp/.virgl_test
  env VK_DRIVER_FILES=$HOST_ICD LD_LIBRARY_PATH=$HOST_LIBS VKR_MEMORY_STATS_MS=200 \
    VK_ADD_LAYER_PATH=$OUT/layers VK_INSTANCE_LAYERS=VK_LAYER_DROIDDECK_malimem:VK_LAYER_DROIDDECK_gputype "${senv[@]}" \
    $SERVER_BIN --venus --no-virgl --multi-clients > $OUT/server-$name.log 2>&1 &
  SERVER=$!
  for i in $(seq 1 50); do [ -S /tmp/.virgl_test ] && break; sleep 0.2; done
  DISPLAY=:9 env "${genv[@]}" MESA_LOADER_DRIVER_OVERRIDE=zink GALLIUM_DRIVER=zink LIBGL_KOPPER_DRI2=true DROIDDECK_ZINK_STATS_MS=500 DROIDDECK_VN_WAIT_STACKS_MS=500 "$@" timeout 120 $OUT/glmem > $OUT/glmem-$name.txt 2>&1; GLRC=$?
  sleep 1.2; DISPLAY=:9 env "${genv[@]}" "$@" timeout 60 $OUT/vkmem 0 > $OUT/vkmem-$name.txt 2>&1
  grep "glmem:" $OUT/glmem-$name.txt; grep "droiddeck zink: device-local" $OUT/glmem-$name.txt | head -1
  grep "vkmem: memory type" $OUT/vkmem-$name.txt | tr '\n' ';'; echo
  grep "memory type\|native device-local memory" $OUT/server-$name.log | sort -u | head -6
  NATIVE=$(grep "allocations in" $OUT/server-$name.log | sed 's/.*native device-local [0-9]* ([0-9.]* MiB, total \([0-9]*\).*/\1/' | sort -n | tail -1)
  HEAP=$(grep "allocations in" $OUT/server-$name.log | sed 's/.*dma-heap [0-9]* ([0-9.]* MiB, total \([0-9]*\).*/\1/' | sort -n | tail -1)
  grep "allocations in" $OUT/server-$name.log | tail -1
  echo "== $name: native allocations ${NATIVE:-0}, dma-buf ones ${HEAP:-0}"
  if [ $GLRC != 0 ]; then tail -6 $OUT/glmem-$name.txt; tail -4 $OUT/vkmem-$name.txt; grep -v "^$" $OUT/server-$name.log | tail -6; fi
}
echo "---- phone memory types, GL textures from the driver's Zink (expect native memory)"
glmem_case mali VKR_NATIVE_MEMORY=1 --
[ $GLRC = 0 ] && [ "${NATIVE:-0}" -gt 0 ] && echo "PASS GL textures in native memory with the phone's types (native ${NATIVE:-0}, dma-buf ${HEAP:-0})" \
  || { echo "FAIL GL textures in native memory with the phone's types (rc $GLRC, native ${NATIVE:-0}, dma-buf ${HEAP:-0})"; tail -8 $OUT/glmem-mali.txt; grep -i "error\|fail" $OUT/server-mali.log | head -5; }
# the driver's frame stats (GPU time from timestamps, render passes, pipelines, waits)
grep "droiddeck zink \[" $OUT/glmem-mali.txt | tail -2 | cut -c1-400
if grep "droiddeck zink \[" $OUT/glmem-mali.txt | grep -qv " 0 timed" && grep "droiddeck zink \[" $OUT/glmem-mali.txt | grep -q "render passes"; then
  echo "PASS the driver's frame stats (GPU time measured)"; else echo "FAIL the driver's frame stats"; fi
# who waits (call stacks of fence and server-reply waits, in droiddeck_vulkan.txt)
grep "wait stack" /tmp/steamroot/logs/droiddeck_vulkan.txt | grep glmem | tail -3 | cut -c1-400
grep "wait stack" /tmp/steamroot/logs/droiddeck_vulkan.txt | grep glmem | grep -qE "libvulkan_panvk.so\+0x| vn_[A-Za-z_]*\+0x" && echo "PASS wait stacks with library offsets or names" || echo "FAIL wait stacks"
grep "droiddeck cpu over" $OUT/glmem-mali.txt | tail -1 | cut -c1-300
grep -q "droiddeck cpu over .*glmem" $OUT/glmem-mali.txt && echo "PASS the session's CPU use by thread" || echo "FAIL the session's CPU use by thread"
echo "---- the same with the driver keeping the GPU's order (DROIDDECK_ZINK_PREFER_UNMAPPABLE=0: expect none)"
glmem_case mali-order VKR_NATIVE_MEMORY=1 -- DROIDDECK_ZINK_PREFER_UNMAPPABLE=0 DROIDDECK_VN_IMAGE_NATIVE=0
[ $GLRC = 0 ] && [ "${NATIVE:-0}" = 0 ] && echo "PASS the check notices memory not taken from the native type" \
  || echo "FAIL GPU order case (rc $GLRC, native ${NATIVE:-0})"
# BC decode buffers (0027): nothing maps them, so they belong in the server's native type, not in
# dma-heap memory. vkbc's own memory is all host-visible, so a native allocation here is the
# decode's.
bc_native_case() {  # name, client env...
  local name=$1; shift
  kill $SERVER 2>/dev/null; wait $SERVER 2>/dev/null; rm -f /tmp/.virgl_test
  env VK_DRIVER_FILES=$HOST_ICD LD_LIBRARY_PATH=$HOST_LIBS VKR_MEMORY_STATS_MS=200 VKR_NATIVE_MEMORY=1 \
    VK_ADD_LAYER_PATH=$OUT/layers VK_INSTANCE_LAYERS=VK_LAYER_DROIDDECK_malimem:VK_LAYER_DROIDDECK_gputype \
    $SERVER_BIN --venus --no-virgl --multi-clients > $OUT/server-$name.log 2>&1 &
  SERVER=$!
  for i in $(seq 1 50); do [ -S /tmp/.virgl_test ] && break; sleep 0.2; done
  local before=$(wc -l < $L 2>/dev/null || echo 0)
  env "$@" timeout 60 $OUT/vkstats 0.2 > $OUT/img-$name.txt 2>&1
  grep "image memory types" $OUT/img-$name.txt
  IMGHV=$(grep "vkstats: optimal image" $OUT/img-$name.txt | sed 's/.*host-visible among them //')
  LINHV=$(grep "vkstats: linear image" $OUT/img-$name.txt | sed 's/.*host-visible among them //')
  env DROIDDECK_VN_EMULATE=mali "$@" timeout 120 $OUT/vkbc > $OUT/bc-$name.txt 2>&1; BCRC=$?
  sleep 0.5
  grep "vkbc: done" $OUT/bc-$name.txt
  tail -n +$((before + 1)) $L | grep " vkbc" | grep -E "BC staging|on the GPU|BC decode buffers" | tail -4 | cut -c1-260
  grep "memory type" $OUT/server-$name.log | sort -u | head -5
  grep "allocations in" $OUT/server-$name.log | tail -1
  NATIVE=$(grep "allocations in" $OUT/server-$name.log | sed 's/.*native device-local [0-9]* ([0-9.]* MiB, total \([0-9]*\).*/\1/' | sort -n | tail -1)
  echo "== $name: native allocations ${NATIVE:-0}"
  # the server's stats line only comes with a later allocation, which a run this short may not
  # make; the driver's own line says which type the decode took (0x1: device-local only)
  BCTYPE=$(tail -n +$((before + 1)) $L | grep " vkbc" | grep "BC decode buffers (device-local)" | sed 's/.*flags \(0x[0-9a-f]*\).*/\1/' | tail -1)
  # its own annotation: the report chunks run out before this section
  printf '::notice title=bc native %s::%s\n' "$name" "$( { tail -n +$((before + 1)) $L | grep " vkbc" | tail -8; grep -E "memory type|allocations in|rror" $OUT/server-$name.log | sort | uniq -c | sort -rn | head -12; } | sed 's/%/%25/g' | cut -c1-250 | awk 'BEGIN{ORS="%0A"}{print}')" >&3
}
echo "---- BC decode buffers with the phone's memory types (expect native memory)"
bc_native_case bcnative
[ $BCRC = 0 ] && [ "$BCTYPE" = 0x1 ] && echo "PASS BC decode buffers in native memory (flags $BCTYPE, server native ${NATIVE:-0})" \
  || { echo "FAIL BC decode buffers in native memory (rc $BCRC, flags ${BCTYPE:-none})"; tail -6 $OUT/bc-bcnative.txt; }
[ "$IMGHV" = 0x0 ] && [ -n "$LINHV" ] && [ "$LINHV" != 0x0 ] && echo "PASS optimal images offered native memory only, linear ones host-visible" \
  || echo "FAIL image memory types (optimal host-visible ${IMGHV:-none}, linear ${LINHV:-none})"
bc_native_case bcheap DROIDDECK_VN_BC_NATIVE=0 DROIDDECK_VN_IMAGE_NATIVE=0
[ -n "$IMGHV" ] && [ "$IMGHV" != 0x0 ] && echo "PASS DROIDDECK_VN_IMAGE_NATIVE=0 offers the host-visible types again" \
  || echo "FAIL DROIDDECK_VN_IMAGE_NATIVE=0 (optimal host-visible ${IMGHV:-none})"
[ $BCRC = 0 ] && [ -n "$BCTYPE" ] && [ "$BCTYPE" != 0x1 ] && echo "PASS the check notices decode buffers outside the native type (flags $BCTYPE)" \
  || echo "FAIL BC decode with DROIDDECK_VN_BC_NATIVE=0 (rc $BCRC, flags ${BCTYPE:-none})"
echo "---- an older app (no native type): GL still right"
glmem_case mali-old VKR_NATIVE_MEMORY=0 --
[ $GLRC = 0 ] && echo "PASS GL textures with an older app's server" || echo "FAIL GL textures with an older app's server (rc $GLRC)"
kill $XVFB 2>/dev/null
# A Vulkan program presenting to an X server without DRI3 and Present (as the games' Xwayland
# -shm): the driver's X11 WSI must send its frames by the CPU instead of requests to the
# missing extensions (which close the connection: Lake, 26 Oct).
echo "---- Vulkan presenting to X without DRI3 (Present cannot be turned off in Xvfb) (vkcube)"
gcc -O2 -Wall -o $OUT/xshot tools/tests/glx/xshot.c -lX11 || exit 1
Xvfb :10 -screen 0 640x480x24 -extension Present >/dev/null 2>&1 & XVFB=$!; sleep 2
glmem_case_server() {  # the server with the phone's memory types, as above
  kill $SERVER 2>/dev/null; wait $SERVER 2>/dev/null; rm -f /tmp/.virgl_test
  env VK_DRIVER_FILES=$HOST_ICD LD_LIBRARY_PATH=$HOST_LIBS VKR_MEMORY_STATS_MS=200 \
    VK_ADD_LAYER_PATH=$OUT/layers VK_INSTANCE_LAYERS=VK_LAYER_DROIDDECK_malimem:VK_LAYER_DROIDDECK_gputype \
    $SERVER_BIN --venus --no-virgl --multi-clients > $OUT/server-cube.log 2>&1 &
  SERVER=$!
  for i in $(seq 1 50); do [ -S /tmp/.virgl_test ] && break; sleep 0.2; done
}
glmem_case_server
DISPLAY=:10 xdpyinfo 2>/dev/null | grep -iE "DRI3|Present" | head -3
# frames for 10 s (timeout ends it: rc 124), the screen grabbed while it runs
( DISPLAY=:10 timeout 10 vkcube --wsi xcb --c 1000000 > $OUT/cube.txt 2>&1; echo "rc=$?" >> $OUT/cube.txt ) &
CUBE=$!; sleep 6; DISPLAY=:10 $OUT/xshot | tee $OUT/shot.txt; wait $CUBE
grep -E "droiddeck wsi|rc=|rror|DRI3|missing" $OUT/cube.txt | head -8
if grep -q "rc=124" $OUT/cube.txt && grep -q "droiddeck wsi: X server without" $OUT/cube.txt && ! grep -q " 0 of " $OUT/shot.txt; then
  echo "PASS Vulkan frames to X without DRI3/Present"; else echo "FAIL Vulkan frames to X without DRI3/Present"; tail -12 $OUT/cube.txt; fi
kill $XVFB 2>/dev/null
echo "---- the same with the alias off (VKR_NATIVE_MEMORY=0, as before)"
start_server 0
timeout 120 $OUT/vkmem 0 > $OUT/mem0.txt 2>&1; rc=$?
grep "vkmem:" $OUT/mem0.txt | grep -v "memory type"
[ $rc = 0 ] && echo "PASS vkmem with the alias off" || { echo "FAIL vkmem with the alias off (rc $rc)"; tail -8 $OUT/mem0.txt; }
grep -E "native device-local" $OUT/server-0.log | head -3
kill $SERVER 2>/dev/null
