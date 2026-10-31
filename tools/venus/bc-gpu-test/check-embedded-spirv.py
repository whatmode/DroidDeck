"""Check that the SPIR-V embedded in the driver is the SPIR-V of the shader that was tested.

The guest ICD has no shader loader, so the compute shader ships as the C array in
vn_droiddeck_bc_spirv.h, generated from bc_s3tc.comp. run.sh tests the .comp; if someone edits the
.comp or the header without regenerating, the shader under test is no longer the one the GPU runs.
That is the same anti-drift rule as genref.py, applied to the other side.

Usage: check-embedded-spirv.py <compiled.spv> <embedded-header.h>
"""
import re
import sys


def spv_words(path):
    data = open(path, "rb").read()
    if len(data) % 4:
        raise SystemExit("%s is not a whole number of words" % path)
    return [int.from_bytes(data[i : i + 4], "little") for i in range(0, len(data), 4)]


def embedded_words(path):
    text = open(path, encoding="utf-8").read()
    m = re.search(r"VN_DD_BC_S3TC_SPIRV_WORDS\s+(\d+)", text)
    if not m:
        raise SystemExit("%s has no VN_DD_BC_S3TC_SPIRV_WORDS" % path)
    declared = int(m.group(1))
    body = re.search(r"vn_dd_bc_s3tc_spirv\[[^\]]*\]\s*=\s*\{(.*?)\};", text, re.S)
    if not body:
        raise SystemExit("%s has no vn_dd_bc_s3tc_spirv[] initializer" % path)
    words = [int(w, 16) for w in re.findall(r"0x([0-9a-fA-F]{8})", body.group(1))]
    if len(words) != declared:
        raise SystemExit(
            "%s declares %d words but lists %d" % (path, declared, len(words))
        )
    return words


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    spv, header = sys.argv[1], sys.argv[2]
    want, got = spv_words(spv), embedded_words(header)
    if want == got:
        print("embedded SPIR-V matches bc_s3tc.comp (%d words)" % len(want))
        return
    n = min(len(want), len(got))
    first = next((i for i in range(n) if want[i] != got[i]), n)
    raise SystemExit(
        "embedded SPIR-V differs from the tested shader: %d words vs %d, first difference at word "
        "%d (shader 0x%08x, driver 0x%08x). Re-run gen-spirv-header.py."
        % (
            len(want),
            len(got),
            first,
            want[first] if first < len(want) else 0,
            got[first] if first < len(got) else 0,
        )
    )


main()
