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
}
trap annotate EXIT
grep -q '^DisableSandbox' /etc/pacman.conf || sed -i 's/^\[options\]/[options]\nDisableSandbox/' /etc/pacman.conf
{ for m in https://ca.us.mirror.archlinuxarm.org https://fl.us.mirror.archlinuxarm.org https://de3.mirror.archlinuxarm.org https://nl.mirror.archlinuxarm.org; do echo "Server = $m/\$arch/\$repo"; done; cat /etc/pacman.d/mirrorlist; } > /etc/pacman.d/mirrorlist.new
mv /etc/pacman.d/mirrorlist.new /etc/pacman.d/mirrorlist
pacman -Syu --noconfirm --needed gcc mesa vulkan-swrast libglvnd libx11 xorg-server-xvfb \
  xorg-xwayland weston xorg-xdpyinfo >/tmp/pacman.log 2>&1 || { echo "pacman failed"; tail -15 /tmp/pacman.log; exit 1; }
echo "== mesa $(pacman -Q mesa), $(pacman -Q xorg-xwayland)"
OUT=/tmp/glx; mkdir -p $OUT
gcc -O2 -Wall -o $OUT/glxcheck tools/tests/glx/glxcheck.c -lGL -lX11 || exit 1
gcc -shared -fPIC -O2 -Wall -pthread -o $OUT/libblsession.so tools/linuxfs/preload/*.c -ldl || exit 1

export MESA_LOADER_DRIVER_OVERRIDE=zink GALLIUM_DRIVER=zink LIBGL_DEBUG=verbose
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
  check ok   "$1: Zink, Kopper on" -- --
  check fail "$1: Zink, client sets LIBGL_KOPPER_DISABLE (Steam today, no preload)" -- --steam-env
  check ok   "$1: same, session preload with BL_WEBHELPER_KOPPER=1" LD_PRELOAD=$OUT/libblsession.so BL_WEBHELPER_KOPPER=1 -- --steam-env
  check fail "$1: same preload, BL_WEBHELPER_KOPPER unset (the fix is opt-in)" LD_PRELOAD=$OUT/libblsession.so -- --steam-env
  check ok   "$1: inherited LIBGL_KOPPER_DISABLE, preload on" LD_PRELOAD=$OUT/libblsession.so BL_WEBHELPER_KOPPER=1 LIBGL_KOPPER_DISABLE=true -- --
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
        --use-angle=$angle --ignore-gpu-blocklist --no-first-run --enable-logging=stderr \
        "data:text/html,<canvas id=c></canvas><script>c.getContext('webgl').clearColor(1,0,0,1)</script>" \
        > $OUT/cr.log 2>&1; echo "exit $?"
      grep -E "xcb:|Fatal IO|extension|DRI3|GPU process|gpu_init|GL_RENDERER|ANGLE|zink|kopper|Exiting GPU" $OUT/cr.log | sort | uniq -c | sort -rn | head -25
    done
  done
else
  echo "chromium not installable; webhelper case skipped"
fi
echo "== $fails failing case(s)"
exit $fails
