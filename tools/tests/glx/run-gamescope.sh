#!/bin/bash
# Steam's UI pattern (tools/tests/glx/retire.c) under the app's gamescope (mali-gl runtime) with
# the bridge driver's GL: how often the main window's swapchain is made again (each showed as
# a flash in Steam's UI) and why. Arch Linux ARM, Zink on lavapipe reported as a GPU.
set -uo pipefail
REPORT=/tmp/gs-report.txt; : > $REPORT
exec 3>&1
exec > >(tee -a $REPORT) 2>&1
annotate() {
  sleep 1
  local n=0
  split -l 70 $REPORT /tmp/gs-chunk.
  for f in /tmp/gs-chunk.*; do
    n=$((n+1)); [ $n -gt 9 ] && break
    printf '::notice title=gamescope report %d::%s\n' $n "$(sed 's/%/%25/g' "$f" | cut -c1-300 | awk 'BEGIN{ORS="%0A"}{print}')" >&3
  done
}
trap annotate EXIT
grep -q '^DisableSandbox' /etc/pacman.conf || sed -i 's/^\[options\]/[options]\nDisableSandbox/' /etc/pacman.conf
{ for m in https://ca.us.mirror.archlinuxarm.org https://fl.us.mirror.archlinuxarm.org https://de3.mirror.archlinuxarm.org https://nl.mirror.archlinuxarm.org; do echo "Server = $m/\$arch/\$repo"; done; cat /etc/pacman.d/mirrorlist; } > /etc/pacman.d/mirrorlist.new
mv /etc/pacman.d/mirrorlist.new /etc/pacman.d/mirrorlist
pacman -Syu --noconfirm --needed gcc mesa vulkan-swrast vulkan-headers libglvnd libx11 xorg-xwayland gamescope \
  unzip >/tmp/pacman.log 2>&1 || { echo "pacman failed"; tail -15 /tmp/pacman.log; exit 1; }
OUT=/tmp/gs; mkdir -p $OUT
gcc -O2 -Wall -o $OUT/retire tools/tests/glx/retire.c -lGL -lX11 || exit 1
gcc -shared -fPIC -O2 -Wall -o $OUT/libgputype.so tools/tests/glx/gputype_layer.c || exit 1
mkdir -p $OUT/layers && cp tools/tests/glx/gputype_layer.json $OUT/layers/ && sed -i "s|/tmp/glx/libgputype.so|$OUT/libgputype.so|" $OUT/layers/gputype_layer.json
GS=/usr/bin/gamescope
if [ -f gsrt/mali-gl-runtime-aarch64.tar.gz ]; then
  tar -xzf gsrt/mali-gl-runtime-aarch64.tar.gz -C $OUT && GS=$OUT/mali-gl/bin/gamescope
fi
echo "== gamescope: $GS ($($GS --version 2>&1 | head -1))"
zip=$(ls drv/Venus-Bridge-guest-*.zip 2>/dev/null | head -1)
unzip -q -o "$zip" 'gl/*' -d $OUT/drv || { echo "no driver GL part"; exit 1; }
G=$OUT/drv/gl
genv=()
while IFS= read -r line; do
  case "$line" in ''|'#'*) continue ;; esac
  k=${line%%=*}; v=${line#*=}; v=${v//@GL@/$G}
  if [ "${k%+}" != "$k" ]; then k=${k%+}; v="${!k:+${!k}:}$v"; fi
  genv+=("$k=$v")
done < $G/env
printf '#!/bin/sh\nexec /usr/bin/Xwayland "$@" -shm\n' > $OUT/Xwayland-shm && chmod 755 $OUT/Xwayland-shm
export XDG_RUNTIME_DIR=/tmp/xdg; mkdir -p -m 700 $XDG_RUNTIME_DIR
export MESA_LOADER_DRIVER_OVERRIDE=zink GALLIUM_DRIVER=zink LIBGL_KOPPER_DRI2=true
export ENABLE_GAMESCOPE_WSI=1 GAMESCOPE_WSI_FORCE_BYPASS=1 WLR_XWAYLAND=$OUT/Xwayland-shm
echo "== driver GL env: ${genv[*]}"
# The gputype layer only for the GL app (gamescope itself runs on lavapipe as it is).
run_gs() {
  env "${genv[@]}" timeout 120 "$1" --backend headless -W 1600 -H 720 -w 1600 -h 720 -r 60 ${GS_EXTRA:-} -- \
    env VK_LAYER_PATH=$OUT/layers VK_INSTANCE_LAYERS=VK_LAYER_DROIDDECK_gputype $OUT/retire 4 > $OUT/log 2>&1
  echo "== $1 exit $?"
}
run_gs $GS
report() {
  grep -E "retire|droiddeck kopper|Swapchain retired|Creating swapchain|Failed|OUT_OF_DATE|error|Error" $OUT/log | sed 's/\x1b\[[0-9;]*m//g' | cut -c1-160 | head -60
  echo "== new swapchains: $(grep -c 'droiddeck kopper: new swapchain' $OUT/log), retired: $(grep -c 'Swapchain retired' $OUT/log), created: $(grep -c 'Creating swapchain' $OUT/log)"
}
echo "======== plain"; report
echo "======== steam mode (-e), as the session runs it"; GS_EXTRA="-e" run_gs $GS; report
if ! grep -q "retire: done" $OUT/log && [ "$GS" != /usr/bin/gamescope ]; then
  echo "== the app's gamescope did not run it; its log:"; sed 's/\x1b\[[0-9;]*m//g' $OUT/log | tail -25 | cut -c1-200
  echo "== trying Arch's gamescope"; run_gs /usr/bin/gamescope
fi
grep -q "retire: done" $OUT/log
