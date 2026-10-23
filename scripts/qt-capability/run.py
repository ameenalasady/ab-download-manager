#!/usr/bin/env python3
"""Safe launcher for the isolated Qt6 Wayland capability shell.

This launcher has no engine or production profile argument and no X11 fallback.
The downloads view may explicitly consume an isolated service descriptor; the
launcher does not start that service or open its databases. It creates a fresh
private HOME/XDG tree under the ignored
build/qt-capability/run directory, links only the existing Wayland socket into
a private XDG_RUNTIME_DIR, and preserves read-only data/config/font lookup.
"""

from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import re
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BINARY = REPO_ROOT / "build" / "qt-capability" / "qt-capability-shell"
RUN_ROOT = REPO_ROOT / "build" / "qt-capability" / "run"
BACKDROP_PATTERNS = ("stripes", "black", "white", "magenta")


def fail(message: str, exit_code: int = 2) -> int:
    print(f"qt-capability-launcher: {message}", file=sys.stderr)
    return exit_code


def wayland_environment() -> tuple[dict[str, str], Path, str] | int:
    env = os.environ.copy()

    configured_platform = env.get("QT_QPA_PLATFORM", "")
    if configured_platform and configured_platform != "wayland":
        return fail(
            f"refusing QT_QPA_PLATFORM={configured_platform!r}; only native wayland is supported"
        )

    wayland_display = env.get("WAYLAND_DISPLAY", "").strip()
    if not wayland_display:
        return fail("WAYLAND_DISPLAY is required; no X11 or offscreen fallback is allowed")
    if "/" in wayland_display or wayland_display in (".", ".."):
        return fail(f"invalid WAYLAND_DISPLAY={wayland_display!r}")

    runtime_override = env.get("XDG_RUNTIME_DIR", "").strip()
    if not runtime_override:
        return fail("XDG_RUNTIME_DIR is required to locate the Wayland socket")

    source_runtime = Path(runtime_override).expanduser().resolve()
    source_socket = source_runtime / wayland_display
    try:
        socket_stat = source_socket.stat()
    except OSError as error:
        return fail(f"Wayland socket is not accessible: {source_socket}: {error}")
    if not stat.S_ISSOCK(socket_stat.st_mode):
        return fail(f"Wayland socket is not a socket: {source_socket}")
    if socket_stat.st_uid != os.getuid():
        return fail(f"Wayland socket is not owned by the current user: {source_socket}")

    return env, source_socket, wayland_display


def prepare_child_environment(
    base_env: dict[str, str], source_socket: Path, wayland_display: str, temp_root: Path
) -> dict[str, str]:
    child_env = base_env.copy()

    # Never let an inherited X11 connection become a fallback.
    child_env.pop("DISPLAY", None)
    child_env["QT_QPA_PLATFORM"] = "wayland"
    # Keep probe receipts on stderr rather than losing qInfo to the journal.
    child_env["QT_FORCE_STDERR_LOGGING"] = "1"
    child_env["QT_LOGGING_RULES"] = child_env.get("QT_LOGGING_RULES", "") + "\ndefault.info=true"

    child_env["HOME"] = str(temp_root / "home")
    child_env["XDG_CONFIG_HOME"] = str(temp_root / "config")
    child_env["XDG_CACHE_HOME"] = str(temp_root / "cache")
    child_env["XDG_DATA_HOME"] = str(temp_root / "data")
    child_env["XDG_STATE_HOME"] = str(temp_root / "state")
    child_env["TMPDIR"] = str(temp_root / "tmp")
    child_env["XDG_RUNTIME_DIR"] = str(temp_root / "runtime")
    child_env["WAYLAND_DISPLAY"] = wayland_display

    # Read-only lookup paths stay intact so system styles, icons, and fonts
    # remain discoverable even though writable state is private.
    if not child_env.get("XDG_DATA_DIRS"):
        child_env["XDG_DATA_DIRS"] = "/usr/local/share:/usr/share"
    if not child_env.get("XDG_CONFIG_DIRS"):
        child_env["XDG_CONFIG_DIRS"] = "/etc/xdg"

    (temp_root / "runtime" / wayland_display).symlink_to(source_socket)
    return child_env


