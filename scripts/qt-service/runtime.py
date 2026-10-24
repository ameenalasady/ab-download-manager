#!/usr/bin/env python3
"""Explicit service/frontend owner launcher. Never discovers or migrates a legacy profile.
Also the browser native-messaging host (stdout is protocol-only). Performs no installation,
registration, opener, tray or desktop changes itself.
"""
from __future__ import annotations
import argparse
import contextlib
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time
import uuid

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
PACKAGED = (HERE / "service/bin/service").is_file()
SERVICE = HERE / "service/bin/service" if PACKAGED else REPO / "desktop/service/build/install/service/bin/service"
FRONTEND = HERE / "qt-capability-shell" if PACKAGED else REPO / "build/qt-capability/qt-capability-shell"
UI_ENV = HERE / "ui_environment.py" if PACKAGED else REPO / "scripts/qt-capability/run.py"
ACTIVITY_MONITOR = HERE / "abdm-activity-monitor" if PACKAGED else REPO / "build/qt-capability/abdm-activity-monitor"
MAX_FRAME = 2 * 1024 * 1024
MAX_NATIVE_MESSAGE = 4 * 1024 * 1024  # Firefox limit; Chrome sends at most 64 MiB, replies 1 MiB.


def no_symlinks(path: Path) -> Path:
    if not path.is_absolute() or path != Path(os.path.normpath(path)):
        raise ValueError("Paths must be absolute and normalized")
    for entry in (path, *path.parents):
        if entry.is_symlink():
            raise ValueError("Symlinked runtime path refused")
    return path


def require_not_quarantined(path: Path) -> None:
    # Archived policies/descriptors retain absolute references to the SOURCE runtime.
    # Refuse before reading/attaching/launching, including from nested workspace paths.
    no_symlinks(path)
    if any(os.path.lexists(entry / ".qt-copy-quarantine") for entry in (path, *path.parents)):
        raise ValueError("Copied/recovery profiles are quarantined; execution is forbidden")


def private(path: Path, directory: bool = False) -> None:
    st = path.lstat()
    if st.st_uid != os.getuid() or st.st_mode & 0o077 or not (stat.S_ISDIR(st.st_mode) if directory else stat.S_ISREG(st.st_mode)):
        raise ValueError("Runtime path must be private, owned and of the expected type")


def write_private(path: Path, value: dict) -> None:
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "w") as stream:
        json.dump(value, stream, indent=2)
        stream.flush()
        os.fsync(stream.fileno())


def initialize(profile: Path, destination: Path) -> Path:
    no_symlinks(profile); no_symlinks(destination)
    require_not_quarantined(profile); require_not_quarantined(destination)
    if profile.parent == profile or not destination.is_dir() or not os.access(destination, os.W_OK):
        raise ValueError("Use an existing writable destination and a NEW profile directory")
    if destination == profile or destination in profile.parents or profile in destination.parents:
        raise ValueError("Profile and destination must be disjoint")
    # mkdir without exist_ok: NEVER retrofit a marker into existing profile state.
    profile.mkdir(mode=0o700, parents=False)
    identity = {"version": 1, "profileId": str(uuid.uuid4())}
    write_private(profile / "qt-runtime-profile.json", identity)
    policy = profile / "qt-runtime-policy.json"
    write_private(policy, {**identity, "profile": str(profile), "destination": str(destination),
                           "destinationRoots": [str(destination)], "redirects": True, "proxy": {"type": "direct"}})
    return policy


