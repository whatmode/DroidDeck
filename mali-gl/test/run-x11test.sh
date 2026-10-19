#!/bin/bash
# Phase 3 test: patched gamescope (headless) + patched Xwayland with glamor on
# /dev/mali0 + an X11 GLES client using DRI3.  Log: mali-gl-x11test.log here.
P=$(cd "$(dirname "$0")" && pwd)
if [ ! -t 1 ] && [ -z "${MALI_GL_IN_TERM:-}" ]; then
  export MALI_GL_IN_TERM=1
  if command -v konsole >/dev/null; then exec konsole --hold -e bash "$P/run-x11test.sh"; fi
  if command -v xterm >/dev/null; then exec xterm -hold -e bash "$P/run-x11test.sh"; fi
fi
LOG=$P/mali-gl-x11test.log
unset LIBGL_ALWAYS_SOFTWARE GALLIUM_DRIVER MESA_LOADER_DRIVER_OVERRIDE
export LD_LIBRARY_PATH=$P/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export GBM_BACKENDS_PATH=$P/lib/gbm
export LIBGL_DRIVERS_PATH=$P/lib/dri
export WLR_XWAYLAND=$P/bin/Xwayland
export MESA_LOG_LEVEL=${MESA_LOG_LEVEL:-info}
GS=$P/bin/gamescope
[ -x "$GS" ] || GS=$(command -v gamescope)
{
  echo "### mali-gl x11 test $(date)"
  echo "gamescope: $GS"
  echo "VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-} VK_DRIVER_FILES=${VK_DRIVER_FILES:-}"
  ls -l /dev/mali0 2>&1
  echo "### gamescope (headless) + Xwayland + x11test"
  timeout 90 "$GS" --backend headless -W 1280 -H 720 -w 1280 -h 720 -- "$P/bin/x11test"
  echo "### exit code $?"
  echo "### texture from pixmap (Steam's GL composer path)"
  PAN_MESA_DEBUG=gl3 timeout 180 "$GS" --backend headless -W 1280 -H 720 -w 1280 -h 720 -- "$P/bin/tfptest" 2>&1 | grep -v "MESA: info\|scriptmgr\|^>\|gamescope\]"
  echo "### GL context probe (PAN_MESA_DEBUG=gl3, as in game mode)"
  PAN_MESA_DEBUG=gl3 timeout 90 "$GS" --backend headless -W 1280 -H 720 -w 1280 -h 720 -- "$P/bin/glprobe" 2>&1 | grep -v "MESA: info\|scriptmgr\|^>"
  echo "### GL context probe without gl3"
  timeout 90 "$GS" --backend headless -W 1280 -H 720 -w 1280 -h 720 -- "$P/bin/glprobe" 2>&1 | grep -E "^ *(GLX|EGL)|PAN_MESA"
} 2>&1 | tee "$LOG"
echo
echo "Log saved to $LOG"
read -r -t 600 -p "Press Enter to close" _ || true
