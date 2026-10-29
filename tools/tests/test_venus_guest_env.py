"""The venus-bridge guest driver carries its own environment.

The driver's settings used to live in the APK (SessionService) or in the user's droiddeck-env
file, so changing one meant an APK build or editing a file on the phone. build-guest.sh now puts
tools/venus/guest-env into the zip, and the app reads it when the driver is selected. These tests
hold the two ends of that: the file is a well-formed env file, and the build actually ships it.
"""
from pathlib import Path
import re
import unittest

HERE = Path(__file__).resolve().parents[1]
GUEST_ENV = HERE / "venus/guest-env"
BUILD = HERE / "venus/build-guest.sh"

# What the driver's own env file may set: the guest ICD's own knobs, the session's Mali/gamescope
# knobs, and nothing else. A typo here is silent -- the guest getenv()s a name nothing defines --
# so the set is closed rather than "anything that looks like a variable".
ALLOWED = {
    "DROIDDECK_VN_SYNC_PRESENT",
    "DROIDDECK_VN_BC_GPU",
    "DROIDDECK_VN_BC7_GPU",
    "DROIDDECK_VN_BC_GPU_MAX_MB",
    "DROIDDECK_VN_BC_THREADS",
    "DROIDDECK_VN_EMULATE",
    "DROIDDECK_VN_EMULATE_BC",
    "DROIDDECK_VN_WAIT_STACKS",
    "DROIDDECK_VN_RELAX_US",
    "DROIDDECK_KOPPER_KEEP_PRESENT_MODE",
    "DROIDDECK_KOPPER_SHARE_SCREEN",
    "DROIDDECK_KOPPER_SKIP_HIDDEN",
    "DROIDDECK_WSI_X11_CPU",
    "BL_MALI_REFRESH",
}

# The two settings the zip is expected to state, because they are the present-ordering and decode
# choices the driver was built around. Active (uncommented) on purpose: a driver that shipped with
# the ordered present only as a comment would take the fast path.
REQUIRED = {
    "DROIDDECK_VN_SYNC_PRESENT": "1",
    "DROIDDECK_VN_BC_GPU": "1",
}

NAME_VALUE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=(.*)")


def active_lines(text):
    """(name, value) for every uncommented NAME=value line."""
    out = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = NAME_VALUE.fullmatch(line)
        assert match, "not a NAME=value line: %r" % line
        out.append((match.group(1), match.group(2)))
    return out


class GuestEnvFileTest(unittest.TestCase):
    def setUp(self):
        self.text = GUEST_ENV.read_text()

    def test_exists_and_is_not_empty(self):
        self.assertTrue(GUEST_ENV.is_file())
        self.assertTrue(active_lines(self.text))

    def test_every_name_is_one_the_guest_reads(self):
        for name, _ in active_lines(self.text):
            self.assertIn(name, ALLOWED, "%s is not a knob the guest driver reads" % name)

    def test_no_duplicate_names(self):
        names = [name for name, _ in active_lines(self.text)]
        self.assertEqual(len(names), len(set(names)), "a duplicate line would silently win")

    def test_states_the_present_and_decode_choices(self):
        values = dict(active_lines(self.text))
        for name, value in REQUIRED.items():
            self.assertEqual(values.get(name), value, "%s must be %s" % (name, value))

    def test_values_are_true_false_or_a_number(self):
        # debug_get_bool_option falls back to its default for anything it does not parse, so a
        # value like "on" reads as the default rather than as true. Only booleans and numbers are
        # accepted here; a knob that needs another string would be added deliberately.
        for name, value in active_lines(self.text):
            self.assertRegex(value, r"^(0|1|[0-9]+)$", "%s=%s is not a value Mesa parses" % (name, value))


class BuildShipsTheEnvFileTest(unittest.TestCase):
    def setUp(self):
        self.script = BUILD.read_text()

    def test_the_zip_gets_the_drivers_env_file(self):
        self.assertIn('cp "$HERE/guest-env" "$D/env"', self.script)

    def test_an_empty_env_file_fails_the_build(self):
        # Otherwise a typo in the path ships a driver with no settings and no error.
        self.assertIn('[ -s "$D/env" ]', self.script)

    def test_the_gl_part_repeats_it_for_older_apks(self):
        # APKs before the root env file only read gl/env, and that file exists only when the GL
        # build succeeded. The driver's settings are appended there too, guarded on the file.
        self.assertIn('if [ -f "$D/gl/env" ]; then', self.script)
        self.assertIn('grep -vE \'^[[:space:]]*(#|$)\' "$HERE/guest-env"', self.script)


if __name__ == "__main__":
    unittest.main()
