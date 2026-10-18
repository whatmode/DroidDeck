#!/bin/bash
# Run the mali-gl offscreen GLES test.  Works from wherever the tarball was unpacked.
# The log is written next to this script (mali-gl-test.log) and to the terminal.
P=$(cd "$(dirname "$0")" && pwd)
# Started by a click in the file manager: reopen in a terminal so the output is visible.
if [ ! -t 1 ] && [ -z "${MALI_GL_IN_TERM:-}" ]; then
  export MALI_GL_IN_TERM=1
  if command -v konsole >/dev/null; then exec konsole --hold -e bash "$P/run-gltest.sh"; fi
  if command -v xterm >/dev/null; then exec xterm -hold -e bash "$P/run-gltest.sh"; fi
fi
LOG=$P/mali-gl-test.log
# DroidDeck's desktop forces software GL; this test wants the GPU.
unset LIBGL_ALWAYS_SOFTWARE GALLIUM_DRIVER MESA_LOADER_DRIVER_OVERRIDE
export LD_LIBRARY_PATH=$P/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export GBM_BACKENDS_PATH=$P/lib/gbm
export LIBGL_DRIVERS_PATH=$P/lib/dri
export MESA_LOG_LEVEL=${MESA_LOG_LEVEL:-debug}
export EGL_LOG_LEVEL=${EGL_LOG_LEVEL:-debug}
export MESA_DEBUG=${MESA_DEBUG:-1}
{
  echo "### mali-gl test $(date)"
  uname -a
  ls -l /dev/mali0 2>&1
  echo "### gltest"
  "$P/bin/gltest"
  echo "### exit code $?"
} 2>&1 | tee "$LOG"
echo
echo "Log saved to $LOG"
read -r -t 600 -p "Press Enter to close" _ || true
