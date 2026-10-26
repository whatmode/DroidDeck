#!/usr/bin/env bash
# run.sh <mesa-src> <build-dir> - check the BC band split against the driver's own decode code.
#
# The BC fast paths (bc_decode_s3tc/bc_decode_rgtc) and the row-band split meet in bc_decode_task.
# Testing them apart is not enough: the decoders were correct without bands, the split was correct
# with the generic decoder, and the fast paths still ignored the band offset, so every band after
# the first overwrote the top rows and the rest of the texture stayed staging garbage. On the
# phone that reads as "the map is cut in half".
#
# genbcband.py pulls bc_decode_task and the decoders out of <mesa-src>'s vn_droiddeck_bc.c verbatim,
# so the check runs the real code rather than a copy that can drift. A copy did drift once: it had
# the correct BC5 stride while the driver shipped half of it, so the copy passed and the phone did
# not.
#
# <build-dir> is a build dir made by check-guest.sh; its compile_commands.json gives the include
# paths and defines, and its libmesa_util.a gives util_format_translate for the reference decode.
set -euo pipefail
MESA=$(cd "$1" && pwd)
B=$(cd "$2" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)

python3 "$HERE/genbcband.py" "$MESA/src/virtio/vulkan/vn_droiddeck_bc.c" "$OUT/bcband_extracted.h"

FLAGS=$(python3 - "$B" <<'PY'
import json, shlex, sys
for e in json.load(open(sys.argv[1] + "/compile_commands.json")):
    if e["file"].endswith("vn_droiddeck_bc.c"):
        toks = shlex.split(e["command"])
        print(" ".join(t for t in toks[1:] if t.startswith(("-D", "-I", "-std"))))
        break
else:
    raise SystemExit("vn_droiddeck_bc.c not in the build's compile_commands.json")
PY
)

# The extracted code includes the build's own paths, which are relative to the build dir.
cd "$B"
# The reference decode comes from Mesa's util, which check-guest.sh's object list does not build.
ninja src/util/libmesa_util.a src/util/libmesa_util_simd.a src/c11/impl/libmesa_util_c11.a \
  > "$OUT/ninja.log" 2>&1 || { tail -20 "$OUT/ninja.log"; exit 1; }
gcc -O1 -g $FLAGS -I"$OUT" -o "$OUT/bcband" "$HERE/bcband.c" \
  src/util/libmesa_util.a src/util/libmesa_util_simd.a src/c11/impl/libmesa_util_c11.a \
  -lpthread -lm
"$OUT/bcband"
