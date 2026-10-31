"""Check that the BC7 tables in the shader are the ones Mesa's header produces.

The shader cannot include a header, so the partition/anchor/mode tables are copied into bc_s3tc.comp
by gen-bc7-tables.py.  A wrong row does not crash: it puts a texel in the wrong subset or reads its
index from the wrong bit, which is a plausible wrong picture -- the same failure bc-band-test's
header records.  So the tables are regenerated from the Mesa checkout and compared, rather than
trusted.

Usage: check-bc7-tables.py <bc_s3tc.comp> <regenerated.glsl>
"""
import sys

BEGIN = "/* BC7-TABLES-BEGIN (gen-bc7-tables.py) */"
END = "/* BC7-TABLES-END */"


def region(text, path):
    i = text.find(BEGIN)
    j = text.find(END)
    if i < 0 or j < 0 or j < i:
        raise SystemExit("%s has no BC7-TABLES-BEGIN/END block" % path)
    # drop the generator's own banner line so only the tables are compared
    body = text[i + len(BEGIN):j].split("\n")
    body = [l for l in body if "Generated from texcompress_bptc_tmp.h" not in l]
    return "\n".join(l.strip() for l in body if l.strip())


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    comp, gen = sys.argv[1], sys.argv[2]
    a = region(open(comp, encoding="utf-8").read(), comp)
    b = region(open(gen, encoding="utf-8").read(), gen)
    if a == b:
        print("BC7 tables in bc_s3tc.comp match texcompress_bptc_tmp.h")
        return
    al, bl = a.split("\n"), b.split("\n")
    n = min(len(al), len(bl))
    first = next((i for i in range(n) if al[i] != bl[i]), n)
    raise SystemExit(
        "BC7 tables differ from texcompress_bptc_tmp.h at line %d:\n  shader: %s\n  header: %s\n"
        "Re-run gen-bc7-tables.py." % (
            first + 1,
            al[first] if first < len(al) else "<missing>",
            bl[first] if first < len(bl) else "<missing>",
        )
    )


main()
