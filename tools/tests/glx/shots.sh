#!/bin/bash
# shots.sh <out dir> <program...>: runs the program inside gamescope and takes gamescope
# screenshots (GAMESCOPECTRL_REQUEST_SCREENSHOT) while it draws, one every ~100 ms.
out=$1; shift
mkdir -p "$out"
"$@" &
pid=$!
sleep 2
n=0
while kill -0 $pid 2>/dev/null && [ $n -lt 25 ]; do
  rm -f /tmp/gamescope.png
  xprop -root -f GAMESCOPECTRL_REQUEST_SCREENSHOT 32c -set GAMESCOPECTRL_REQUEST_SCREENSHOT 1
  for t in 1 2 3 4 5 6 7 8 9 10; do [ -s /tmp/gamescope.png ] && break; sleep 0.05; done
  [ -s /tmp/gamescope.png ] && sleep 0.05 && cp /tmp/gamescope.png "$out/shot$n.png"
  n=$((n+1))
done
wait $pid
