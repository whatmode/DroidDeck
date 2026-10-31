"""Extract the BC decode functions from vn_droiddeck_bc.c into a header the band test compiles.

The functions are taken verbatim, so the test cannot drift from the driver the way a hand-copied
"verbatim copy" does: the BC5 source stride was `s += block` in the copy the test used and
`s += 8` in the driver that shipped, which is why the copy passed and the phone did not.

Usage: genbcband.py <vn_droiddeck_bc.c> <out.h>
"""
import re
import sys


def name_line(src, name):
    for i, line in enumerate(src):
        if re.match(r"^" + re.escape(name) + r"\s*\(", line) and not line.rstrip().endswith(";"):
            return i
    raise SystemExit("not found: " + name)


def sig_start(src, i):
    """Walk back over the return type, which Mesa puts on its own line."""
    j = i
    while j > 0 and src[j - 1].strip() and not src[j - 1].rstrip().endswith((";", "}", "{")):
        j -= 1
    return j


def extract(src, name):
    i = name_line(src, name)
    start = sig_start(src, i)
    j = i
    while "{" not in src[j]:
        j += 1
    depth, k = 0, j
    while k < len(src):
        depth += src[k].count("{") - src[k].count("}")
        if depth == 0:
            break
        k += 1
    return "\n".join(src[start : k + 1])


def extract_struct(src, name):
    for i, line in enumerate(src):
        if line.startswith("struct " + name + " {"):
            k = i
            while not src[k].startswith("};"):
                k += 1
            return "\n".join(src[i : k + 1])
    raise SystemExit("struct not found: " + name)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src = open(sys.argv[1], encoding="utf-8").read().splitlines()

    parts = [extract_struct(src, "vn_dd_bc_job"), extract_struct(src, "vn_dd_bc_task")]
    for fn in ["bc_expand565", "bc_alpha_palette", "bc_alpha_bits",
               "bc_decode_s3tc", "bc_decode_rgtc", "bc_decode_task"]:
        parts.append(extract(src, fn))

    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write("\n\n".join(parts) + "\n")
    print("extracted %d blocks from %s" % (len(parts), sys.argv[1]))


main()
