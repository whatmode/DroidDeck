#!/bin/bash
# Steam client GLX start-up on Zink without DRI3, the way the Venus bridge session runs it:
# GL on Zink (here over lavapipe instead of the bridge), an X server without DRI3 (Xvfb, and
# Xwayland -shm on a headless weston), the session preload. Runs in Arch Linux ARM, the phone
# rootfs' distribution, so Mesa and libglvnd match. Each case must give the expected result.
set -uo pipefail
pacman -Sy --noconfirm --needed gcc mesa vulkan-swrast libglvnd libx11 xorg-server-xvfb \
  xorg-xwayland weston xorg-xdpyinfo >/dev/null 2>&1 || { echo "pacman failed"; exit 1; }
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
echo "== $fails failing case(s)"
exit $fails
