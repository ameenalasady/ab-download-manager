"""Capture safety tests use synthetic images, never a live desktop."""
import contextlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "proof_capture", Path(__file__).resolve().parents[1] / "compositor_capture.py")
capture = importlib.util.module_from_spec(spec)
try:
    spec.loader.exec_module(capture)
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


@contextlib.contextmanager
def capture_environment(root):
    """Deterministic clock, private RAM dir, and synthetic full screenshot."""
    clock = FakeClock()

    def allocate(**kwargs):
        path = root / "private-full"
        path.mkdir(mode=0o700)
        return str(path)

    def screenshot(command, **kwargs):
        capture.Image.new("RGB", (2048, 1152), "white").save(command[-1])

    with patch.object(capture, "time", clock), \
         patch.object(capture.tempfile, "mkdtemp", side_effect=allocate), \
         patch.object(capture.subprocess, "run", side_effect=screenshot) as shot:
        yield clock, shot


def compositor(active_ids, activate_failures=()):
    """Fake run(): scripted active-window IDs; shell becomes active by default."""
    state = {"active_calls": 0, "activate_calls": 0}

    def fake(*args):
        if args[:2] == ("kdotool", "windowactivate"):
            state["activate_calls"] += 1
            if state["activate_calls"] in activate_failures:
                raise subprocess.CalledProcessError(1, args)
            return ""
        if args[:2] == ("kdotool", "getactivewindow"):
            index = state["active_calls"]
            state["active_calls"] += 1
            return active_ids[index] if index < len(active_ids) else "shell"
        if args[0] == "kscreen-doctor":
            return "Geometry: 0,0 2048x1152"
        raise AssertionError(f"unexpected command: {args}")

    return fake, state


