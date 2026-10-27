"""Extract the S3TC decode functions from vn_droiddeck_bc.c for the GPU-decode differential test.

The functions are taken verbatim from the driver, so the reference the shader is compared against
cannot drift from the decoder that ships -- the same reason bc-band-test/genbcband.py extracts
rather than copies. bc-band-test's header records what a hand-copied reference cost: it passed
while the phone showed a cut-in-half map.

Usage: genref.py <vn_droiddeck_bc.c> <out.c>
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


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src = open(sys.argv[1], encoding="utf-8").read().splitlines()

    parts = [extract(src, fn) for fn in
             ["bc_expand565", "bc_alpha_palette", "bc_alpha_bits", "bc_decode_s3tc"]]
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write('#include "ref_s3tc.h"\n\n' + "\n\n".join(parts) + "\n")
    print("extracted %d blocks from %s" % (len(parts), sys.argv[1]))


main()
