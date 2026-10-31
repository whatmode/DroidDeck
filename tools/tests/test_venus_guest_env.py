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
    "DROIDDECK_VN_POOL_DEFER",
    "DROIDDECK_VN_BC_NATIVE",
    "DROIDDECK_VN_IMAGE_NATIVE",
    "DROIDDECK_VN_RING_KB",
    "DROIDDECK_VN_PIPELINE_CACHE",
    "DROIDDECK_VN_PIPELINE_CACHE_SAVE_S",
    "DROIDDECK_VN_PRECOMPILE",
    "DROIDDECK_VN_BC_POOL_MB",
    "DROIDDECK_VN_STATS",
    "DROIDDECK_VN_STATS_MS",
    "DROIDDECK_VN_DXVK_TILER",
    "DROIDDECK_VN_PASS_STATS_MS",
    "DROIDDECK_VN_SPARE_MB",
    "DROIDDECK_VN_CPU_PLACE",
    "DROIDDECK_VN_PIN_THREADS",
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


class TheDriverOwnsItsKnobsTest(unittest.TestCase):
    """The driver's settings live in the driver's zip and nowhere else.

    A knob that belongs to the driver is stated in the zip's env file, so it changes by shipping a
    driver rather than by an APK build. The app must not set one at all -- a line in the APK is
    what made the old A/Bs untrustworthy. The app does apply the driver's env after its own lines,
    but before the user's droiddeck-env and the agent: those stay able to override a driver knob,
    because every on-device A/B here is an edit to the user's file and a knob nothing can override
    would leave shipping another driver as the only way to test it.
    """

    SERVICE = Path(__file__).resolve().parents[2] / (
        "app/src/main/java/com/droiddeck/launcher/session/SessionService.kt")
    MANAGER = Path(__file__).resolve().parents[2] / (
        "app/src/main/java/com/droiddeck/launcher/gpu/LinuxVulkanDriverManager.java")

    def body(self):
        source = self.SERVICE.read_text()
        return source.split("private fun addClientEnvironment(", 1)[1]

    def test_the_app_sets_no_driver_knob(self):
        # ALLOWED is the set the driver's own env file may set. The app must not name any of them:
        # a knob set here would shadow or contradict the driver's zip.
        body = self.body()
        for name in sorted(ALLOWED):
            self.assertNotIn('"%s=' % name, body,
                             "%s is the driver's to state, not the app's" % name)

    def test_the_driver_is_applied_after_the_apps_own_lines(self):
        body = self.body()
        last_app_line = body.index('guest.add("BANNER_AUDIO_DIRECT_DECAY=0")')
        driver = body.index("applyDriverEnvironment(")
        self.assertLess(last_app_line, driver, "an app line would override the driver")

    def test_the_user_and_the_agent_still_override_the_driver(self):
        # This is the escape hatch every device A/B uses: DROIDDECK_VN_SYNC_PRESENT=0 in the user's
        # droiddeck-env must beat the driver's line. Applied last, the driver would make that
        # impossible and a knob could only be changed by shipping another driver.
        body = self.body()
        driver = body.index("applyDriverEnvironment(")
        user = body.index("extraEnv().forEach")
        agent = body.index("AgentEnv.beginSession(this)")
        self.assertLess(driver, user, "the driver would override the user's droiddeck-env")
        self.assertLess(driver, agent, "the driver would override the agent")

    def test_the_gl_part_is_read_after_the_root_file(self):
        # The driver has two env files and both must be read in one pass, root first then gl/, so a
        # line in gl/env wins over the same line in the root file and a += line in gl/env appends
        # to what the root file produced.
        manager = self.MANAGER.read_text()
        apply = manager.split("public List<String> applyDriverEnvironment(", 1)[1].split("\n    }", 1)[0]
        self.assertLess(apply.index("driverEnvironment("), apply.index("glEnvironment("),
                        "gl/env would lose to the root env file")
        self.assertIn("guest.addAll(", apply)


if __name__ == "__main__":
    unittest.main()
