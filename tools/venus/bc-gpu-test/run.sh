#!/usr/bin/env bash
# run.sh <mesa-src> - check the GPU BC decode shader against the driver's own CPU decoder.
#
# The shader (bc_s3tc.comp) and bc_decode_s3tc() in vn_droiddeck_bc.c are two implementations of
# one contract: a block of BC1/BC2/BC3 must decode to the same RGBA8 texels either way. Testing
# the shader by reading it is not enough -- the alpha index field of BC3 is 48 bits held in a
# 16-bit low word and a 32-bit high one, and getting the straddle boundary wrong reads zeros for
# every index past the fifth. That is a plausible-looking wrong alpha, not a crash.
#
# So this compiles bc_s3tc.comp to SPIR-V with glslangValidator, translates that SPIR-V to C++
# with SPIRV-Cross, runs the result on the host, and diffs whole images against the reference
# decoder extracted verbatim from the driver (genref.py). The artifact tested is therefore the one
# that ships, not a re-implementation -- the same anti-drift rule bc-band-test/run.sh follows.
#
# Needs glslangValidator, the SPIRV-Cross CLI, the SPIRV-Cross source tree (its shader runtime is
# header-only, under include/spirv_cross) and glm. Set SPIRV_CROSS_DIR (a source checkout or
# install prefix), GLM_DIR, and GLSLANG if they are not in the usual places; CI builds the CLI and
# clones glm, then passes the first two.
set -euo pipefail
MESA=$(cd "$1" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)

GLSLANG=${GLSLANG:-glslangValidator}
command -v "$GLSLANG" >/dev/null || { echo "glslangValidator not found (set GLSLANG)"; exit 1; }

SC=${SPIRV_CROSS_DIR:-}
SC_BIN=${SPIRV_CROSS_BIN:-}
if [ -z "$SC_BIN" ]; then
  for c in "$SC/spirv-cross" "$SC/build/spirv-cross" "$SC/bin/spirv-cross"; do
    [ -x "$c" ] && SC_BIN="$c" && break
  done
fi
[ -n "$SC_BIN" ] || SC_BIN=$(command -v spirv-cross || true)
[ -n "$SC_BIN" ] && [ -x "$SC_BIN" ] || { echo "spirv-cross not found (set SPIRV_CROSS_DIR or SPIRV_CROSS_BIN)"; exit 1; }
[ -n "$SC" ] || SC=$(cd "$(dirname "$SC_BIN")/.." && pwd)
SC_INC=""
for d in "$SC/include" "$SC"; do [ -d "$d/spirv_cross" ] && SC_INC="$d" && break; done
[ -n "$SC_INC" ] || { echo "spirv_cross headers not found under $SC (set SPIRV_CROSS_DIR)"; exit 1; }

GLM=${GLM_DIR:-/usr/include}
[ -f "$GLM/glm/glm.hpp" ] || [ -f "$GLM/glm.hpp" ] || { echo "glm not found (set GLM_DIR)"; exit 1; }

"$GLSLANG" -V "$HERE/bc_s3tc.comp" -o "$OUT/bc_s3tc.spv" > "$OUT/glslang.log" 2>&1 \
  || { echo "glslangValidator failed"; cat "$OUT/glslang.log"; exit 1; }

"$SC_BIN" "$OUT/bc_s3tc.spv" --cpp > "$OUT/bc_s3tc_shader.cpp" 2> "$OUT/spirv-cross.log" \
  || { echo "spirv-cross failed"; cat "$OUT/spirv-cross.log"; exit 1; }

python3 "$HERE/genref.py" "$MESA/src/virtio/vulkan/vn_droiddeck_bc.c" "$OUT/ref_s3tc.c"

# The guest ICD cannot load a shader at runtime, so the SPIR-V is embedded in the driver as a C
# array by gen-spirv-header.py. That array is what the GPU runs; if it drifts from bc_s3tc.comp,
# the tested shader is no longer the shipped one. Compare the words.
python3 "$HERE/check-embedded-spirv.py" "$OUT/bc_s3tc.spv" \
  "$MESA/src/virtio/vulkan/vn_droiddeck_bc_spirv.h"

cp "$HERE/ref_s3tc.h" "$HERE/test_s3tc.cpp" "$OUT/"
g++ -O1 -g -std=c++14 -I"$OUT" -I"$SC_INC" -I"$GLM" \
  -o "$OUT/test_s3tc" "$OUT/test_s3tc.cpp" -lpthread
"$OUT/test_s3tc"
