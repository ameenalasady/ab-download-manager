from __future__ import annotations

import importlib.util
import json
import os
import re
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent
LAUNCHER_DIR = TEST_DIR.parent
REPO_ROOT = LAUNCHER_DIR.parents[1]
RUN_PY = LAUNCHER_DIR / "run.py"
TRUE_BINARY = str(Path("/bin/true").resolve())

spec = importlib.util.spec_from_file_location("qt_capability_launcher", RUN_PY)
launcher = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(launcher)


class SafeLauncherTest(unittest.TestCase):
    def setUp(self) -> None:
        self.runtime_dir = Path(tempfile.mkdtemp(prefix="qtcap-test-runtime-"))
        self.socket_path = self.runtime_dir / "wayland-1"
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(str(self.socket_path))
        self.server.listen(1)

        self.env = os.environ.copy()
        self.env.update(
            {
                "WAYLAND_DISPLAY": "wayland-1",
                "XDG_RUNTIME_DIR": str(self.runtime_dir),
                "QT_QPA_PLATFORM": "wayland",
                "XDG_DATA_DIRS": "/usr/local/share:/usr/share",
                "DISPLAY": ":99",
            }
        )

    def tearDown(self) -> None:
        self.server.close()
        shutil.rmtree(self.runtime_dir, ignore_errors=True)

    def run_launcher(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(RUN_PY), *arguments],
            cwd=REPO_ROOT,
            env=self.env,
            text=True,
            capture_output=True,
            check=False,
        )

    def run_dry(self, *arguments: str) -> tuple[subprocess.CompletedProcess[str], dict]:
        result = self.run_launcher(
            "--dry-run", "--keep-temp", "--binary", TRUE_BINARY, *arguments
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        plan = json.loads(result.stdout)
        return result, plan

    def test_dry_run_creates_private_writable_state(self) -> None:
        _, plan = self.run_dry("--", "--no-blur")

        temp_root = Path(plan["temp_root"])
        try:
            self.assertTrue(temp_root.is_dir())
            self.assertEqual(stat.S_IMODE(temp_root.stat().st_mode), 0o700)

            environment = plan["environment"]
            self.assertEqual(environment["QT_QPA_PLATFORM"], "wayland")
            self.assertIsNone(environment["DISPLAY"])
            self.assertEqual(Path(environment["HOME"]), temp_root / "home")
            self.assertEqual(Path(environment["XDG_CONFIG_HOME"]), temp_root / "config")
            self.assertEqual(Path(environment["XDG_CACHE_HOME"]), temp_root / "cache")
            self.assertEqual(Path(environment["XDG_DATA_HOME"]), temp_root / "data")
            self.assertEqual(Path(environment["XDG_STATE_HOME"]), temp_root / "state")
            self.assertEqual(Path(environment["XDG_RUNTIME_DIR"]), temp_root / "runtime")
            self.assertEqual(Path(environment["TMPDIR"]), temp_root / "tmp")
            self.assertEqual(environment["XDG_DATA_DIRS"], "/usr/local/share:/usr/share")

            socket_link = temp_root / "runtime" / "wayland-1"
            self.assertTrue(socket_link.is_symlink())
            self.assertEqual(socket_link.resolve(), self.socket_path.resolve())

            self.assertEqual(plan["shell_command"], [TRUE_BINARY, "--no-blur"])
            self.assertFalse(plan["launched"])
        finally:
            shutil.rmtree(temp_root, ignore_errors=True)

    def test_actual_launch_uses_private_state_and_cleans_up(self) -> None:
        result = self.run_launcher("--binary", TRUE_BINARY, "--keep-temp")
        self.assertEqual(result.returncode, 0, result.stderr)
        match = re.search(r"private state: (.+)", result.stderr)
        self.assertIsNotNone(match, result.stderr)
        temp_root = Path(match.group(1).strip())
        try:
            self.assertTrue(temp_root.is_dir())
            self.assertTrue((temp_root / "runtime" / "wayland-1").is_symlink())
        finally:
            shutil.rmtree(temp_root, ignore_errors=True)

    def test_with_backdrop_keeps_shell_as_primary_process(self) -> None:
        _, plan = self.run_dry("--with-backdrop")
        temp_root = Path(plan["temp_root"])
        try:
            self.assertEqual(plan["mode"], "shell")
            self.assertEqual(
                plan["backdrop_command"],
                [TRUE_BINARY, "--backdrop", "--backdrop-pattern", "stripes"],
            )
        finally:
            shutil.rmtree(temp_root, ignore_errors=True)

    def test_backdrop_only_mode(self) -> None:
        _, plan = self.run_dry("--backdrop-only")
        temp_root = Path(plan["temp_root"])
        try:
            self.assertEqual(plan["mode"], "backdrop-only")
            self.assertEqual(
                plan["backdrop_command"],
                [TRUE_BINARY, "--backdrop", "--backdrop-pattern", "stripes"],
            )
        finally:
            shutil.rmtree(temp_root, ignore_errors=True)

    def test_backdrop_pattern_is_forwarded(self) -> None:
        _, plan = self.run_dry("--backdrop-only", "--backdrop-pattern", "black")
        temp_root = Path(plan["temp_root"])
        try:
            self.assertEqual(plan["backdrop_pattern"], "black")
            self.assertEqual(
                plan["backdrop_command"],
                [TRUE_BINARY, "--backdrop", "--backdrop-pattern", "black"],
            )
        finally:
            shutil.rmtree(temp_root, ignore_errors=True)

    def test_backdrop_pattern_requires_backdrop_mode(self) -> None:
        result = self.run_launcher(
            "--dry-run", "--binary", TRUE_BINARY, "--backdrop-pattern", "black"
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("requires --with-backdrop or --backdrop-only", result.stderr)

    def test_refuses_unknown_backdrop_pattern(self) -> None:
        result = self.run_launcher(
            "--dry-run", "--binary", TRUE_BINARY, "--backdrop-pattern", "red"
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("invalid choice", result.stderr)

    def test_refuses_x11_platform_override(self) -> None:
        self.env["QT_QPA_PLATFORM"] = "x11"
        result = self.run_launcher("--dry-run", "--binary", TRUE_BINARY)
        self.assertEqual(result.returncode, 2)
        self.assertIn("only native wayland", result.stderr)

    def test_refuses_missing_wayland_display(self) -> None:
        self.env.pop("WAYLAND_DISPLAY")
        result = self.run_launcher("--dry-run", "--binary", TRUE_BINARY)
        self.assertEqual(result.returncode, 2)
        self.assertIn("WAYLAND_DISPLAY is required", result.stderr)

    def test_refuses_missing_socket(self) -> None:
        self.socket_path.unlink()
        result = self.run_launcher("--dry-run", "--binary", TRUE_BINARY)
        self.assertEqual(result.returncode, 2)
        self.assertIn("socket is not accessible", result.stderr)

    def test_refuses_parent_directory_wayland_name(self) -> None:
        self.env["WAYLAND_DISPLAY"] = "../wayland-1"
        result = self.run_launcher("--dry-run", "--binary", TRUE_BINARY)
        self.assertEqual(result.returncode, 2)
        self.assertIn("invalid WAYLAND_DISPLAY", result.stderr)

    def test_style_snapshot_is_private_and_allowlisted(self) -> None:
        source = self.runtime_dir / "original-config"
        theme = source / "Kvantum/Layan"
        theme.mkdir(parents=True)
        fixtures = {"kdeglobals": "[KDE]\nwidgetStyle=kvantum\n",
                    "Kvantum/kvantum.kvconfig": "[General]\ntheme=Layan\n",
                    "Kvantum/Layan/Layan.kvconfig": "[General]\n",
                    "Kvantum/Layan/Layan.svg": "<svg/>"}
        for name, content in fixtures.items():
            (source / name).write_text(content)
        (source / "private-app-profile.json").write_text("must not copy")
        self.env["XDG_CONFIG_HOME"] = str(source)
        _, plan = self.run_dry("--snapshot-desktop-style")
        temp_root = Path(plan["temp_root"])
        try:
            self.assertEqual(set(plan["style_inputs_sha256"]), set(fixtures))
            for name, content in fixtures.items():
                target = temp_root / "config" / name
                self.assertFalse(target.is_symlink())
                self.assertEqual(target.read_text(), content)
            self.assertFalse((temp_root / "config/private-app-profile.json").exists())
            (temp_root / "config/kdeglobals").write_text("private change")
            self.assertEqual((source / "kdeglobals").read_text(), fixtures["kdeglobals"])
            self.assertEqual((temp_root / "home/.config").resolve(), temp_root / "config")
        finally:
            shutil.rmtree(temp_root)

    def test_desktop_environment_copies_style_and_reads_user_data_only(self) -> None:
        home = self.runtime_dir / "user-home"
        (home / ".config").mkdir(parents=True)
        (home / ".local/share/icons/Example").mkdir(parents=True)
        (home / ".config/kdeglobals").write_text("[General]\nfont=Geist,10\n")
        base = dict(self.env, HOME=str(home))
        base.pop("XDG_CONFIG_HOME", None)
        base.pop("XDG_DATA_HOME", None)
        temp_root = Path(tempfile.mkdtemp(prefix="qtcap-desktop-", dir=self.runtime_dir))
        for name in ("home", "config", "cache", "data", "state", "runtime", "tmp"):
            (temp_root / name).mkdir()
        env = launcher.desktop_environment(base, self.socket_path, "wayland-1", temp_root)
        # Writable state stays private; the style is a copy; user data is lookup-only.
        for key in ("HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"):
            self.assertTrue(Path(env[key]).is_relative_to(temp_root), key)
        copied = temp_root / "config/kdeglobals"
        self.assertFalse(copied.is_symlink())
        self.assertEqual(copied.read_text(), "[General]\nfont=Geist,10\n")
        self.assertEqual(env["XDG_DATA_DIRS"].split(":"),
                         [str(home / ".local/share"), "/usr/local/share", "/usr/share"])
        self.assertNotIn("DISPLAY", env)
        self.assertEqual(env["QT_QPA_PLATFORM"], "wayland")

    def test_style_snapshot_rejects_theme_path_traversal(self) -> None:
        source = self.runtime_dir / "bad-config"
        (source / "Kvantum").mkdir(parents=True)
        (source / "Kvantum/kvantum.kvconfig").write_text("[General]\ntheme=../../private\n")
        self.env["XDG_CONFIG_HOME"] = str(source)
        result = self.run_launcher("--dry-run", "--binary", TRUE_BINARY, "--snapshot-desktop-style")
        self.assertEqual(result.returncode, 2)
        self.assertIn("Unsafe Kvantum theme", result.stderr)

    def test_stays_free_of_production_hooks(self) -> None:
        forbidden = (
            "1515" + "1",
            "NativeWayland" + "AppKt",
            "com.abdownloadmanager.desktop." + "experimental",
        )
        code_files = []
        for root in (REPO_ROOT / "desktop" / "qt-capability", LAUNCHER_DIR):
            for path in root.rglob("*"):
                if path.suffix in {".cpp", ".h", ".qml", ".py"}:
                    code_files.append(path)
        self.assertTrue(code_files)
        for path in code_files:
            contents = path.read_text(encoding="utf-8")
            for marker in forbidden:
                self.assertNotIn(marker, contents, f"{marker} found in {path}")


if __name__ == "__main__":
    unittest.main()
