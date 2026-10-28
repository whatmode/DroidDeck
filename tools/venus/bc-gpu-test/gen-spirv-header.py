"""Generate the embedded SPIR-V header for the guest driver from bc_s3tc.comp.

The guest ICD has no way to load a shader at runtime -- the SPIR-V has to be a C array in the
driver -- so the .comp is compiled here and emitted as vn_droiddeck_bc_spirv.h. That array is the
copy the GPU actually runs, so it is the one that has to match the CPU decoder; run.sh compiles
the .comp again and checks the array in the patch is the same words, which is what keeps the
generated header from drifting away from the shader that was verified.

Usage: gen-spirv-header.py <bc_s3tc.comp> <out.h> [glslangValidator]
"""
import os
import subprocess
import sys
import tempfile


def words_from_spv(path):
    data = open(path, "rb").read()
    if len(data) % 4:
        raise SystemExit("SPIR-V is not a whole number of words")
    if data[:4] != b"\x03\x02\x23\x07":
        raise SystemExit("bad SPIR-V magic")
    return [int.from_bytes(data[i : i + 4], "little") for i in range(0, len(data), 4)]


def emit(words, out):
    with open(out, "w", encoding="utf-8") as f:
        f.write(
            "/* Generated from bc_s3tc.comp by tools/venus/bc-gpu-test/gen-spirv-header.py.\n"
            " * Do not edit: run.sh recompiles the .comp and fails if this array is not the same\n"
            " * words.  The shader decodes one 4x4 block per invocation: BC1/BC2/BC3 by kind, and\n"
            " * BC7 (kind 4), which reads the block as bytes because it has no fixed field layout. */\n"
            "#ifndef VN_DROIDDECK_BC_SPIRV_H\n"
            "#define VN_DROIDDECK_BC_SPIRV_H\n"
            "\n"
            "#include <stdint.h>\n"
            "\n"
            "#define VN_DD_BC_S3TC_SPIRV_WORDS %d\n"
            "\n"
            "static const uint32_t vn_dd_bc_s3tc_spirv[VN_DD_BC_S3TC_SPIRV_WORDS] = {\n" % len(words)
        )
        for i in range(0, len(words), 8):
            f.write("   " + " ".join("0x%08x," % w for w in words[i : i + 8]) + "\n")
        f.write("};\n\n#endif /* VN_DROIDDECK_BC_SPIRV_H */\n")


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    comp, out = sys.argv[1], sys.argv[2]
    glslang = sys.argv[3] if len(sys.argv) > 3 else "glslangValidator"
    with tempfile.TemporaryDirectory() as d:
        spv = os.path.join(d, "bc_s3tc.spv")
        r = subprocess.run([glslang, "-V", comp, "-o", spv], capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stdout + r.stderr)
            raise SystemExit("glslangValidator failed on " + comp)
        emit(words_from_spv(spv), out)
    print("wrote %s" % out)


main()
