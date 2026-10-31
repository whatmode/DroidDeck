"""Extract Mesa's BC7 decoder from texcompress_bptc_tmp.h for the GPU-decode differential test.

The shader's BC7 path and decompress_rgba_unorm_block() are two implementations of one contract.
The reference is taken verbatim from Mesa's own header -- the same decoder the CPU path runs when a
BC7 upload falls through util_format_translate -- so it cannot drift from what ships.  The tables
(partition, anchor, mode) come out of the same file as the shader's copy, which gen-bc7-tables.py
generates from, so both sides are known to be the same numbers.

Usage: genref-bc7.py <mesa-src> <out.c>
"""
import re
import sys

FUNCS = [
    "extract_bits", "expand_component", "extract_unorm_endpoints", "is_anchor",
    "count_anchors_before_texel", "interpolate", "apply_rotation",
    "decompress_rgba_unorm_block", "decompress_rgba_unorm",
]

TABLES = ["partition_table1", "partition_table2", "anchor_indices", "bptc_unorm_modes"]

PREAMBLE = r'''/* Reference BC7 decoder, extracted verbatim from Mesa's
 * src/util/format/texcompress_bptc_tmp.h by genref-bc7.py.  Do not edit. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef MIN2
#define MIN2(a, b) ((a) < (b) ? (a) : (b))
#endif

/* ffs() comes from glibc (strings.h).  The unorm decode path touches nothing else outside the
 * header: _mesa_half_to_float() is only used by the BC6H float decoder, which this reference does
 * not include. */

#define BLOCK_SIZE 4
#define N_PARTITIONS 64
#define BLOCK_BYTES 16

'''


def block(src, name):
    """Return the function definition named `name`, signature through closing brace."""
    m = re.search(r"\n" + re.escape(name) + r"\s*\([^;]*?\)\s*\n?\{", src, re.S)
    if not m:
        raise SystemExit("function not found: " + name)
    i = m.start() + 1
    # walk back over the return type / static qualifier on preceding lines
    while True:
        prev = src.rfind("\n", 0, i - 1)
        line = src[prev + 1:i].strip()
        if not line or line.endswith((";", "}", "{")) or line.startswith("/*") or line.startswith("*"):
            break
        i = prev + 1
    j = src.index("{", m.end() - 1)
    depth, k = 0, j
    while k < len(src):
        depth += src[k].count("{") - src[k].count("}")
        if depth == 0:
            break
        k += 1
    return src[i:k + 1]


def table(src, name):
    m = re.search(r"\nstatic const [^\n]*\n?" + re.escape(name) +
                  r"\s*(\[[^\]]*\])+\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        raise SystemExit("table not found: " + name)
    return src[m.start() + 1:m.end()]


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src = open(sys.argv[1] + "/src/util/format/texcompress_bptc_tmp.h", encoding="utf-8").read()
    parts = [PREAMBLE]
    parts.append("struct bptc_unorm_mode {\n"
                 "   int n_subsets;\n   int n_partition_bits;\n   bool has_rotation_bits;\n"
                 "   bool has_index_selection_bit;\n   int n_color_bits;\n   int n_alpha_bits;\n"
                 "   bool has_endpoint_pbits;\n   bool has_shared_pbits;\n   int n_index_bits;\n"
                 "   int n_secondary_index_bits;\n};\n")
    for t in TABLES:
        parts.append(table(src, t))
    for f in FUNCS:
        parts.append(block(src, f))
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write("\n".join(parts) + "\n")
    print("extracted %d tables and %d functions from %s" % (len(TABLES), len(FUNCS), sys.argv[1]))


main()
