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
  printf '::notice title=gamescope summary::%s\n' "$(grep -E '^PASS|^FAIL|^== new|^========|exit [0-9]' $REPORT | sed 's/%/%25/g' | awk 'BEGIN{ORS="%0A"}{print}')" >&3
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
  unzip xorg-xprop >/tmp/pacman.log 2>&1 || { echo "pacman failed"; tail -15 /tmp/pacman.log; exit 1; }
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
    env VK_LAYER_PATH=$OUT/layers VK_INSTANCE_LAYERS=VK_LAYER_DROIDDECK_gputype gdb -q -batch -ex run -ex "bt 25" --args $OUT/retire 4 ${RETIRE_MODE:-popups} > $OUT/log 2>&1
  echo "== $1 exit $?"
  grep -q "retire: done" $OUT/log || { echo "== the GL program did not finish; end of the log:"; sed 's/\x1b\[[0-9;]*m//g' $OUT/log | grep -v "vulkan:   [A-Z]" | tail -20 | cut -c1-180; }
}
run_gs $GS
report() {
  grep -E "retire:|droiddeck kopper|Swapchain retired|Creating swapchain|Failed|OUT_OF_DATE" $OUT/log | sed 's/\x1b\[[0-9;]*m//g' | cut -c1-120 | head -30
  echo "== new swapchains: $(grep -c 'droiddeck kopper: new swapchain' $OUT/log), retired: $(grep -c 'Swapchain retired' $OUT/log), created: $(grep -c 'Creating swapchain' $OUT/log)"
}
echo "======== plain"; report
echo "======== steam mode (-e), as the session runs it"; GS_EXTRA="-e" run_gs $GS; report
echo "======== the same window from a second X connection"; RETIRE_MODE=twodisplays GS_EXTRA="-e" run_gs $GS; report
retired=$(grep -c 'Swapchain retired' $OUT/log)
if grep -q "retire: done" $OUT/log && [ "${fails:-0}" = 0 ] && [ "$retired" = 0 ]; then echo "PASS one swapchain for a window drawn from two connections"
else echo "FAIL: $retired swapchain(s) retired for a window drawn from two connections"; fails=1; fi
# Steam's side menu over its main window: screenshots must show both, every time.
shots() {  # mode label share
  rm -rf $OUT/shots
  env "${genv[@]}" DROIDDECK_KOPPER_SHARE_SCREEN=$3 timeout 120 $GS --backend headless -W 1600 -H 720 -w 1600 -h 720 -r 60 -- \
    env VK_LAYER_PATH=$OUT/layers VK_INSTANCE_LAYERS=VK_LAYER_DROIDDECK_gputype tools/tests/glx/shots.sh $OUT/shots $OUT/retire 6 $1 > $OUT/log 2>&1
  echo "== $(ls $OUT/shots 2>/dev/null | wc -l) screenshot file(s); log:"; sed 's/\x1b\[[0-9;]*m//g' $OUT/log | grep -i "screenshot\|retire:\|xprop\|error" | sort | uniq -c | head -12 | cut -c1-180
  python3 - "$2" $OUT/shots <<'PY'
import glob, sys
sys.path.insert(0, "tools/tests/glx")
import pngpix
label, d = sys.argv[1:]
both = main_only = menu_only = other = 0
for f in sorted(glob.glob(d + "/shot*.png")):
    w, h, px = pngpix.read(f)
    m = px(w * 3 // 4, h // 2); n = px(w * 100 // 1600, h // 2)
    main = m[2] > 150 and m[0] < 80; menu = n[0] > 150 and n[1] > 150 and n[2] < 80
    if label.startswith("child"):
        # one window over the whole screen: blue (main) or yellow (child), as X shows it
        main, menu = main and not menu, menu and m[0] > 150 and m[1] > 150 and m[2] < 80
        if main: main_only += 1
        elif menu: menu_only += 1
        else: other += 1
        continue
    if main and menu: both += 1
    elif main: main_only += 1
    elif menu: menu_only += 1
    else: other += 1
print(f"== {label}: {both} screenshot(s) with both windows, {main_only} main only, {menu_only} menu/child only, {other} neither")
if label.startswith("child hidden"): ok = main_only and not (both or menu_only or other)
elif label.startswith("child shown"): ok = menu_only and not (both or main_only or other)
else: ok = both and not (main_only or menu_only or other)
print(("PASS " if ok else "FAIL ") + label)
PY
}
echo "======== side menu from another X connection, shared Zink screen"; shots menu2 "menu from a second connection, shared screen" 1
echo "======== side menu from another X connection, a Zink screen per connection"; shots menu2 "menu from a second connection, screen per connection" 0
echo "======== side menu from the same X connection, shared Zink screen"; shots menu1 "menu from the same connection, shared screen" 1
echo "======== unmapped GL child inside the main window (Chromium/ANGLE)"; shots childhidden "child hidden: X shows the main window" 1
echo "== retired: $(grep -c 'Swapchain retired' $OUT/log), kopper notes: $(grep -c 'droiddeck kopper' $OUT/log)"; grep "droiddeck kopper" $OUT/log | sort | uniq -c | head -8 | cut -c1-160
echo "======== mapped GL child over the main window"; shots childshown "child shown: X shows the child" 1
echo "== retired: $(grep -c 'Swapchain retired' $OUT/log), kopper notes: $(grep -c 'droiddeck kopper' $OUT/log)"; grep "droiddeck kopper" $OUT/log | sort | uniq -c | head -8 | cut -c1-160
echo "======== the same window from a second context"; RETIRE_MODE=twocontexts GS_EXTRA="-e" run_gs $GS; report
if ! grep -q "retire: done" $OUT/log && [ "${fails:-0}" = 0 ] && [ "$GS" != /usr/bin/gamescope ]; then
  echo "== the app's gamescope did not run it; its log:"; sed 's/\x1b\[[0-9;]*m//g' $OUT/log | tail -25 | cut -c1-200
  echo "== trying Arch's gamescope"; run_gs /usr/bin/gamescope
fi
grep -q "retire: done" $OUT/log && [ "${fails:-0}" = 0 ]