def snapshot_desktop_style(base_env: dict[str, str], temp_root: Path) -> dict[str, str]:
    """Copy an explicit style-only allowlist; never link writable config to real HOME."""
    source = Path(base_env.get("XDG_CONFIG_HOME") or Path(base_env["HOME"]) / ".config")
    inputs = [Path("kdeglobals"), Path("Kvantum/kvantum.kvconfig")]
    selection = source / inputs[1]
    if selection.is_file():
        config = configparser.ConfigParser(interpolation=None)
        config.read(selection)
        theme = config.get("General", "theme", fallback="")
        if theme and not re.fullmatch(r"[A-Za-z0-9_.-]+", theme):
            raise ValueError("Unsafe Kvantum theme name in style snapshot")
        if theme:
            # A theme consists of these two data files. Do not recursively copy config.
            inputs += [Path("Kvantum") / theme / (theme + suffix)
                       for suffix in (".kvconfig", ".svg")]
    copied = {}
    for relative in inputs:
        path = source / relative
        if not path.is_file():
            continue
        destination = temp_root / "config" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
        copied[str(relative)] = hashlib.sha256(destination.read_bytes()).hexdigest()
    # Support plugins using HOME/.config as well as XDG_CONFIG_HOME. Both point
    # exclusively inside this newly allocated disposable tree.
    (temp_root / "home" / ".config").symlink_to(temp_root / "config", target_is_directory=True)
    return copied


def desktop_environment(
    base_env: dict[str, str], source_socket: Path, wayland_display: str, temp_root: Path
) -> dict[str, str]:
    """The reviewed presentation for real use: private writable state plus the desktop style.

    Same as --snapshot-desktop-style (kdeglobals colors/fonts/icon-theme name and the selected
    Kvantum theme are copied, never linked), and the user's data home is appended to the
    read-only lookup path so user-installed icon themes and color schemes resolve. Nothing
    here is ever written back; theme changes apply on the next launch.
    """
    child_env = prepare_child_environment(base_env, source_socket, wayland_display, temp_root)
    snapshot_desktop_style(base_env, temp_root)
    data_home = Path(base_env.get("XDG_DATA_HOME") or Path(base_env["HOME"]) / ".local" / "share")
    if data_home.is_absolute() and data_home.is_dir():
        lookup = child_env["XDG_DATA_DIRS"].split(":")
        if str(data_home) not in lookup:
            child_env["XDG_DATA_DIRS"] = ":".join([str(data_home), *lookup])
    return child_env


def make_temp_tree() -> Path:
    RUN_ROOT.mkdir(parents=True, exist_ok=True)
    temp_root = Path(tempfile.mkdtemp(prefix="qtcap-", dir=RUN_ROOT))
    os.chmod(temp_root, 0o700)
    for name in ("home", "config", "cache", "data", "state", "runtime", "tmp"):
        (temp_root / name).mkdir(mode=0o700)
    return temp_root


def relevant_environment(env: dict[str, str]) -> dict[str, str | None]:
    keys = [
        "HOME",
        "XDG_CONFIG_HOME",
        "XDG_CACHE_HOME",
        "XDG_DATA_HOME",
        "XDG_STATE_HOME",
        "XDG_RUNTIME_DIR",
        "XDG_DATA_DIRS",
        "XDG_CONFIG_DIRS",
        "XDG_CURRENT_DESKTOP",
        "XDG_SESSION_TYPE",
        "TMPDIR",
        "WAYLAND_DISPLAY",
        "DISPLAY",
        "QT_QPA_PLATFORM",
        "QT_QPA_PLATFORMTHEME",
        "QT_FORCE_STDERR_LOGGING",
        "QT_LOGGING_RULES",
        "QT_STYLE_OVERRIDE",
        "QT_PLUGIN_PATH",
        "QML2_IMPORT_PATH",
        "QML_IMPORT_PATH",
        "FONTCONFIG_FILE",
        "FONTCONFIG_PATH",
    ]
    return {key: env.get(key) for key in keys}


