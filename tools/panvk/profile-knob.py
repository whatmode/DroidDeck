#!/usr/bin/env python3
"""Set one field of panvk_kbase_profile_for()'s initializer, for a behaviour bisection.

Patch 0005 moved the old per-game env switches (bc_compute, no_crc, no_bc_emul, no_gs,
gpu_prerast, bc_wide, wsi_no_afbc, no_wb_mmap, cached_before_coherent) into the compiled
profile, so changing one is an edit plus a rebuild. This rewrites exactly one field so that
edit is a build input instead of a source change, and fails loudly if the name is ambiguous
or absent: a silently unapplied knob would make the bisection meaningless.

Usage: profile-knob.py <panvk_kbase_profile.h> name=value
"""
import re
import sys


def apply_knob(text, knob):
    name, sep, value = knob.partition('=')
    if not sep or not name or not value:
        raise ValueError('expected name=value, got: %s' % knob)
    # Only the initializer uses a leading dot; the later `p.field = ...` overrides do not,
    # and must not be touched.
    pattern = re.compile(r'^([ \t]*\.' + re.escape(name) + r'[ \t]*=[ \t]*)([^,\n]+)(,)$', re.M)
    hits = pattern.findall(text)
    if len(hits) != 1:
        raise ValueError('.%s matches %d initializer fields, want exactly 1' % (name, len(hits)))
    return pattern.sub(lambda m: m.group(1) + value + m.group(3), text, count=1), hits[0][1].strip()


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    path, knob = argv[1], argv[2]
    try:
        with open(path) as handle:
            text = handle.read()
        updated, previous = apply_knob(text, knob)
    except ValueError as error:
        print('PANVK_PROFILE_KNOB: %s' % error, file=sys.stderr)
        return 1
    with open(path, 'w') as handle:
        handle.write(updated)
    print('knob: .%s = %s (was %s)' % (knob.partition('=')[0], knob.partition('=')[2], previous))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
