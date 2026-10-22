#!/usr/bin/env python3
"""Assemble a relocatable PRIVATE preview directory/archive. Does not install/register.
Uses an explicit allowlist; never copies build evidence, profiles, logs or credentials.
External redistribution remains blocked pending complete dependency/license audit.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import zipfile

REPO = Path(__file__).resolve().parents[2]


def assemble(output: Path) -> Path:
    allowed = REPO / "build/qt-packages"
    output = output.absolute()
    if output != output.resolve() or not output.is_relative_to(allowed) or output == allowed or output.exists():
        raise ValueError("Use a NEW directory under build/qt-packages without symlink aliases")
    service = REPO / "desktop/service/build/install/service"
    frontend = REPO / "build/qt-capability/qt-capability-shell"
    if not (service / "bin/service").is_file() or not frontend.is_file():
        raise ValueError("Build service installDist and Qt shell first")
    output.mkdir(mode=0o700, parents=True)
    lib = output / "lib"
    lib.mkdir()
    (output / "bin").mkdir()
    for sub in ("bin", "lib"):
        shutil.copytree(service / sub, lib / "service" / sub)
    # No bundled JVM, Qt/KDE plugins, themes or machine-specific settings.
    shutil.copyfile(frontend, lib / "qt-capability-shell")
    (lib / "qt-capability-shell").chmod(0o755)
    monitor = frontend.with_name("abdm-activity-monitor")
    if monitor.is_file():
        shutil.copyfile(monitor, lib / monitor.name)
        (lib / monitor.name).chmod(0o755)
    shutil.copyfile(REPO / "scripts/qt-service/runtime.py", lib / "runtime.py")
    shutil.copyfile(REPO / "scripts/qt-capability/run.py", lib / "ui_environment.py")
    launcher = output / "bin/abdm-qt"
    launcher.write_text('#!/bin/sh\nset -eu\nbase=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)\nexec python3 "$base/lib/runtime.py" "$@"\n')
    launcher.chmod(0o755)
    shutil.copyfile(REPO / "LICENSE", output / "LICENSE")
    notices = output / "notices"
    notices.mkdir()
    dependencies = []
    for jar in sorted((lib / "service/lib").glob("*.jar")):
        entries = []
        with zipfile.ZipFile(jar) as archive:
            for index, name in enumerate(archive.namelist()):
                lower = name.lower()
                if lower.startswith("meta-inf/") and any(word in lower for word in ("license", "notice", "copying")) and not name.endswith("/"):
                    info = archive.getinfo(name)
                    if info.file_size > 2 * 1024 * 1024:
                        raise ValueError("Oversized JAR notice")
                    filename = f"{jar.stem}-{index}-{Path(name).name}"
                    (notices / filename).write_bytes(archive.read(name))
                    entries.append(filename)
        dependencies.append({"jar": jar.name, "sha256": hashlib.sha256(jar.read_bytes()).hexdigest(), "embeddedNotices": entries})
    (output / "dependencies.json").write_text(json.dumps(dependencies, indent=2) + "\n")
    (output / "README.txt").write_text('''AB Download Manager Qt migration — PRIVATE PORTABLE PREVIEW
NOT a release, production-profile replacement or redistribution-approved package.
Requires Linux, Java 25, Python 3.10+, Qt 6.7+ Core/Gui/Quick/QuickControls2/Widgets/
Network/DBus, Qt Wayland plugin and KDE desktop QML style. Optional KF6 blur bridge
requires the linked system KF6WindowSystem library when present in this build.
No JVM/Qt/KDE/theme/plugin is bundled. See system-dependencies.txt.

This artifact performs no installation, registrations, desktop changes or updates.
Do not point it at an existing/installed profile. Full manual gate remains OPEN.
After obtaining approval for the chosen locations, initialize only a NEW profile:
  bin/abdm-qt init --profile /absolute/NEW-private-profile --destination /absolute/existing-download-folder
  bin/abdm-qt launch --runtime-policy /absolute/NEW-private-profile/qt-runtime-policy.json
Closing Qt intentionally keeps service/transfers running. Reopen with launch.
  bin/abdm-qt status --runtime-policy /absolute/NEW-private-profile/qt-runtime-policy.json
  bin/abdm-qt stop --runtime-policy /absolute/NEW-private-profile/qt-runtime-policy.json
Stop authenticates and waits for writer lease release. Never remove app.lock or
force-kill a live writer to install/cut over. Crash recovery starts a new epoch,
never replays commands or resumes transfers automatically; inspect saved state.
Runtime policy supports explicit HTTP(S), destination roots and direct/HTTP/SOCKS
proxy, with optional exact canonical hostProxies (no wildcards). A redirect crossing
proxy routes rejects rather than silently using the wrong route or direct fallback.
No system/PAC proxy, authenticated SOCKS, HLS or power actions.
Pending browser/clipboard reviews persist privately; acceptance is reserved on disk
before Add. Unknown/partial outcomes remain non-replayable review records. A discard
removes only that review record, not any downloads. Journal .tmp/corruption requires
explicit inspection. File forcing is not a directory/power-loss durability proof.
Separate bounded history summary/delta API exists, but the Qt full-snapshot frontend
is not yet a large-history paging implementation. No 50k engine acceptance claim.
Explicit clipboard import (never passive monitoring), frontend system tray, and
freedesktop completion/error notifications are supported. Background start honors
appSettings.json autoStartOnBoot; no desktop files are edited. Activity-dependent
limiting uses the optional read-only Wayland ext-idle-notify helper (30s threshold).
No activity is guessed before an idle/resume cycle. Notification clicks pass a fresh
xdg-activation token; tokenless focus requests remain subject to compositor policy.
Browser extension: original HTTP API (apiPort, default
15151) and `bin/abdm-qt native-host --runtime-policy ...` as the native-messaging host.
Passwords/headers use original plaintext storage; keep profile/policy/logs private.
This is application isolation, not an OS sandbox against hostile same-UID changes.

LICENSE checkpoint: Apache-2.0 repository LICENSE and extracted embedded JAR
notices are included. dependencies.json lists exact JAR hashes and notices;
missing notices are NOT an assertion of no obligations. System Qt/KDE licensing,
all transitive notices/source/replacement requirements and release source bundle
still need audit before external distribution. Do not redistribute this preview.
''')
    linked = subprocess.run(["ldd", str(frontend)], capture_output=True, text=True, check=True).stdout
    if monitor.is_file():
        linked += "\n# Read-only activity helper\n" + subprocess.run(["ldd", str(monitor)], capture_output=True, text=True, check=True).stdout
        # Generated protocol bindings carry the upstream MIT permission notice.
        shutil.copyfile(REPO / "desktop/qt-capability/ACTIVITY-PROTOCOL-NOTICE.txt", notices / "ext-idle-notify.txt")
    (output / "system-dependencies.txt").write_text(linked)
    manifest = {}
    for path in sorted(output.rglob("*")):
        if path.is_symlink():
            raise ValueError("Unexpected packaged symlink")
        if path.is_file():
            manifest[str(path.relative_to(output))] = hashlib.sha256(path.read_bytes()).hexdigest()
    (output / "manifest.json").write_text(json.dumps({"format": 1, "releaseApproved": False, "files": manifest}, indent=2) + "\n")
    archive_path = output.with_suffix(".tar.gz")
    if archive_path.exists():
        raise ValueError("Archive already exists")
    with tarfile.open(archive_path, "w:gz") as archive:
        archive.add(output, arcname=output.name)
    archive_path.chmod(0o600)
    return archive_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    print(assemble(parser.parse_args().output))


if __name__ == "__main__":
    main()
