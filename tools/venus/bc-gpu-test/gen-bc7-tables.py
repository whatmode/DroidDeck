"""Generate the BC7 mode/partition/anchor tables for the GPU decode shader.

The tables come from Mesa's own BC7 decoder, src/util/format/texcompress_bptc_tmp.h, so the shader
cannot disagree with the CPU path about which texel a partition puts in which subset.  A wrong
anchor index or partition row does not crash: it reads a plausible wrong texel, which is the same
class of failure bc-band-test's header records.  So the tables are generated here, embedded in the
shader between the BEGIN/END markers, and run.sh regenerates and diffs them.

Usage: gen-bc7-tables.py <mesa-src> [out.glsl]
"""
import re
import sys


def read_modes(src):
    m = re.search(r"bptc_unorm_modes\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        raise SystemExit("bptc_unorm_modes not found")
    rows = []
    for body in re.findall(r"/\*\s*\d+\s*\*/\s*\{([^}]*)\}", m.group(1)):
        vals = [v.strip() for v in body.split(",")]
        nums = []
        for v in vals:
            if v in ("true", "false"):
                nums.append(1 if v == "true" else 0)
            else:
                nums.append(int(v))
        rows.append(nums)
    if len(rows) != 8:
        raise SystemExit("expected 8 BC7 modes, got %d" % len(rows))
    return rows


def read_hex_array(src, name):
    m = re.search(r"\b" + name + r"\s*\[[^\]]*\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit(name + " not found")
    return [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", m.group(1))]


def read_anchors(src):
    m = re.search(r"anchor_indices\[\]\[N_PARTITIONS\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        raise SystemExit("anchor_indices not found")
    rows = [[int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", r)]
            for r in re.findall(r"\{([^{}]*)\}", m.group(1))]
    if len(rows) != 3 or any(len(r) != 64 for r in rows):
        raise SystemExit("anchor_indices is not 3x64")
    return rows


def emit_int(name, vals, per=8):
    out = ["const uint %s[%d] = uint[%d](" % (name, len(vals), len(vals))]
    for i in range(0, len(vals), per):
        out.append("   " + ", ".join("%du" % v for v in vals[i:i + per]) +
                   ("," if i + per < len(vals) else ""))
    out.append(");")
    return "\n".join(out)


def emit_u32(name, vals, per=4):
    out = ["const uint %s[64] = uint[64](" % name]
    for i in range(0, len(vals), per):
        out.append("   " + ", ".join("0x%08xu" % v for v in vals[i:i + per]) +
                   ("," if i + per < len(vals) else ""))
    out.append(");")
    return "\n".join(out)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    src = open(sys.argv[1] + "/src/util/format/texcompress_bptc_tmp.h", encoding="utf-8").read()
    modes = read_modes(src)
    cols = ["BC7_NSUBSETS", "BC7_NPART", "BC7_ROT", "BC7_IDSEL", "BC7_COLORBITS", "BC7_ALPHABITS",
            "BC7_EPBITS", "BC7_SHAREDPBITS", "BC7_INDEXBITS", "BC7_SECINDEXBITS"]
    lines = ["/* BC7-TABLES-BEGIN (gen-bc7-tables.py) */",
             "/* Generated from texcompress_bptc_tmp.h by gen-bc7-tables.py. Do not edit. */"]
    for c in range(len(cols)):
        lines.append(emit_int(cols[c], [row[c] for row in modes]))
    lines.append(emit_u32("BC7_PART1", read_hex_array(src, "partition_table1")))
    lines.append(emit_u32("BC7_PART2", read_hex_array(src, "partition_table2")))
    for name, row in zip(["BC7_ANCHOR_A", "BC7_ANCHOR_B", "BC7_ANCHOR_C"], read_anchors(src)):
        lines.append(emit_int(name, row, per=16))
    lines.append("/* BC7-TABLES-END */")
    text = "\n".join(lines) + "\n"
    if len(sys.argv) > 2:
        open(sys.argv[2], "w", encoding="utf-8").write(text)
        print("wrote %s (%d modes, 64 partitions)" % (sys.argv[2], len(modes)))
    else:
        sys.stdout.write(text)


main()
