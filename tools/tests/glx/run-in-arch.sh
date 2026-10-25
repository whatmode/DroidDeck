#!/bin/bash
# Steam client GLX start-up on Zink without DRI3, the way the Venus bridge session runs it:
# GL on Zink (here over lavapipe instead of the bridge), an X server without DRI3 (Xvfb, and
# Xwayland -shm on a headless weston), the session preload. Runs in Arch Linux ARM, the phone
# rootfs' distribution, so Mesa and libglvnd match. Each case must give the expected result.
set -uo pipefail
# The job log is not readable from where results are checked; annotations are. Everything this
# script prints also goes to report.txt, sent as notice annotations (chunks) at exit.
REPORT=/tmp/glx-report.txt; : > $REPORT
exec 3>&1
exec > >(tee -a $REPORT) 2>&1
annotate() {
  sleep 1
  local n=0
  split -l 70 $REPORT /tmp/glx-chunk.
  for f in /tmp/glx-chunk.*; do
    n=$((n+1)); [ $n -gt 9 ] && break
    printf '::notice title=glx report %d::%s\n' $n "$(sed 's/%/%25/g' "$f" | cut -c1-300 | awk 'BEGIN{ORS="%0A"}{print}')" >&3
  done
  echo "---- $1: audited 'steam', GLX call trace (expect FIRST call line and a context)"
  LD_AUDIT=$OUT/libblsession.so $OUT/audit3/steam > $OUT/log 2>&1
  grep "glxaudit\|program:" $OUT/log | grep -v "env at" | head -10
  if grep -q "FIRST GLX call" $OUT/log && grep -q "program: version 1.4" $OUT/log && ! grep -q "ctx (nil)" $OUT/log; then echo "PASS $1: trace"; else echo "FAIL $1: trace"; fails=$((fails+1)); fi
  echo "---- $1: audited 'steam' that sets LIBGL_KOPPER_DISABLE itself (report)"
  LD_AUDIT=$OUT/libblsession.so $OUT/audit2/steam > $OUT/log 2>&1; echo "exit $?"
  grep "glxaudit\|program got" $OUT/log | grep -v "env at" | head -12
}
trap annotate EXIT
grep -q '^DisableSandbox' /etc/pacman.conf || sed -i 's/^\[options\]/[options]\nDisableSandbox/' /etc/pacman.conf
{ for m in https://ca.us.mirror.archlinuxarm.org https://fl.us.mirror.archlinuxarm.org https://de3.mirror.archlinuxarm.org https://nl.mirror.archlinuxarm.org; do echo "Server = $m/\$arch/\$repo"; done; cat /etc/pacman.d/mirrorlist; } > /etc/pacman.d/mirrorlist.new
mv /etc/pacman.d/mirrorlist.new /etc/pacman.d/mirrorlist
pacman -Syu --noconfirm --needed gcc mesa vulkan-swrast vulkan-tools vulkan-headers strace libglvnd libx11 xorg-server-xvfb \
  xorg-xwayland weston xorg-xdpyinfo >/tmp/pacman.log 2>&1 || { echo "pacman failed"; tail -15 /tmp/pacman.log; exit 1; }
echo "== mesa $(pacman -Q mesa), $(pacman -Q xorg-xwayland)"
OUT=/tmp/glx; mkdir -p $OUT
gcc -O2 -Wall -o $OUT/glxcheck tools/tests/glx/glxcheck.c -lGL -lX11 || exit 1
printf '#include <stdlib.h>\nint main(void) { exit(1); }\n' > $OUT/steam.c && gcc -o $OUT/steam $OUT/steam.c || exit 1
gcc -O2 -Wall -o $OUT/xcblocal tools/tests/glx/xcblocal.c -ldl || exit 1
mkdir -p $OUT/audit3 && gcc -O2 -Wall -o $OUT/audit3/steam tools/tests/glx/audittrace.c -ldl || exit 1
mkdir -p $OUT/audit2 && gcc -O2 -Wall -o $OUT/audit2/steam tools/tests/glx/auditkopper.c -ldl || exit 1
mkdir -p $OUT/audit && gcc -O2 -Wall -o $OUT/audit/steam tools/tests/glx/auditcheck.c -ldl || exit 1
gcc -shared -fPIC -O2 -Wall -pthread -o $OUT/libblsession.so tools/linuxfs/preload/*.c -ldl || exit 1

# Zink takes a CPU Vulkan device (lavapipe) only when software GL is asked for, and asking
# (LIBGL_ALWAYS_SOFTWARE) changes the GLX path. The phone's bridge device is a GPU, so a
# test-only layer reports lavapipe as one: GLX then goes DRI3 first, then Kopper, as there.
gcc -shared -fPIC -O2 -Wall -o $OUT/libgputype.so tools/tests/glx/gputype_layer.c || exit 1
mkdir -p $OUT/layers && cp tools/tests/glx/gputype_layer.json $OUT/layers/
export VK_LAYER_PATH=$OUT/layers VK_INSTANCE_LAYERS=VK_LAYER_DROIDDECK_gputype
# LIBGL_KOPPER_DRI2=true as the app sets it for the guest: without DRI3 multibuffer Mesa's GLX
# otherwise drops Zink before trying Kopper (glxext.c, "DRI3 not available").
export MESA_LOADER_DRIVER_OVERRIDE=zink GALLIUM_DRIVER=zink LIBGL_DEBUG=verbose LIBGL_KOPPER_DRI2=true
fails=0
check() {  # expect(ok|fail) label env... -- args
  local expect=$1 label=$2; shift 2
  local envs=(); while [ "$1" != -- ]; do envs+=("$1"); shift; done; shift
  echo "---- $label (expect $expect)"
  env "${envs[@]}" timeout 60 $OUT/glxcheck "$@" > $OUT/log 2>&1; local rc=$?
  grep -E "glxcheck|droiddeck|kopper|zink|failed to load|MESA-LOADER" $OUT/log | head -20
  local got=fail; [ $rc = 0 ] && got=ok
  if [ $got = $expect ]; then echo "PASS $label"; else echo "FAIL $label: got $got (rc $rc)"; tail -20 $OUT/log; fails=$((fails+1)); fi
}

run_cases() {
  echo "== X extensions on $DISPLAY: $(xdpyinfo | sed -n '/number of extensions/,/default screen/p' | grep -v 'default screen\|number of' | tr -s ' \n' ' ')"
  check ok   "$1: Zink with Kopper" -- --
  # Steam's client sets LIBGL_KOPPER_DISABLE=true in its own process. With LIBGL_KOPPER_DRI2
  # GLX still finds visuals, so that is not why it exits on "glXChooseVisual failed".
  check ok   "$1: Zink, client sets LIBGL_KOPPER_DISABLE" -- --steam-env
  # Without the driver Mesa's GLX has no configs at all: what the in-process report must show.
  check fail "$1: Zink with no Vulkan driver" VK_DRIVER_FILES=/nonexistent.json -- --
  # The session preload's report from a failing Steam client process (glxreport.c).
  echo "---- $1: in-process GLX report of an exiting 'steam' (expect visuals)"
  BL_STEAM_GLX_REPORT=1 LD_PRELOAD=$OUT/libblsession.so $OUT/steam > $OUT/log 2>&1
  grep "glxreport" $OUT/log | grep -v " env \| loaded " | head -12
  if grep -q "glXChooseVisual(rgba): visual" $OUT/log; then echo "PASS $1: report"; else echo "FAIL $1: report"; fails=$((fails+1)); fi
  # X libraries opened RTLD_LOCAL (as Steam's client does) under the session preload: its xcb
  # wrappers must pass requests on (they used to send nothing, so GLX found no GLX).
  echo "---- $1: xcb requests with the X libraries opened locally, session preload (expect replies)"
  BL_XCB_TRACE=1 LD_PRELOAD=$OUT/libblsession.so $OUT/xcblocal > $OUT/log 2>&1; rc=$?
  cat $OUT/log | head -5
  if [ $rc = 0 ]; then echo "PASS $1: xcb local"; else echo "FAIL $1: xcb local (rc $rc)"; fails=$((fails+1)); fi
  # The preload as an audit library (glxaudit.c): Steam's own call seen and rescued.
  for how in "dlsym 0" "gpa 0" "dlsym 3"; do
    echo "---- $1: audited 'steam', lookup $how (expect rescued)"
    LD_AUDIT=$OUT/libblsession.so $OUT/audit/steam $how > $OUT/log 2>&1
    grep "glxaudit\|program got" $OUT/log | head -10
    if grep -q "program got a visual" $OUT/log; then echo "PASS $1: audit $how"; else echo "FAIL $1: audit $how"; tail -5 $OUT/log; fails=$((fails+1)); fi
  done
}

Xvfb :7 -screen 0 640x480x24 >/dev/null 2>&1 & sleep 2
DISPLAY=:7; export DISPLAY; run_cases Xvfb; kill %1 2>/dev/null

export XDG_RUNTIME_DIR=/tmp/xdg; mkdir -p -m 700 $XDG_RUNTIME_DIR
weston --backend=headless --socket=wl-glx --idle-time=0 >/tmp/weston.log 2>&1 & sleep 3
if [ -S $XDG_RUNTIME_DIR/wl-glx ]; then
  WAYLAND_DISPLAY=wl-glx Xwayland :8 -shm >/tmp/xwl.log 2>&1 & sleep 3
  DISPLAY=:8; export DISPLAY; run_cases "Xwayland -shm"
else
  echo "weston headless did not start; Xwayland case skipped"; tail -5 /tmp/weston.log
fi
# Chromium (Steam's webhelper is CEF) drawing through ANGLE on Zink on the same DRI3-less
# servers, with the session preload's X request trace: any request for an extension the server
# lacks is named with its caller. Reported, not gated: Steam's CEF is an older Chromium.
if pacman -S --noconfirm --needed chromium >/dev/null 2>&1; then
  echo "== $(chromium --version 2>/dev/null)"
  for d in :7 :8; do
    if [ $d = :7 ]; then Xvfb :7 -screen 0 640x480x24 >/dev/null 2>&1 & sleep 2; fi
    for angle in gl vulkan; do
      echo "---- chromium on $d, ANGLE on $angle (report)"
      rm -rf /tmp/cr; DISPLAY=$d LD_PRELOAD=$OUT/libblsession.so BL_XCB_TRACE=1 BL_WEBHELPER_KOPPER=1 \
        timeout 25 chromium --no-sandbox --user-data-dir=/tmp/cr --ozone-platform=x11 --use-gl=angle \
        --use-angle=$angle --ignore-gpu-blocklist --no-first-run --enable-logging=stderr --disable-dev-shm-usage \
        "data:text/html,<canvas id=c></canvas><script>c.getContext('webgl').clearColor(1,0,0,1)</script>" \
        > $OUT/cr.log 2>&1; echo "exit $?"
      grep -E "xcb:|Fatal IO|extension|DRI3|GPU process|gpu_init|GL_RENDERER|ANGLE|zink|kopper|Exiting GPU" $OUT/cr.log | sort | uniq -c | sort -rn | head -15
      grep -v "Fontconfig\|dbus\|bus.cc" $OUT/cr.log | tail -8 | cut -c1-220
    done
  done
else
  echo "chromium not installable; webhelper case skipped"
fi
# The bridge driver's own GL part (Zink, GLX, EGL from its Mesa), selected the way the app does
# from the driver's gl/env: GL must come from it and say how each display was set up.
zip=$(ls drv/Venus-Bridge-guest-*.zip 2>/dev/null | head -1)
if [ -n "$zip" ] && pacman -S --noconfirm --needed unzip >/dev/null 2>&1 && unzip -q -o "$zip" 'gl/*' -d $OUT/drv 2>/dev/null; then
  G=$OUT/drv/gl
  echo "== driver GL part from $(basename "$zip"): $(ls $G | tr '\n' ' ')"
  genv=()
  while IFS= read -r line; do
    case "$line" in ''|'#'*) continue ;; esac
    k=${line%%=*}; v=${line#*=}; v=${v//@GL@/$G}
    if [ "${k%+}" != "$k" ]; then k=${k%+}; v="${!k:+${!k}:}$v"; fi
    genv+=("$k=$v")
  done < $G/env
  echo "== driver GL env: ${genv[*]}"
  echo "---- driver GL, GLX version asked over Xlib (the fallback for a display whose xcb side fails)"
  DISPLAY=:8 env "${genv[@]}" DROIDDECK_GLX_FORCE_XLIB_VERSION=1 timeout 60 $OUT/glxcheck > $OUT/log 2>&1; rc=$?
  grep -E "droiddeck glx|RESULT" $OUT/log | head -30
  if [ $rc = 0 ] && grep -q "QueryVersion over Xlib: reply, 1.4" $OUT/log && grep -q "GetInputFocus): reply" $OUT/log \
     && grep -q "xcb_query_extension_reply comes from .*libxcb" $OUT/log; then echo "PASS driver GL Xlib version"; else echo "FAIL driver GL Xlib version (rc $rc)"; tail -10 $OUT/log; fails=$((fails+1)); fi
  for d in :7 :8; do
    echo "---- driver GL on $d (expect ok, GL from the driver's Mesa, set-up lines)"
    DISPLAY=$d env "${genv[@]}" timeout 60 $OUT/glxcheck > $OUT/log 2>&1; rc=$?
    grep -E "glxcheck|droiddeck glx|failed|error" $OUT/log | head -14
    if [ $rc = 0 ] && grep -q "droiddeck glx" $OUT/log && grep -q "renderer zink" $OUT/log; then echo "PASS driver GL on $d"; else echo "FAIL driver GL on $d (rc $rc)"; tail -15 $OUT/log; fails=$((fails+1)); fi
  done
else
  echo "== no bridge driver GL part to test"
fi
echo "== $fails failing case(s)"
exit $fails
