#!/bin/bash
# Run the mali-gl offscreen GLES test.  Works from wherever the tarball was unpacked.
# The log is written next to this script (mali-gl-test.log) and to the terminal.
P=$(cd "$(dirname "$0")" && pwd)
LOG=$P/mali-gl-test.log
export LD_LIBRARY_PATH=$P/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export GBM_BACKENDS_PATH=$P/lib/gbm
export LIBGL_DRIVERS_PATH=$P/lib/dri
export MESA_LOG_LEVEL=${MESA_LOG_LEVEL:-info}
export EGL_LOG_LEVEL=${EGL_LOG_LEVEL:-debug}
export MESA_DEBUG=${MESA_DEBUG:-1}
{
  echo "### mali-gl test $(date)"
  uname -a
  ls -l /dev/mali* 2>&1
  echo "### gltest"
  timeout 120 "$P/bin/gltest"
  echo "### exit code $?"
} 2>&1 | tee "$LOG"
echo
echo "Log saved to $LOG"
read -r -t 600 -p "Press Enter to close" _ || true
