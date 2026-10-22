import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "panvk"
KNOB = TOOLS / "profile-knob.py"

# The shape 0005 creates: an initializer with leading-dot fields, then overrides that assign
# through the struct pointer. Only the initializer may be rewritten.
PROFILE_HEADER = """static inline struct panvk_kbase_profile
panvk_kbase_profile_for(const struct pan_kmod_dev_props *props)
{
   const unsigned arch = pan_arch(props->gpu_id);
   struct panvk_kbase_profile p = {
      .bc_mode = PANVK_BC_MODE_FRAGMENT_SHADOW,
      .bc_wide = false,
      .geometry_shaders = true,
      .no_crc = false,
   };

   if (arch < 10)
      p.bc_mode = PANVK_BC_MODE_COMPUTE;
   if (arch < 9)
      p.geometry_shaders = false;

   if (panvk_kbase_has_native_bc(props))
      p.bc_mode = PANVK_BC_MODE_NATIVE;

   return p;
}
"""


def run_knob(text, knob):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "panvk_kbase_profile.h"
        path.write_text(text)
        result = subprocess.run([sys.executable, str(KNOB), str(path), knob],
                                capture_output=True, text=True)
        return result, path.read_text()


class ProfileKnobTest(unittest.TestCase):
    def test_rewrites_the_initializer_and_leaves_the_overrides_alone(self):
        result, updated = run_knob(PROFILE_HEADER, "bc_mode=PANVK_BC_MODE_COMPUTE")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn(".bc_mode = PANVK_BC_MODE_COMPUTE,\n", updated)
        # The two pointer overrides are what makes arch < 10 and the native-BC probe work;
        # a naive global replace would have hit all three occurrences.
        self.assertEqual(2, updated.count("p.bc_mode = "))
        self.assertIn("p.bc_mode = PANVK_BC_MODE_NATIVE;", updated)

    def test_rewrites_a_boolean_field(self):
        result, updated = run_knob(PROFILE_HEADER, "no_crc=true")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn(".no_crc = true,", updated)

    def test_reports_the_previous_value(self):
        result, _ = run_knob(PROFILE_HEADER, "bc_wide=true")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("was false", result.stdout)

    def test_refuses_an_unknown_field(self):
        result, updated = run_knob(PROFILE_HEADER, "nonsense=true")
        self.assertEqual(1, result.returncode)
        self.assertIn("matches 0 initializer fields", result.stderr)
        self.assertEqual(PROFILE_HEADER, updated)

    def test_refuses_a_knob_without_a_value(self):
        result, updated = run_knob(PROFILE_HEADER, "no_crc")
        self.assertEqual(1, result.returncode)
        self.assertIn("expected name=value", result.stderr)
        self.assertEqual(PROFILE_HEADER, updated)

    def test_requires_exactly_one_match(self):
        doubled = PROFILE_HEADER.replace(
            "      .no_crc = false,\n",
            "      .no_crc = false,\n      .no_crc = false,\n",
        )
        result, updated = run_knob(doubled, "no_crc=true")
        self.assertEqual(1, result.returncode)
        self.assertIn("matches 2 initializer fields", result.stderr)
        self.assertEqual(doubled, updated)


if __name__ == "__main__":
    unittest.main()