@unittest.skipUnless(HAVE_PIL, "Pillow is needed only for optional screenshot tooling")
class CaptureSafetyTest(unittest.TestCase):
    def test_refuses_foreign_window_pid(self):
        with patch.object(capture, "info", return_value={"pid": "2", "resourceClass": "proof"}):
            with self.assertRaisesRegex(RuntimeError, "identity/PID mismatch"):
                capture.owned("window", 1, "proof")

    def test_rejects_uncovered_crop_before_screenshot(self):
        root = Path(tempfile.mkdtemp(prefix="qtcap-capture-test-"))
        try:
            fake_run, _ = compositor([])
            with capture_environment(root) as (clock, shot), \
                 patch.object(capture, "owned"), \
                 patch.object(capture, "run", side_effect=fake_run), \
                 patch.object(capture, "geometry", side_effect=[(300,100,980,840), (0,0,500,500)]):
                with self.assertRaisesRegex(RuntimeError, "backdrop must cover"):
                    capture.capture("shell",1,"proof","backdrop",2,root/"unused.png")
            shot.assert_not_called()
            self.assertFalse((root/"unused.png").exists())
            self.assertEqual(json.loads((root/"unused.png.json").read_text())["status"], "failed")
        finally:
            shutil.rmtree(root)

    def test_delayed_activation_polls_until_owned_shell_is_active(self):
        root = Path(tempfile.mkdtemp(prefix="qtcap-capture-test-"))
        try:
            fake_run, state = compositor(["other", "other", "other"])
            with capture_environment(root), \
                 patch.object(capture, "owned"), \
                 patch.object(capture, "run", side_effect=fake_run), \
                 patch.object(capture, "geometry", side_effect=[(300,100,980,840),(100,50,1400,1000),(100,50,1400,1000),(100,50,1400,1000),(300,100,980,840)]):
                meta = capture.capture("shell",1,"proof","backdrop",2,root/"crop.png")
            self.assertTrue((root/"crop.png").exists())
            self.assertEqual(meta["status"], "captured")
            self.assertEqual(state["activate_calls"], 1)
            attempt = meta["activation"]["attempts"][0]
            self.assertGreater(attempt["polls"], 4)
            self.assertGreaterEqual(attempt["settled_after_seconds"] - attempt["active_after_seconds"], 0.8)
        finally:
            shutil.rmtree(root)

    def test_stolen_focus_resets_continuous_settling_interval(self):
        clock = FakeClock()
        fake_run, _ = compositor(["shell", "shell", "other", "shell"])
        with patch.object(capture, "time", clock), \
             patch.object(capture, "owned"), \
             patch.object(capture, "run", side_effect=fake_run):
            attempts = capture.activate("shell", 1, "proof")
        self.assertGreaterEqual(clock.now, 1.1)
        self.assertGreaterEqual(attempts[-1]["active_after_seconds"], 0.3)
        self.assertGreaterEqual(attempts[-1]["settled_after_seconds"], 1.1)

    def test_sustained_focus_loss_requests_activation_again(self):
        fake_run, state = compositor(["shell"] + ["other"] * 8)
        with patch.object(capture, "time", FakeClock()), \
             patch.object(capture, "owned"), \
             patch.object(capture, "run", side_effect=fake_run):
            attempts = capture.activate("shell", 1, "proof")
        self.assertEqual(state["activate_calls"], 2)
        self.assertEqual(len(attempts), 2)
        self.assertEqual([attempt["requested"] for attempt in attempts], [True, True])

    def test_transient_settle_identity_query_error_resets_instead_of_failing(self):
        clock = FakeClock()
        fake_run, _ = compositor([])
        calls = {"count": 0}

        def flaky_owned(wid, pid, app_id):
            calls["count"] += 1
            if calls["count"] == 2:
                raise subprocess.TimeoutExpired("qdbus6", 20)
            return {"pid": str(pid), "resourceClass": app_id}

        with patch.object(capture, "time", clock), \
             patch.object(capture, "owned", side_effect=flaky_owned), \
             patch.object(capture, "run", side_effect=fake_run):
            attempts = capture.activate("shell", 1, "proof")
        self.assertEqual(len(attempts), 1)
        self.assertIn("timed out", attempts[0]["query_error"])
        # The transient query error restarts settling rather than failing the capture.
        self.assertGreaterEqual(attempts[0]["settled_after_seconds"], 0.9)

    def test_settle_identity_mismatch_is_fatal(self):
        clock = FakeClock()
        fake_run, _ = compositor([])
        calls = {"count": 0}

        def mismatched_owned(wid, pid, app_id):
            calls["count"] += 1
            if calls["count"] == 2:
                raise capture.WindowIdentityError("Window identity/PID mismatch; refusing desktop action")
            return {"pid": str(pid), "resourceClass": app_id}

        with patch.object(capture, "time", clock), \
             patch.object(capture, "owned", side_effect=mismatched_owned), \
             patch.object(capture, "run", side_effect=fake_run):
            with self.assertRaises(capture.WindowIdentityError) as raised:
                capture.activate("shell", 1, "proof")
        self.assertEqual(raised.exception.attempts[0]["attempt"], 1)

    def test_activation_retries_after_failed_request_and_stolen_focus(self):
        root = Path(tempfile.mkdtemp(prefix="qtcap-capture-test-"))
        try:
            fake_run, state = compositor(["other"] * 5, activate_failures={1})
            with capture_environment(root), \
                 patch.object(capture, "owned"), \
                 patch.object(capture, "run", side_effect=fake_run), \
                 patch.object(capture, "geometry", side_effect=[(300,100,980,840),(100,50,1400,1000),(100,50,1400,1000),(100,50,1400,1000),(300,100,980,840)]):
                meta = capture.capture("shell",1,"proof","backdrop",2,root/"crop.png")
            attempts = meta["activation"]["attempts"]
            self.assertEqual(state["activate_calls"], 2)
            self.assertEqual([a["requested"] for a in attempts], [False, True])
            self.assertIn("error", attempts[0])
            self.assertEqual(meta["status"], "captured")
        finally:
            shutil.rmtree(root)

    def test_activation_timeout_retains_no_capture_or_foreign_window_data(self):
        root = Path(tempfile.mkdtemp(prefix="qtcap-capture-test-"))
        try:
            fake_run, state = compositor(["foreign-window-id"] * 1000)
            with capture_environment(root) as (clock, shot), \
                 patch.object(capture, "owned"), \
                 patch.object(capture, "run", side_effect=fake_run):
                with self.assertRaisesRegex(RuntimeError, "not active after"):
                    capture.capture("shell",1,"proof","backdrop",2,root/"crop.png")
            shot.assert_not_called()
            self.assertFalse((root/"crop.png").exists())
            text = (root/"crop.png.json").read_text()
            meta = json.loads(text)
            self.assertEqual(meta["status"], "failed")
            self.assertFalse(meta["capture_retained"])
            self.assertTrue(meta["activation"]["attempts"])
            self.assertGreater(state["activate_calls"], 1)
            self.assertNotIn("foreign-window-id", text)
        finally:
            shutil.rmtree(root)

    def test_active_window_identity_mismatch_fails_before_screenshot(self):
        root = Path(tempfile.mkdtemp(prefix="qtcap-capture-test-"))
        try:
            fake_run, _ = compositor([])
            good_shell = {"pid": "1", "resourceClass": "proof"}
            good_backdrop = {"pid": "2", "resourceClass": "org.abdownloadmanager.QtCapability.Backdrop"}
            bad_active = {"pid": "9", "resourceClass": "other"}
            with capture_environment(root) as (clock, shot), \
                 patch.object(capture, "info", side_effect=[good_shell, good_backdrop, good_shell, bad_active]), \
                 patch.object(capture, "run", side_effect=fake_run):
                with self.assertRaisesRegex(RuntimeError, "identity/PID mismatch"):
                    capture.capture("shell",1,"proof","backdrop",2,root/"crop.png")
            shot.assert_not_called()
            self.assertFalse((root/"crop.png").exists())
            meta = json.loads((root/"crop.png.json").read_text())
            self.assertEqual(meta["status"], "failed")
            self.assertIn("identity/PID mismatch", meta["error"])
            self.assertTrue(meta["activation"]["attempts"])
        finally:
            shutil.rmtree(root)

    def test_focus_lost_during_screenshot_fails_closed(self):
        root = Path(tempfile.mkdtemp(prefix="qtcap-capture-test-"))
        try:
            fake_run, _ = compositor(["shell", "other"])
            with capture_environment(root) as (clock, shot), \
                 patch.object(capture, "activate", return_value=[{"attempt": 1}]), \
                 patch.object(capture, "owned"), \
                 patch.object(capture, "run", side_effect=fake_run), \
                 patch.object(capture, "geometry", side_effect=[(300,100,980,840),(100,50,1400,1000),(100,50,1400,1000)]):
                with self.assertRaisesRegex(RuntimeError, "not active"):
                    capture.capture("shell",1,"proof","backdrop",2,root/"crop.png")
            shot.assert_called_once()
            self.assertFalse((root/"crop.png").exists())
            self.assertFalse((root/"private-full").exists())
            meta = json.loads((root/"crop.png.json").read_text())
            self.assertEqual(meta["status"], "failed")
            self.assertFalse(meta["capture_retained"])
            self.assertTrue(meta["activation"]["attempts"])
        finally:
            shutil.rmtree(root)

    def test_backdrop_identity_loss_before_screenshot_fails_closed(self):
        root = Path(tempfile.mkdtemp(prefix="qtcap-capture-test-"))
        try:
            fake_run, _ = compositor([])
            shell_id = {"pid": "1", "resourceClass": "proof"}
            backdrop_id = {"pid": "2", "resourceClass": "org.abdownloadmanager.QtCapability.Backdrop"}
            with capture_environment(root) as (clock, shot), \
                 patch.object(capture, "activate", return_value=[{"attempt": 1}]), \
                 patch.object(capture, "owned", side_effect=[shell_id, backdrop_id,
                       capture.WindowIdentityError("Window identity/PID mismatch; refusing desktop action")]), \
                 patch.object(capture, "run", side_effect=fake_run), \
                 patch.object(capture, "geometry", side_effect=[(300,100,980,840),(100,50,1400,1000)]):
                with self.assertRaises(capture.WindowIdentityError):
                    capture.capture("shell",1,"proof","backdrop",2,root/"crop.png")
            shot.assert_not_called()
            self.assertFalse((root/"crop.png").exists())
            self.assertFalse((root/"private-full").exists())
            meta = json.loads((root/"crop.png.json").read_text())
            self.assertEqual(meta["status"], "failed")
            self.assertFalse(meta["capture_retained"])
        finally:
            shutil.rmtree(root)

    def test_backdrop_move_before_screenshot_fails_closed(self):
        root = Path(tempfile.mkdtemp(prefix="qtcap-capture-test-"))
        try:
            fake_run, _ = compositor([])
            with capture_environment(root) as (clock, shot), \
                 patch.object(capture, "activate", return_value=[{"attempt": 1}]), \
                 patch.object(capture, "owned"), \
                 patch.object(capture, "run", side_effect=fake_run), \
                 patch.object(capture, "geometry", side_effect=[(300,100,980,840),(100,50,1400,1000),(100,50,1300,1000)]):
                with self.assertRaisesRegex(RuntimeError, "Backdrop changed before capture"):
                    capture.capture("shell",1,"proof","backdrop",2,root/"crop.png")
            shot.assert_not_called()
            self.assertFalse((root/"crop.png").exists())
            self.assertFalse((root/"private-full").exists())
            meta = json.loads((root/"crop.png.json").read_text())
            self.assertEqual(meta["status"], "failed")
        finally:
            shutil.rmtree(root)

    def test_backdrop_identity_loss_after_screenshot_fails_closed(self):
        root = Path(tempfile.mkdtemp(prefix="qtcap-capture-test-"))
        try:
            fake_run, _ = compositor([])
            shell_id = {"pid": "1", "resourceClass": "proof"}
            backdrop_id = {"pid": "2", "resourceClass": "org.abdownloadmanager.QtCapability.Backdrop"}
            with capture_environment(root) as (clock, shot), \
                 patch.object(capture, "activate", return_value=[{"attempt": 1}]), \
                 patch.object(capture, "owned", side_effect=[shell_id, backdrop_id, backdrop_id, shell_id,
                       capture.WindowIdentityError("Window identity/PID mismatch; refusing desktop action")]), \
                 patch.object(capture, "run", side_effect=fake_run), \
                 patch.object(capture, "geometry", side_effect=[(300,100,980,840),(100,50,1400,1000),(100,50,1400,1000)]):
                with self.assertRaises(capture.WindowIdentityError):
                    capture.capture("shell",1,"proof","backdrop",2,root/"crop.png")
            shot.assert_called_once()
            self.assertFalse((root/"crop.png").exists())
            self.assertFalse((root/"private-full").exists())
            meta = json.loads((root/"crop.png.json").read_text())
            self.assertEqual(meta["status"], "failed")
            self.assertFalse(meta["full_capture_retained"])
        finally:
            shutil.rmtree(root)

    def test_backdrop_move_after_screenshot_fails_closed(self):
        root = Path(tempfile.mkdtemp(prefix="qtcap-capture-test-"))
        try:
            fake_run, _ = compositor([])
            with capture_environment(root) as (clock, shot), \
                 patch.object(capture, "activate", return_value=[{"attempt": 1}]), \
                 patch.object(capture, "owned"), \
                 patch.object(capture, "run", side_effect=fake_run), \
                 patch.object(capture, "geometry", side_effect=[(300,100,980,840),(100,50,1400,1000),(100,50,1400,1000),(100,50,1300,1000)]):
                with self.assertRaisesRegex(RuntimeError, "Backdrop changed during capture"):
                    capture.capture("shell",1,"proof","backdrop",2,root/"crop.png")
            shot.assert_called_once()
            self.assertFalse((root/"crop.png").exists())
            self.assertFalse((root/"private-full").exists())
            meta = json.loads((root/"crop.png.json").read_text())
            self.assertEqual(meta["status"], "failed")
        finally:
            shutil.rmtree(root)

    def test_full_capture_is_private_and_destroyed(self):
        root = Path(tempfile.mkdtemp(prefix="qtcap-capture-test-"))
        original_mkdtemp = tempfile.mkdtemp
        try:
            def allocate(**kwargs):
                self.assertEqual(kwargs["dir"], "/dev/shm")
                return original_mkdtemp(prefix=kwargs["prefix"], dir=root)

            def screenshot(command, **kwargs):
                full = Path(command[-1])
                self.assertEqual(full.parent.stat().st_mode & 0o777, 0o700)
                capture.Image.new("RGB",(3968,1152),"white").save(full)

            def query(*args):
                if args[0] == "kscreen-doctor":
                    return "Geometry: 0,0 2048x1152\nGeometry: 2048,0 1920x1080"
                return "shell"

            with patch.object(capture,"owned"), patch.object(capture,"run",side_effect=query), \
                 patch.object(capture.time,"sleep"), \
                 patch.object(capture,"geometry",side_effect=[(300,100,980,840),(100,50,1400,1000),(100,50,1400,1000),(100,50,1400,1000),(300,100,980,840)]), \
                 patch.object(capture.tempfile,"mkdtemp",side_effect=allocate), \
                 patch.object(capture.subprocess,"run",side_effect=screenshot):
                meta = capture.capture("shell",1,"proof","backdrop",2,root/"crop.png")
            self.assertFalse(meta["full_capture_retained"])
            self.assertEqual(meta["status"], "captured")
            with capture.Image.open(root/"crop.png") as image:
                self.assertEqual(image.size,(996,856))
            self.assertFalse(list(root.glob("abdm-qt-private-full-*")))
        finally:
            shutil.rmtree(root)
    def test_cleanup_failure_does_not_mask_original_error(self):
        root = Path(tempfile.mkdtemp(prefix="qtcap-capture-test-"))
        try:
            fake_run, _ = compositor(["shell", "other"])
            with capture_environment(root) as (clock, shot), \
                 patch.object(capture, "activate", return_value=[{"attempt": 1}]), \
                 patch.object(capture, "destroy_private_capture", return_value="unlink /dev/shm/x/full.png: EIO"), \
                 patch.object(capture, "owned"), \
                 patch.object(capture, "run", side_effect=fake_run), \
                 patch.object(capture, "geometry", side_effect=[(300,100,980,840),(100,50,1400,1000),(100,50,1400,1000)]):
                with self.assertRaisesRegex(RuntimeError, "not active"):
                    capture.capture("shell",1,"proof","backdrop",2,root/"crop.png")
            meta = json.loads((root/"crop.png.json").read_text())
            self.assertEqual(meta["status"], "failed")
            self.assertIn("not active", meta["error"])
            self.assertTrue(meta["full_capture_retained"])
            self.assertIn("EIO", meta["cleanup_error"])
            self.assertFalse(meta["capture_retained"])
        finally:
            shutil.rmtree(root)

    def test_cleanup_failure_blocks_captured_status(self):
        root = Path(tempfile.mkdtemp(prefix="qtcap-capture-test-"))
        try:
            fake_run, _ = compositor([])
            with capture_environment(root) as (clock, shot), \
                 patch.object(capture, "activate", return_value=[{"attempt": 1}]), \
                 patch.object(capture, "destroy_private_capture", return_value="rmdir /dev/shm/x: ENOTEMPTY"), \
                 patch.object(capture, "owned"), \
                 patch.object(capture, "run", side_effect=fake_run), \
                 patch.object(capture, "geometry", side_effect=[(300,100,980,840),(100,50,1400,1000),(100,50,1400,1000),(100,50,1400,1000),(300,100,980,840)]):
                with self.assertRaisesRegex(RuntimeError, "cleanup failed"):
                    capture.capture("shell",1,"proof","backdrop",2,root/"crop.png")
            meta = json.loads((root/"crop.png.json").read_text())
            self.assertEqual(meta["status"], "failed")
            self.assertNotEqual(meta["status"], "captured")
            self.assertTrue(meta["full_capture_retained"])
            self.assertIn("ENOTEMPTY", meta["cleanup_error"])
            # The public-backdrop crop itself was written; the full frame may remain.
            self.assertTrue(meta["capture_retained"])
            self.assertTrue((root/"crop.png").exists())
        finally:
            shutil.rmtree(root)


if __name__ == "__main__":
    unittest.main()
