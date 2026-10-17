#!/usr/bin/env python3
"""Control/screenshot only an already-running, disposable UiReviewDriver instance."""
import argparse
import re
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
REVIEW = ROOT / "build/ui-review"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--action", action="append", default=[])
    parser.add_argument("--capture", help="Window-title prefix to capture (requires KDE/Spectacle)")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    profile = args.profile.resolve()
    if not profile.is_relative_to(REVIEW.resolve()) or (profile / ".abdm-desktop-integration.properties").exists():
        parser.error("Only nonintegrated build/ui-review profiles are allowed")
    try:
        pid = int((profile / "ui-ready").read_text())
        commandline = Path(f"/proc/{pid}/cmdline").read_bytes().split(b"\0")
    except (OSError, ValueError):
        parser.error("The review app is not running or is not ready yet")
    if b"UiReviewDriver" not in commandline or f"-Dabdm.experimental.profileRoot={profile}".encode() not in commandline:
        parser.error("The recorded PID is not this profile's UI driver")
    for action in args.action:
        result = profile / "ui-result"
        result.unlink(missing_ok=True)
        temporary = profile / ".ui-command.tmp"
        temporary.write_text(action + "\n")
        temporary.replace(profile / "ui-command")
        deadline = time.monotonic() + 10
        while not result.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        response = result.read_text().strip() if result.exists() else "Timed out"
        if response != "OK " + action.strip():
            raise RuntimeError(response)
        print(response)
    if args.capture:
        if not args.output:
            parser.error("--capture requires --output")
        subprocess.run(["kdotool", "search", "--all", "--pid", str(pid), "--name", "^" + re.escape(args.capture),
                        "windowactivate"], check=True)
        time.sleep(0.3)
        subprocess.run(["spectacle", "-a", "-b", "-n", "-o", str(args.output.resolve())], check=True)


if __name__ == "__main__":
    main()
