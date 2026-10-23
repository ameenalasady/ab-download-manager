#!/usr/bin/env python3
"""Launch the real UI with a disposable profile, without installing or global input injection."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
REVIEW = ROOT / "build/ui-review"
spec = importlib.util.spec_from_file_location("native_launch", ROOT / "scripts/native-wayland/launch.py")
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--source", action="store_true", help="Prepend freshly compiled local UI classes/resources")
    parser.add_argument("--backend", choices=["wayland", "x11"], default="wayland")
    args = parser.parse_args()
    bundle = args.bundle.resolve()
    profile = args.profile.resolve()
    if not profile.is_relative_to(REVIEW.resolve()) or profile == REVIEW.resolve():
        parser.error("Profile must be inside build/ui-review")
    if (profile / ".abdm-desktop-integration.properties").exists():
        parser.error("Integrated profiles are forbidden")
    launcher.verify(bundle)
    profile = launcher.prepare_profile(profile)
    status = profile / "session-status.json"
    if status.exists():
        import json
        pid = json.loads(status.read_text()).get("pid", 0)
        process = Path(f"/proc/{pid}/cmdline")
        if process.exists() and f"-Dabdm.experimental.profileRoot={profile}".encode() in process.read_bytes().split(b"\0"):
            parser.error("This UI review profile is already running")
    for name in ("ui-ready", "ui-command", "ui-result"):
        (profile / name).unlink(missing_ok=True)
    driver = REVIEW / "driver"
    driver.mkdir(parents=True, exist_ok=True)
    subprocess.run(["/usr/lib/jvm/java-25-openjdk/bin/javac", "-cp", str(bundle / "lib/app/*"),
                    "-d", str(driver), str(ROOT / "scripts/ui-review/UiReviewDriver.java")], check=True)
    cmd, env = launcher.command(bundle, profile, args.backend, False, os.environ.copy())
    classpath_index = cmd.index("-cp") + 1
    prefixes = [driver]
    if args.source:
        prefixes += [ROOT / "desktop/app/build/classes/kotlin/main",
                     ROOT / "shared/app/build/classes/kotlin/desktop/main",
                     ROOT / "shared/resources/build/classes/kotlin/desktop/main",
                     ROOT / "shared/resources/src/commonMain/resources"]
        for path in prefixes:
            if not path.exists():
                parser.error(f"Compile the UI first; missing {path}")
    cmd[classpath_index] = os.pathsep.join(map(str, prefixes)) + os.pathsep + cmd[classpath_index]
    cmd[cmd.index("com.abdownloadmanager.desktop.experimental.NativeWaylandAppKt")] = "UiReviewDriver"
    os.chdir(profile)
    os.execvpe(cmd[0], cmd, env)


if __name__ == "__main__":
    main()