def profile_for(args) -> tuple[Path, list[str]]:
    if args.runtime_policy:
        policy = no_symlinks(args.runtime_policy)
        require_not_quarantined(policy.parent)
        private(policy)
        if policy.stat().st_size > 16384:
            raise ValueError("Runtime policy too large")
        value = json.loads(policy.read_text())
        profile = no_symlinks(Path(value["profile"]))
        require_not_quarantined(profile)
        private(profile, True)
        marker = profile / "qt-runtime-profile.json"
        private(marker)
        if marker.stat().st_size > 1024 or json.loads(marker.read_text()) != {"version": 1, "profileId": value["profileId"]}:
            raise ValueError("Profile initialization identity mismatch")
        return profile, ["--runtime-policy", str(policy)]
    profile = no_symlinks(args.isolated_profile)
    require_not_quarantined(profile)
    allowed = REPO / "build/qt-service"
    if PACKAGED or not profile.is_relative_to(allowed) or profile == allowed:
        raise ValueError("Isolated mode is source-only and restricted to build/qt-service")
    profile.mkdir(mode=0o700, parents=True, exist_ok=True)
    private(profile, True)
    return profile, ["--isolated-profile", str(profile)]


def receive(stream: socket.socket) -> dict:
    def exact(size):
        data = bytearray()
        while len(data) < size:
            chunk = stream.recv(size - len(data))
            if not chunk:
                raise ConnectionError("Service closed the connection")
            data.extend(chunk)
        return data
    size = struct.unpack(">I", exact(4))[0]
    if not 0 < size <= MAX_FRAME:
        raise ValueError("Invalid service frame")
    return json.loads(exact(size).decode("utf-8"))


def exchange(stream: socket.socket, request: dict) -> dict:
    payload = json.dumps(request, separators=(",", ":")).encode()
    if len(payload) > MAX_FRAME:
        raise ValueError("Request too large")
    stream.sendall(struct.pack(">I", len(payload)) + payload)
    return receive(stream)


@contextlib.contextmanager
def connect(profile: Path):
    require_not_quarantined(profile)
    descriptor = profile / "qt-service.json"
    private(descriptor)
    if descriptor.stat().st_size > 4096:
        raise ValueError("Invalid service descriptor")
    doc = json.loads(descriptor.read_text())
    endpoint = Path(doc["socket"])
    st = endpoint.lstat()
    private(endpoint.parent, True)
    if not stat.S_ISSOCK(st.st_mode) or st.st_uid != os.getuid() or st.st_mode & 0o077:
        raise ValueError("Invalid service endpoint")
    if doc.get("version") != 1 or len(doc.get("token", "")) != 43:
        raise ValueError("Unsupported service descriptor")
    with socket.socket(socket.AF_UNIX) as stream:
        stream.settimeout(12)
        stream.connect(str(endpoint))
        hello = exchange(stream, {"type": "hello", "version": 1, "token": doc["token"], "clientId": str(uuid.uuid4())})
        if hello.get("type") != "hello" or hello.get("epoch") != doc["epoch"] or hello.get("version") != 1:
            raise ValueError("Service negotiation failed")
        yield stream, hello


def available(profile: Path) -> bool:
    try:
        with connect(profile) as (stream, hello):
            result = exchange(stream, {"type": "snapshot"})
            return result.get("epoch") == hello["epoch"] and result.get("snapshot", {}).get("state") == "ready"
    except (OSError, ValueError, KeyError, ConnectionError):
        return False


