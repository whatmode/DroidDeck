"""droiddeck-shader-precompile: the launched game's recorded shaders compiled before it starts."""
from pathlib import Path
import os
import runpy
import stat
import tempfile
import threading
import time
import unittest

BIN = Path(__file__).resolve().parents[1] / "linuxfs/overlay/usr/local/bin"
PRE = runpy.run_path(str(BIN / "droiddeck-shader-precompile"))
GAME_ENV = runpy.run_path(str(BIN / "droiddeck-game-env"))

REPORT = """Fossilize INFO: =================
Fossilize INFO:  Progress report:
Fossilize INFO:    Overall {done} / {total}
Fossilize INFO:    Compile graphics {done} / {total}, skipped 0, cached 0
Fossilize INFO:    Compile compute 0 / 0, skipped 0, cached 0
Fossilize INFO: ================="""


class Fixture:
    def __init__(self, tool_body):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.steam = root / "steam"
        self.shaders = self.steam / "steamapps/shadercache/1118240"
        db = self.shaders / "fozpipelinesv6/steamapprun_pipeline_cache.0"
        db.mkdir(parents=True)
        (db / "steamapp_pipeline_cache.foz").write_bytes(b"recorded")
        tool = self.steam / "steamrtarm64/fossilize_replay"
        tool.parent.mkdir(parents=True)
        tool.write_text("#!/bin/sh\necho run >> %s/ran\n%s\n" % (root, tool_body))
        tool.chmod(tool.stat().st_mode | stat.S_IEXEC)
        self.launch = root / "launch"
        self.launch.mkdir()
        self.root = root
        self.env = {"HOME": str(root), "STEAM_COMPAT_CLIENT_INSTALL_PATH": str(self.steam),
                    "MESA_GLSL_CACHE_DIR": str(self.shaders), "BL_LAUNCH_DIR": str(self.launch),
                    "PATH": os.environ.get("PATH", "/usr/bin:/bin")}
        self.lines = []

    def runs(self):
        try:
            return (self.root / "ran").read_text().count("run")
        except OSError:
            return 0

    def run(self):
        return PRE["run"]("1118240", "Lake", self.env, log=self.lines.append)


def reports(*pairs):
    return "\n".join("cat <<'R'\n%s\nR" % REPORT.format(done=d, total=t) for d, t in pairs)


class ParseProgressTest(unittest.TestCase):
    def test_overall_and_kinds(self):
        state = {}
        self.assertTrue(PRE["parse_progress"]("Fossilize INFO:    Overall 812 / 1985", state))
        self.assertEqual((state["done"], state["total"]), (812, 1985))
        self.assertTrue(PRE["parse_progress"]("Fossilize INFO:    Compile graphics 800 / 1900, skipped 3, cached 7", state))
        self.assertEqual(state["graphics"], {"done": 800, "total": 1900, "skipped": 3, "cached": 7})
        self.assertFalse(PRE["parse_progress"]("Fossilize INFO: something else", state))