def stop_process(process: subprocess.Popen[bytes] | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2)


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if "--" in argv:
        separator = argv.index("--")
        launcher_args = argv[:separator]
        shell_args = argv[separator + 1 :]
    else:
        launcher_args = argv
        shell_args = []

    parser = argparse.ArgumentParser(
        description="Launch the isolated Qt6 Wayland capability shell with private writable state.",
        epilog="Arguments after -- are passed to the shell, e.g. -- --self-test --frame-out path.png",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print the isolated plan and do not launch")
    parser.add_argument("--keep-temp", action="store_true", help="Keep the private temp tree")
    parser.add_argument("--snapshot-desktop-style", action="store_true",
                        help="Copy only kdeglobals and selected Kvantum data into private state")
    parser.add_argument("--with-backdrop", action="store_true", help="Start the opaque backdrop process first")
    parser.add_argument("--backdrop-only", action="store_true", help="Run only the opaque backdrop process")
    parser.add_argument(
        "--backdrop-pattern",
        choices=BACKDROP_PATTERNS,
        default=None,
        help="Opaque backdrop pattern: stripes, black, white, or magenta",
    )
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY, help="Capability shell binary")
    args = parser.parse_args(launcher_args)

    if args.with_backdrop and args.backdrop_only:
        return fail("--with-backdrop and --backdrop-only are mutually exclusive")
    if args.backdrop_pattern is not None and not (args.with_backdrop or args.backdrop_only):
        return fail("--backdrop-pattern requires --with-backdrop or --backdrop-only")

    backdrop_pattern = args.backdrop_pattern or "stripes"

    binary = args.binary.expanduser().resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        return fail(f"executable not found: {binary}; build it under build/qt-capability first")

    environment_result = wayland_environment()
    if isinstance(environment_result, int):
        return environment_result
    base_env, source_socket, wayland_display = environment_result

    temp_root = make_temp_tree()
    try:
        child_env = prepare_child_environment(base_env, source_socket, wayland_display, temp_root)
        style_inputs = snapshot_desktop_style(base_env, temp_root) if args.snapshot_desktop_style else {}
    except (OSError, ValueError, configparser.Error) as error:
        shutil.rmtree(temp_root)
        return fail(f"cannot prepare isolated environment: {error}")

    shell_command = [str(binary), *shell_args]
    backdrop_command = [
        str(binary),
        "--backdrop",
        "--backdrop-pattern",
        backdrop_pattern,
    ]
    mode = "backdrop-only" if args.backdrop_only else "shell"

    plan = {
        "mode": mode,
        "binary": str(binary),
        "shell_command": shell_command,
        "backdrop_command": backdrop_command if (args.with_backdrop or args.backdrop_only) else None,
        "backdrop_pattern": backdrop_pattern,
        "style_inputs_sha256": style_inputs,
        "environment": relevant_environment(child_env),
        "temp_root": str(temp_root),
        "launched": not args.dry_run,
    }

    if args.dry_run:
        print(json.dumps(plan, indent=2, sort_keys=True))
        if not args.keep_temp:
            shutil.rmtree(temp_root, ignore_errors=True)
        return 0

    print(f"qt-capability-launcher: private state: {temp_root}", file=sys.stderr)
    backdrop_process: subprocess.Popen[bytes] | None = None
    child_process: subprocess.Popen[bytes] | None = None
    try:
        if args.with_backdrop:
            print("qt-capability-launcher: starting opaque backdrop", file=sys.stderr)
            backdrop_process = subprocess.Popen(backdrop_command, env=child_env)
            time.sleep(0.4)
            if backdrop_process.poll() is not None:
                return fail("opaque backdrop exited during startup", 3)

        if args.backdrop_only:
            child_process = subprocess.Popen(backdrop_command, env=child_env)
        else:
            child_process = subprocess.Popen(shell_command, env=child_env)
        return child_process.wait()
    except KeyboardInterrupt:
        return 130
    except OSError as error:
        return fail(f"failed to launch: {error}", 3)
    finally:
        stop_process(child_process)
        stop_process(backdrop_process)
        if not args.keep_temp:
            shutil.rmtree(temp_root, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