def lease_free(profile: Path) -> bool:
    lock = profile / "app-data/config/app.lock"
    if not lock.exists():
        return True
    fd = os.open(lock, os.O_RDWR | os.O_NOFOLLOW)
    try:
        try:
            fcntl.lockf(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except BlockingIOError:
            return False
    finally:
        os.close(fd)  # Never unlink the shared lock inode.


def stop(profile: Path, timeout: float = 60) -> None:
    # Authentication is mandatory. Never kill a PID from a stale descriptor.
    with connect(profile) as (stream, hello):
        command_id = str(uuid.uuid4())
        reply = exchange(stream, {"type": "command", "epoch": hello["epoch"], "commandId": command_id, "op": "service.shutdown"})
        if reply.get("commandId") != command_id or reply.get("epoch") != hello["epoch"] or reply.get("result", {}).get("ok") is not True:
            raise ValueError("Shutdown was not acknowledged")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not (profile / "qt-service.json").exists() and lease_free(profile):
            return
        time.sleep(0.1)
    raise TimeoutError("Joined shutdown did not finish; do not force-cutover or remove the lock")


def frontend_command(policy: Path) -> list[str]:
    # How the service opens a window for a browser request when none is connected.
    return [sys.executable, str(Path(__file__).resolve()), "launch", "--runtime-policy", str(policy)]


class ServiceStartError(RuntimeError):
    """The service process exited during startup. The message is fixed text plus the log path."""


def ensure_service(profile: Path, arguments: list[str], service: Path = SERVICE, frontend: list[str] | None = None) -> bool:
    require_not_quarantined(profile)
    # Serialize cooperating cold launches separately from the authoritative JVM writer lease.
    fd = os.open(profile / "qt-launch.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "r+") as launch_lock:
        fcntl.flock(launch_lock, fcntl.LOCK_EX)
        if available(profile):
            return False  # Attached, not owned; never stop it on UI failure/close.
        if not lease_free(profile):
            raise RuntimeError("A writer holds this profile but no compatible service is ready; refusing restart")
        if not service.is_file() or not os.access(service, os.X_OK):
            raise ValueError("Service executable not found; build/package it first")
        log_fd = os.open(profile / "qt-service.log", os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        os.fchmod(log_fd, 0o600)
        with os.fdopen(log_fd, "ab") as log:
            environment = dict(os.environ)
            environment.pop("ABDM_QT_FRONTEND_LAUNCH", None)
            environment.pop("ABDM_QT_ACTIVITY_MONITOR", None)
            environment.pop("XDG_ACTIVATION_TOKEN", None)
            environment.pop("DESKTOP_STARTUP_ID", None)
            if arguments[0] == "--runtime-policy" and ACTIVITY_MONITOR.is_file() and os.access(ACTIVITY_MONITOR, os.X_OK):
                environment["ABDM_QT_ACTIVITY_MONITOR"] = json.dumps([str(ACTIVITY_MONITOR)])
            if frontend:
                environment["ABDM_QT_FRONTEND_LAUNCH"] = json.dumps(frontend)
            process = subprocess.Popen([str(service), *arguments], cwd=REPO if not PACKAGED else HERE, env=environment,
                                       stdin=subprocess.DEVNULL, stdout=log, stderr=log, start_new_session=True)
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise ServiceStartError(f"service failed to start; see {profile / 'qt-service.log'}")
            if available(profile):
                return True
            time.sleep(0.1)
        # Only the child started above is owned. TERM invokes its joined shutdown hook;
        # never SIGKILL a live writer on timeout.
        process.terminate()
        try:
            process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            raise RuntimeError("Owned startup failed to drain; writer left alive, no forced kill") from None
        raise TimeoutError("Service readiness timed out")


def launch_ui(profile: Path, frontend: Path = FRONTEND) -> int:
    require_not_quarantined(profile)
    if not frontend.is_file() or not os.access(frontend, os.X_OK):
        raise ValueError("Qt frontend not found; build/package it first")
    spec = importlib.util.spec_from_file_location("abdm_private_ui", UI_ENV)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = module.wayland_environment()
    if isinstance(result, int):
        return result
    base, source, display = result
    with tempfile.TemporaryDirectory(prefix="abdm-qt-ui-") as temporary:
        module.RUN_ROOT = Path(temporary)
        root = module.make_temp_tree()
        # Private writable state, but the desktop's colors, fonts, icons and Kvantum theme:
        # the presentation every gallery/smoke was reviewed under.
        environment = module.desktop_environment(base, source, display, root)
        # Packaged: one stable Wayland app ID, matched by the installed .desktop StartupWMClass.
        tag = "abdm" if PACKAGED else "explicit-runtime-" + uuid.uuid4().hex[:12]
        process = subprocess.Popen([str(frontend), "--view", "downloads", "--instance-tag", tag,
                                    "--service-descriptor", str(profile / "qt-service.json")], env=environment)
        try:
            return process.wait()
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=15)
    # Deliberately no service stop: closing Qt leaves transfers running.


def autostart_allowed(profile: Path) -> bool:
    """Read-only gate for the installed --background/start route. No desktop-file edits.

    Matches per-field fallback (absent/null/wrong type => original default true).
    Broken JSON or an unfinished transaction is not permission to start a writer.
    """
    settings = no_symlinks(profile / "app-data/config/appSettings.json")
    if settings.with_name(settings.name + ".tmp").exists():
        raise ValueError("Unfinished settings transaction")
    if not settings.exists():
        return True
    if not settings.is_file() or settings.stat().st_size > 1024 * 1024:
        raise ValueError("Invalid settings document")
    value = json.loads(settings.read_text())
    if not isinstance(value, dict):
        raise ValueError("Invalid settings document")
    flag = value.get("autoStartOnBoot")
    return flag if isinstance(flag, bool) else True


def activate_frontend(profile: Path) -> bool:
    """One correlated request, never replayed. KWin may refuse tokenless activation."""
    token = os.environ.get("XDG_ACTIVATION_TOKEN")
    if token is not None and (not 1 <= len(token) <= 256 or any(c.isspace() or ord(c) < 32 for c in token)):
        token = None
    with connect(profile) as (stream, hello):
        command_id = str(uuid.uuid4())
        reply = exchange(stream, {"type": "command", "epoch": hello["epoch"], "commandId": command_id,
                                  "op": "frontend.activate", "token": token})
        return (reply.get("epoch") == hello["epoch"] and reply.get("commandId") == command_id
                and reply.get("result", {}).get("ok") is True)


def read_native(stream) -> dict | None:
    header = stream.read(4)
    if not header:
        return None  # The browser closed the port.
    if len(header) < 4:
        raise EOFError("Truncated native message length")
    size = struct.unpack("=I", header)[0]
    if not 0 < size <= MAX_NATIVE_MESSAGE:
        raise EOFError("Invalid native message length")
    data = stream.read(size)
    if len(data) < size:
        raise EOFError("Truncated native message")
    return json.loads(data.decode("utf-8"))


def write_native(stream, message: dict) -> None:
    data = json.dumps(message, separators=(",", ":")).encode("utf-8")
    stream.write(struct.pack("=I", len(data)) + data)
    stream.flush()


def native_reply(message_id: str, payload, error: bool = False) -> dict:
    # Original NativeMessagingMessage: payload is itself a JSON string.
    return {"id": message_id, "content": {"action": None, "isError": error, "payload": json.dumps(payload)}}


def native_error(message_id: str, error_type: str, message: str) -> dict:
    return native_reply(message_id, {"errorType": error_type, "message": message}, error=True)


def native_add(profile: Path, arguments: list[str], frontend: list[str] | None, payload: str) -> dict:
    request = json.loads(payload)
    if isinstance(request, list):  # Legacy extension payload: a bare list of HTTP items.
        request = {"items": request}
    if not isinstance(request, dict):
        raise ValueError("Invalid add request")
    ensure_service(profile, arguments, frontend=frontend)  # Cold-starts like the original host awakens the app.
    with connect(profile) as (stream, hello):
        command_id = str(uuid.uuid4())
        reply = exchange(stream, {"type": "command", "epoch": hello["epoch"], "commandId": command_id,
                                  "op": "browser.add", "request": request})
    if reply.get("commandId") != command_id or reply.get("epoch") != hello["epoch"]:
        raise ValueError("Uncorrelated service reply")
    return reply.get("result", {})


def native_host(profile: Path, arguments: list[str], frontend: list[str] | None, stdin=None, stdout=None) -> int:
    """Original com.abdownloadmanager host protocol: ping and add. One request at a time."""
    stdin = stdin or sys.stdin.buffer
    stdout = stdout or sys.stdout.buffer
    while True:
        try:
            message = read_native(stdin)
        except (EOFError, OSError):
            return 1
        except (ValueError, UnicodeDecodeError):
            write_native(stdout, native_error("N_" + str(uuid.uuid4()), "SerializationException", "Invalid native message"))
            continue
        if message is None:
            return 0
        message_id = message.get("id") if isinstance(message, dict) else None
        content = message.get("content") if isinstance(message, dict) else None
        if not isinstance(message_id, str) or not isinstance(content, dict) or not isinstance(content.get("payload"), str):
            write_native(stdout, native_error("N_" + str(uuid.uuid4()), "SerializationException", "Invalid native message"))
            continue
        action = content.get("action")
        if action == "ping":
            write_native(stdout, native_reply(message_id, True))
        elif action == "add":
            try:
                result = native_add(profile, arguments, frontend, content["payload"])
                if result.get("ok") is True:
                    write_native(stdout, native_reply(message_id, True))
                else:
                    # Service errors are bounded and free of credentials; never echo the payload.
                    write_native(stdout, native_error(message_id, str(result.get("code", "command-rejected")),
                                                      str(result.get("error", "Download was not added"))[:240]))
            except (OSError, ValueError, KeyError, RuntimeError, TimeoutError, ConnectionError) as error:
                write_native(stdout, native_error(message_id, type(error).__name__,
                                                  "Service hand-off is unavailable or its outcome is unknown. Inspect pending reviews and downloads before an explicit retry; nothing is replayed automatically."))
        else:
            write_native(stdout, native_error(message_id, "NativeMessagingRouterNotFound",
                                              f"Handler for '{action}' not found"))


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    init = sub.add_parser("init", help="Initialize a NEW explicit-runtime profile, never import existing state")
    init.add_argument("--profile", type=Path, required=True)
    init.add_argument("--destination", type=Path, required=True)
    for action in ("launch", "start", "status", "stop", "native-host"):
        command = sub.add_parser(action)
        mode = command.add_mutually_exclusive_group(required=True)
        mode.add_argument("--runtime-policy", type=Path)
        mode.add_argument("--isolated-profile", type=Path)
        if action == "native-host":
            # Browsers append the manifest path/extension origin; the manifest already scopes callers.
            command.add_argument("browser_arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    try:
        if args.action == "init":
            print(initialize(args.profile, args.destination))
            return 0
        profile, arguments = profile_for(args)
        frontend = frontend_command(args.runtime_policy) if args.runtime_policy else None
        if args.action == "native-host":
            return native_host(profile, arguments, frontend)
        if args.action == "status":
            ready = available(profile)
            print("ready" if ready else "not-ready")
            return 0 if ready else 1
        if args.action == "stop":
            stop(profile)
            print("Stopped after joined writer drain")
            return 0
        if args.action == "start" and args.runtime_policy and not autostart_allowed(profile):
            print("Background startup disabled by autoStartOnBoot", file=sys.stderr)
            return 0
        owned = ensure_service(profile, arguments, frontend=frontend)
        print("Started service" if owned else "Attached to service", file=sys.stderr)
        if args.action == "start":
            return 0
        fd = os.open(profile / "qt-ui.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        with os.fdopen(fd, "r+") as frontend_lock:
            try:
                fcntl.flock(frontend_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                activated = activate_frontend(profile)
                print("Activation requested; compositor decides focus" if activated else "Frontend is running; activation request was not accepted", file=sys.stderr)
                return 0 if activated else 1
            return launch_ui(profile)
    except KeyboardInterrupt:
        return 130
    except ServiceStartError as error:
        print(f"abdm-qt: {error}", file=sys.stderr)
        return 2
    except (OSError, ValueError, KeyError, RuntimeError, TimeoutError) as error:
        # Do not print arbitrary JSON/URLs/secrets from malformed files or service errors.
        print(f"abdm-qt: {type(error).__name__}: operation failed; inspect private local state", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
