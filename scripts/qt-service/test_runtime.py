#!/usr/bin/env python3
import contextlib
import importlib.util
import io
import json
import os
import struct
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("runtime", Path(__file__).with_name("runtime.py"))
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class RuntimeLauncherTests(unittest.TestCase):
    def test_new_profile_only_and_private_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            destination = root / "downloads"; destination.mkdir()
            profile = root / "profile"
            policy = runtime.initialize(profile, destination)
            value = json.loads(policy.read_text())
            self.assertEqual(value["destinationRoots"], [str(destination)])
            self.assertEqual(value["proxy"], {"type": "direct"})
            self.assertTrue(value["redirects"])
            self.assertEqual(profile.stat().st_mode & 0o077, 0)
            self.assertEqual(policy.stat().st_mode & 0o077, 0)
            self.assertEqual(json.loads((profile / "qt-runtime-profile.json").read_text())["profileId"], value["profileId"])
            before = policy.read_bytes()
            with self.assertRaises(FileExistsError):
                runtime.initialize(profile, destination)
            self.assertEqual(policy.read_bytes(), before)

    def test_nonempty_legacy_profile_is_never_marked(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            profile = root / "old"; profile.mkdir()
            (profile / "app-data").mkdir()
            destination = root / "downloads"; destination.mkdir()
            with self.assertRaises(FileExistsError):
                runtime.initialize(profile, destination)
            self.assertFalse((profile / "qt-runtime-profile.json").exists())

    def test_symlink_destination_and_profile_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            alias = root / "alias"; alias.symlink_to(root)
            with self.assertRaises(ValueError):
                runtime.initialize(root / "new", alias)
            with self.assertRaises(ValueError):
                runtime.initialize(alias / "new", root)
            self.assertFalse((root / "new").exists())

    def test_overlapping_paths_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(ValueError):
                runtime.initialize(root / "profile", root)
            self.assertFalse((root / "profile").exists())

    def test_lock_probe_never_unlinks_inode(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = Path(temporary)
            lock = profile / "app-data/config/app.lock"
            lock.parent.mkdir(parents=True); lock.write_bytes(b"")
            inode = lock.stat().st_ino
            self.assertTrue(runtime.lease_free(profile))
            self.assertEqual(lock.stat().st_ino, inode)

    def test_autostart_gate_is_read_only_per_field_and_fail_closed_for_documents(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            settings = root / "app-data/config/appSettings.json"
            settings.parent.mkdir(parents=True)
            self.assertTrue(runtime.autostart_allowed(root))
            for value, expected in (({"autoStartOnBoot": False}, False), ({"autoStartOnBoot": True}, True),
                                    ({"autoStartOnBoot": "false"}, True), ({"autoStartOnBoot": None}, True), ({}, True)):
                settings.write_text(json.dumps(value))
                before = settings.read_bytes()
                self.assertEqual(runtime.autostart_allowed(root), expected)
                self.assertEqual(settings.read_bytes(), before)
            for broken in ("{bad", "[]", "null"):
                settings.write_text(broken)
                with self.assertRaises(ValueError):
                    runtime.autostart_allowed(root)
                self.assertEqual(settings.read_text(), broken)
            settings.write_text("{}")
            settings.with_name(settings.name + ".tmp").write_text("unfinished")
            with self.assertRaises(ValueError):
                runtime.autostart_allowed(root)

    def test_background_start_disabled_never_starts_a_service(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            destination = root / "downloads"; destination.mkdir()
            profile = root / "profile"
            policy = runtime.initialize(profile, destination)
            settings = profile / "app-data/config/appSettings.json"
            settings.parent.mkdir(parents=True); settings.write_text('{"autoStartOnBoot":false}')
            with patch.object(runtime, "ensure_service", side_effect=AssertionError("must not start")):
                self.assertEqual(runtime.main(["start", "--runtime-policy", str(policy)]), 0)
            self.assertFalse((profile / "qt-launch.lock").exists())

    def test_archived_policy_cannot_attach_to_or_stop_its_original_source(self):
        from unittest.mock import patch
        import shutil
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            destination = root / "downloads"; destination.mkdir()
            profile = root / "source"
            policy = runtime.initialize(profile, destination)
            workspace = root / "workspace"; workspace.mkdir()
            (workspace / ".qt-copy-quarantine").write_text("quarantined")
            archived = workspace / "original"; shutil.copytree(profile, archived)
            before = {str(p): (p.read_bytes(), p.stat().st_mtime_ns) for p in root.rglob("*") if p.is_file()}
            for action in ("launch", "start", "stop", "status", "native-host"):
                with patch.object(runtime, "connect", side_effect=AssertionError("must not contact source")), \
                     patch.object(runtime, "ensure_service", side_effect=AssertionError("must not start source")):
                    self.assertEqual(runtime.main([action, "--runtime-policy", str(archived / policy.name)]), 2)
            after = {str(p): (p.read_bytes(), p.stat().st_mtime_ns) for p in root.rglob("*") if p.is_file()}
            self.assertEqual(before, after)

    def test_quarantined_runtime_and_nested_initialization_are_rejected_without_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            destination = root / "downloads"; destination.mkdir()
            profile = root / "source"
            policy = runtime.initialize(profile, destination)
            (profile / ".qt-copy-quarantine").write_text("quarantined")
            self.assertEqual(runtime.main(["start", "--runtime-policy", str(policy)]), 2)
            self.assertFalse((profile / "qt-launch.lock").exists())
            for path in (profile, profile / "nested"):
                with self.assertRaises(ValueError):
                    runtime.ensure_service(path, ["--isolated-profile", str(path)])
                with self.assertRaises(ValueError):
                    with runtime.connect(path):
                        self.fail("must not connect")
                with self.assertRaises(ValueError):
                    runtime.launch_ui(path)
            with self.assertRaises(ValueError):
                runtime.initialize(profile / "nested", destination)
            with self.assertRaises(ValueError):
                runtime.initialize(root / "new", profile)
            self.assertFalse((profile / "nested").exists())
            self.assertFalse((root / "new").exists())

    def test_quarantine_marker_is_not_ignored_when_it_is_a_dangling_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / ".qt-copy-quarantine").symlink_to(root / "missing")
            with self.assertRaises(ValueError):
                runtime.require_not_quarantined(root / "nested")

    def test_private_file_creation_refuses_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "target"; target.write_text("untouched")
            alias = root / "alias"; alias.symlink_to(target)
            with self.assertRaises(OSError):
                runtime.write_private(alias, {"secret": "not-written"})
            self.assertEqual(target.read_text(), "untouched")


def frames(*messages) -> io.BytesIO:
    data = b""
    for message in messages:
        raw = message if isinstance(message, bytes) else json.dumps(message).encode()
        data += struct.pack("=I", len(raw)) + raw
    return io.BytesIO(data)


def replies(stream: io.BytesIO) -> list[dict]:
    stream.seek(0)
    result = []
    while header := stream.read(4):
        result.append(json.loads(stream.read(struct.unpack("=I", header)[0])))
    return result


def request(message_id, action, payload) -> dict:
    return {"id": message_id, "content": {"action": action, "isError": False, "payload": json.dumps(payload)}}


class NativeHostTests(unittest.TestCase):
    """The original extension's com.abdownloadmanager stdio protocol."""

    def run_host(self, stdin, **patches):
        saved = {name: getattr(runtime, name) for name in patches}
        try:
            for name, value in patches.items():
                setattr(runtime, name, value)
            stdout = io.BytesIO()
            code = runtime.native_host(Path("/nonexistent-profile"), ["--runtime-policy", "/p"], ["/usr/bin/python3", "x"], stdin, stdout)
            return code, replies(stdout)
        finally:
            for name, value in saved.items():
                setattr(runtime, name, value)

    def test_ping_unknown_and_invalid_messages_keep_the_port_open(self):
        def no_service(*_args, **_kwargs):
            raise AssertionError("ping must not start or contact the service")
        code, out = self.run_host(frames(request("B_1", "ping", None), b"not json", {"id": 5},
                                         request("B_2", "remove", {}), request("B_3", "ping", None)), ensure_service=no_service)
        self.assertEqual(code, 0)
        self.assertEqual(len(out), 5)
        self.assertEqual(out[0], {"id": "B_1", "content": {"action": None, "isError": False, "payload": "true"}})
        self.assertTrue(out[1]["content"]["isError"] and out[1]["id"].startswith("N_"))
        self.assertTrue(out[2]["content"]["isError"])
        self.assertEqual(out[3]["id"], "B_2")
        self.assertIn("not found", json.loads(out[3]["content"]["payload"])["message"])
        self.assertEqual(out[4]["id"], "B_3")

    def test_add_is_forwarded_once_after_cold_start_with_frontend_command(self):
        calls, sent = [], []

        @contextlib.contextmanager
        def connect(_profile):
            yield "stream", {"epoch": "e1"}

        def exchange(_stream, value):
            sent.append(value)
            return {"type": "reply", "epoch": "e1", "commandId": value["commandId"], "result": {"ok": True, "requestId": "r"}}

        payload = {"items": [{"type": "http", "link": "https://example.invalid/a.zip", "headers": {"Cookie": "c=1"}}],
                   "options": {"silentAdd": False, "silentStart": False}}
        legacy = [{"link": "https://example.invalid/b.zip"}]
        code, out = self.run_host(frames(request("B_1", "add", payload), request("B_2", "add", legacy)),
                                  ensure_service=lambda *a, **k: calls.append(k["frontend"]), connect=connect, exchange=exchange)
        self.assertEqual(code, 0)
        self.assertEqual([o["content"]["payload"] for o in out], ["true", "true"])
        self.assertEqual(calls, [["/usr/bin/python3", "x"]] * 2)
        self.assertEqual([s["op"] for s in sent], ["browser.add", "browser.add"])
        self.assertEqual(sent[0]["request"], payload)
        self.assertEqual(sent[1]["request"], {"items": legacy})
        self.assertNotEqual(sent[0]["commandId"], sent[1]["commandId"])

    def test_rejections_and_unavailable_service_never_echo_the_request(self):
        @contextlib.contextmanager
        def connect(_profile):
            yield "stream", {"epoch": "e1"}

        def exchange(_stream, value):
            return {"epoch": "e1", "commandId": value["commandId"], "result": {"ok": False, "code": "command-rejected", "error": "Unsupported download type"}}

        def unavailable(*_a, **_k):
            raise RuntimeError("A writer holds this profile")
        secret = {"items": [{"type": "hls", "link": "https://example.invalid/v.m3u8", "headers": {"Cookie": "secret-cookie"}}]}
        _, rejected = self.run_host(frames(request("B_1", "add", secret)), ensure_service=lambda *a, **k: None, connect=connect, exchange=exchange)
        _, offline = self.run_host(frames(request("B_2", "add", secret)), ensure_service=unavailable)
        for out in (rejected, offline):
            self.assertTrue(out[0]["content"]["isError"])
            self.assertNotIn("secret-cookie", json.dumps(out))
        self.assertEqual(json.loads(rejected[0]["content"]["payload"])["errorType"], "command-rejected")
        self.assertIn("unknown", json.loads(offline[0]["content"]["payload"])["message"])
        self.assertNotIn("nothing was added", json.loads(offline[0]["content"]["payload"])["message"])

    def test_lost_add_reply_is_unknown_and_never_replayed(self):
        sent = []
        @contextlib.contextmanager
        def connect(_profile):
            yield "stream", {"epoch": "e1"}
        def exchange(_stream, value):
            sent.append(value)
            raise ConnectionError("lost reply after possible Add with secret-cookie")
        payload = {"items": [{"link": "https://example.invalid/a", "headers": {"Cookie": "secret-cookie"}}]}
        code, out = self.run_host(frames(request("B_lost", "add", payload)), ensure_service=lambda *a, **k: None,
                                  connect=connect, exchange=exchange)
        self.assertEqual(code, 0)
        self.assertEqual(len(sent), 1)
        self.assertTrue(out[0]["content"]["isError"])
        message = json.loads(out[0]["content"]["payload"])["message"]
        self.assertIn("unknown", message)
        self.assertNotIn("nothing was added", message)
        self.assertNotIn("secret-cookie", json.dumps(out))

    def test_truncated_or_oversized_frames_end_the_session(self):
        for data in (b"\x05\x00", struct.pack("=I", 10) + b"{}", struct.pack("=I", runtime.MAX_NATIVE_MESSAGE + 1)):
            code, out = self.run_host(io.BytesIO(data))
            self.assertEqual((code, out), (1, []))

    def test_launch_ui_uses_the_reviewed_desktop_style_environment(self):
        # Regression: the installed window lost kdeglobals (colors, fonts, icons) because launch_ui
        # built a bare private environment instead of the one every review ran under.
        with tempfile.TemporaryDirectory() as temporary:
            stub = Path(temporary) / "ui_environment.py"
            record = Path(temporary) / "called"
            stub.write_text(
                "import os\nfrom pathlib import Path\nRUN_ROOT = None\n"
                "def wayland_environment(): return dict(os.environ), Path('/s'), 'wayland-1'\n"
                "def make_temp_tree(): return RUN_ROOT\n"
                "def prepare_child_environment(*a): raise AssertionError('bare private environment')\n"
                f"def desktop_environment(base, *a): Path({str(record)!r}).write_text('desktop'); return base\n")
            saved = runtime.UI_ENV
            runtime.UI_ENV = stub
            try:
                self.assertEqual(runtime.launch_ui(Path(temporary), frontend=Path("/bin/true").resolve()), 0)
            finally:
                runtime.UI_ENV = saved
            self.assertEqual(record.read_text(), "desktop")

    def test_activation_is_correlated_and_sent_only_once(self):
        from unittest.mock import patch
        sent = []
        @contextlib.contextmanager
        def connect(_profile):
            yield "stream", {"epoch": "e1"}
        def exchange(_stream, value):
            sent.append(value)
            return {"epoch": "e1", "commandId": value["commandId"], "result": {"ok": True}}
        with patch.object(runtime, "connect", connect), patch.object(runtime, "exchange", exchange), patch.dict(os.environ, {"XDG_ACTIVATION_TOKEN": "fresh-token"}):
            self.assertTrue(runtime.activate_frontend(Path("/private/profile")))
        self.assertEqual(len(sent), 1)
        self.assertEqual((sent[0]["op"], sent[0]["token"]), ("frontend.activate", "fresh-token"))

    def test_frontend_command_relaunches_this_launcher_for_the_same_policy(self):
        command = runtime.frontend_command(Path("/private/profile/qt-runtime-policy.json"))
        self.assertEqual(command[2:], ["launch", "--runtime-policy", "/private/profile/qt-runtime-policy.json"])
        self.assertTrue(Path(command[1]).name == "runtime.py" and Path(command[0]).is_absolute())


if __name__ == "__main__":
    unittest.main()