class RunTest(unittest.TestCase):
    def test_compiles_once_and_marks_the_databases_done(self):
        f = Fixture(reports((1, 3), (3, 3)) + "\nexit 0")
        self.assertEqual(f.run(), 0)
        self.assertEqual(f.runs(), 1)
        self.assertTrue((f.shaders / "droiddeck-venus" / PRE["DONE_NAME"]).exists())
        self.assertFalse((f.launch / "shader-precompile.json").exists())
        self.assertTrue(any("done in" in line and "graphics 3/3" in line for line in f.lines), f.lines)
        log = (f.shaders / "droiddeck-venus/precompile.log").read_text()
        self.assertNotIn("Progress report", log)
        self.assertIn('"done": 3', log)
        # same databases: no second run
        self.assertEqual(f.run(), 0)
        self.assertEqual(f.runs(), 1)
        # a new recording: compiled again
        (f.shaders / "fozpipelinesv6/steamapprun_pipeline_cache.0/steamapp_pipeline_cache.foz").write_bytes(b"recorded more")
        self.assertEqual(f.run(), 0)
        self.assertEqual(f.runs(), 2)

    def test_progress_file_while_running(self):
        f = Fixture(reports((1, 4)) + "\nsleep 1\n" + reports((4, 4)) + "\nexit 0")
        seen = []

        def watch():
            path = f.launch / "shader-precompile.json"
            for _ in range(40):
                if path.exists():
                    seen.append(path.read_text())
                time.sleep(0.05)

        t = threading.Thread(target=watch)
        t.start()
        self.assertEqual(f.run(), 0)
        t.join()
        self.assertTrue(any('"total": 4' in text and '"game": "Lake"' in text for text in seen), seen)

    def test_skip_stops_the_replay_and_leaves_it_undone(self):
        f = Fixture(reports((1, 100)) + "\nsleep 30\nexit 0")

        def press_skip():
            time.sleep(0.6)
            (f.launch / "shader-precompile.skip").write_text("")

        threading.Thread(target=press_skip).start()
        started = time.monotonic()
        self.assertEqual(f.run(), 1)
        self.assertLess(time.monotonic() - started, 10)
        self.assertFalse((f.shaders / "droiddeck-venus" / PRE["DONE_NAME"]).exists())
        self.assertFalse((f.launch / "shader-precompile.skip").exists())

    def test_failed_replay_is_not_marked_done(self):
        f = Fixture(reports((1, 2)) + "\nexit 3")
        self.assertEqual(f.run(), 1)
        self.assertFalse((f.shaders / "droiddeck-venus" / PRE["DONE_NAME"]).exists())

    def test_nothing_recorded(self):
        f = Fixture("exit 0")
        for p in (f.shaders / "fozpipelinesv6").rglob("*.foz"):
            p.unlink()
        self.assertEqual(f.run(), 1)
        self.assertEqual(f.runs(), 0)

    def test_switched_off(self):
        f = Fixture("exit 0")
        f.env["DROIDDECK_VN_PRECOMPILE"] = "0"
        self.assertEqual(f.run(), 1)
        self.assertEqual(f.runs(), 0)

    def test_child_environment(self):
        f = Fixture('echo "child=$DROIDDECK_VN_PRECOMPILE_CHILD dxvk=$DROIDDECK_VN_EMULATE_AS_DXVK dir=$MESA_SHADER_CACHE_DIR glsl=${MESA_GLSL_CACHE_DIR-unset} args=$*"\nexit 0')
        self.assertEqual(f.run(), 0)
        log = (f.shaders / "droiddeck-venus/precompile.log").read_text()
        self.assertIn("child=1 dxvk=1 dir=%s glsl=unset" % f.shaders, log)
        self.assertIn("args=--progress --num-threads", log)


class LauncherTest(unittest.TestCase):
    command = ["/proton", "waitforexitandrun", "S:\\\\steamapps\\\\common\\\\Lake\\\\Lake.exe"]

    def test_venus_bridge_compiles_and_tells_the_driver(self):
        calls = []
        env = GAME_ENV["with_precompiled_shaders"](self.command, {"BL_VENUS_SINGLE": "1"}, "1118240",
                                                   run=lambda app, game, e: calls.append((app, game)) or 0)
        self.assertEqual(calls, [("1118240", "Lake")])
        self.assertEqual(env.get("DROIDDECK_VN_PRECOMPILE_DONE"), "1")

    def test_not_done_leaves_the_driver_to_replay(self):
        env = GAME_ENV["with_precompiled_shaders"](self.command, {"BL_VENUS_SINGLE": "1"}, "1118240",
                                                   run=lambda app, game, e: 1)
        self.assertNotIn("DROIDDECK_VN_PRECOMPILE_DONE", env)

    def test_other_drivers_and_errors_do_not_block_the_launch(self):
        calls = []
        env = GAME_ENV["with_precompiled_shaders"](self.command, {}, "1118240", run=lambda *a: calls.append(a) or 0)
        self.assertEqual(calls, [])
        self.assertNotIn("DROIDDECK_VN_PRECOMPILE_DONE", env)

        def boom(*a):
            raise RuntimeError("no")

        env = GAME_ENV["with_precompiled_shaders"](self.command, {"BL_DRIVER_GL": "x"}, "1118240", run=boom)
        self.assertNotIn("DROIDDECK_VN_PRECOMPILE_DONE", env)


if __name__ == "__main__":
    unittest.main()
