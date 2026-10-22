#!/usr/bin/env python3
"""Opt-in native geometry/self-test checks on this workstation's two outputs.

No display configuration changes. Only newly spawned proof windows are moved or
resized. This tests programmatic state changes, NOT pointer drag/snap/IME input.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid

import run as launcher
from live_check import APP_ID, wait_window
from compositor_capture import info, owned, run


def check(output, name, actions):
    base, socket, display = launcher.wayland_environment()
    temp = launcher.make_temp_tree()
    tag = "geometry-" + uuid.uuid4().hex[:12]
    app_id = APP_ID + "." + tag
    process = None
    try:
        env = launcher.prepare_child_environment(base, socket, display, temp)
        launcher.snapshot_desktop_style(base, temp)
        env.update(QT_FORCE_STDERR_LOGGING="1", QT_LOGGING_RULES="*.info=true", WAYLAND_DEBUG="1")
        log_path = output / (name + ".log")
        with log_path.open("w") as log:
            process = subprocess.Popen([str(launcher.DEFAULT_BINARY), "--self-test",
                                        "--instance-tag", tag, "--self-test-delay-ms", "6000", "--frame-out",
                                        str(output / (name + ".png"))], env=env,
                                       stdout=log, stderr=subprocess.STDOUT)
            wid = wait_window(process, app_id)
            time.sleep(0.7)
            run("kdotool", "windowactivate", wid)
            time.sleep(0.2)
            for action in actions:
                owned(wid, process.pid, app_id)
                # Literal placeholders avoid arbitrary process/window discovery.
                run("kdotool", *(wid if value == "WINDOW" else value for value in action))
                if action[0] == "windowstate":
                    expected = "1" if "--add" in action else "0"
                    deadline = time.monotonic() + 2
                    while time.monotonic() < deadline:
                        states = re.findall(r"WINDOW_STATE .*maximized=(\d)", log_path.read_text())
                        window = info(wid)
                        native_max = bool(int(window.get("maximizeHorizontal", "0")) and
                                          int(window.get("maximizeVertical", "0")))
                        if states and states[-1] == expected and native_max == (expected == "1"):
                            break
                        time.sleep(0.05)
                    else:
                        raise RuntimeError("Timed out awaiting native maximize/restore acknowledgement")
                else:
                    time.sleep(0.3)
            result = process.wait(timeout=15)
        text = log_path.read_text()
        diagnostic = text.split("DIAGNOSTICS\n")[-1].split("SELFTEST")[0]
        if result or "SELFTEST RESULT PASS" not in text:
            raise RuntimeError(f"Native self-test {name} failed; inspect {log_path}")
        dpr = float(re.search(r"device_pixel_ratio: ([\d.]+)", diagnostic).group(1))
        size = re.search(r"window_logical: (\d+)x(\d+)", diagnostic).groups()
        return dict(name=name, selftest="PASS", dpr=dpr, logical_size=list(map(int,size)),
                    maximized="maximized: true" in diagnostic,
                    fractional_preferred_scales=re.findall(r"preferred_scale\((\d+)\)", text))
    finally:
        launcher.stop_process(process)
        shutil.rmtree(temp)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="store_true", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.absolute()
    output.mkdir(parents=True, mode=0o700, exist_ok=False)
    # Coordinates deliberately match the inventoried desktop; no scale setting is changed.
    cases = [
        ("resize-primary", [("windowmove", "WINDOW", "300", "100"),
                            ("windowsize", "WINDOW", "1100", "900")]),
        ("move-secondary", [("windowmove", "WINDOW", "2300", "100")]),
        ("maximize-primary", [("windowmove", "WINDOW", "300", "100"),
                              ("windowstate", "--add", "MAXIMIZED", "WINDOW")]),
        ("restore-primary", [("windowmove", "WINDOW", "300", "100"),
                             ("windowstate", "--add", "MAXIMIZED", "WINDOW"),
                             ("windowstate", "--remove", "MAXIMIZED", "WINDOW")]),
    ]
    results = []
    for name, actions in cases:
        results.append(check(output, name, actions))
        (output / "results.json").write_text(json.dumps(results,indent=2)+"\n")
    by_name = {r["name"]:r for r in results}
    assert by_name["resize-primary"]["logical_size"] == [1100,900], results
    assert by_name["resize-primary"]["dpr"] == 1.25, results
    assert by_name["move-secondary"]["dpr"] == 1.0, results
    assert by_name["maximize-primary"]["maximized"], results
    assert not by_name["restore-primary"]["maximized"], results
    print(json.dumps(results,indent=2))


if __name__ == "__main__":
    main()
