#!/usr/bin/env python3
"""Opt-in live proof. Only owned synthetic windows are moved; no KWin settings change.

Requires the existing build, KDE Wayland session, kdotool, Spectacle, Pillow and
numpy. Captures are cropped over an owned public backdrop; private full desktop
intermediates are destroyed immediately. This is not an interactive-input test.
"""
from pathlib import Path
import argparse
import colorsys
import json
import math
import re
import shutil
import subprocess
import time
import uuid

import numpy as np
from PIL import Image

import run as launcher
from compositor_capture import capture, geometry, owned, run

APP_ID = "org.abdownloadmanager.QtCapability"
PADDING_TOLERANCE_RGB = 8.0
BACKDROP_STRIPES = 14  # Backdrop.qml Repeater model
SOLID_BACKDROP_RGB = {"black": (0.0, 0.0, 0.0),
                      "white": (255.0, 255.0, 255.0),
                      "magenta": (255.0, 0.0, 255.0)}


def expected_padding_rgb(pattern, meta):
    """Expected exposed-padding color from Backdrop.qml's declared pattern."""
    if pattern in SOLID_BACKDROP_RGB:
        return list(SOLID_BACKDROP_RGB[pattern])
    if pattern != "stripes":
        raise RuntimeError(f"Unknown backdrop pattern {pattern!r}")
    geometry_box, backdrop_box, pad = meta.get("geometry"), meta.get("backdrop_geometry"), meta.get("pad")
    if not geometry_box or not backdrop_box or pad is None:
        raise RuntimeError("Cannot derive expected stripe padding without capture geometry")
    scale = meta.get("pixels_per_logical", 1.0)
    # The exposed sample is the crop's top-left 4x4; stripes are vertical, so x decides.
    sample_x = geometry_box[0] - pad + 2 / scale
    index = int((sample_x - backdrop_box[0]) // (backdrop_box[2] / BACKDROP_STRIPES))
    index = min(max(index, 0), BACKDROP_STRIPES - 1)
    # Backdrop.qml: Qt.hsla(index / 14, 0.78, 0.55, 1.0).
    return [round(channel * 255)
            for channel in colorsys.hls_to_rgb(index / BACKDROP_STRIPES, 0.55, 0.78)]


def validate_exposed_padding(pattern, exposed_rgb, meta):
    """Reject a crop whose padding is not the declared owned backdrop.

    A mismatch means the frame contains foreign desktop pixels (evidence
    contamination), not a framework gate result, so fail before aggregation.
    """
    observed = [float(value) for value in exposed_rgb]
    if len(observed) != 3:
        raise RuntimeError("Evidence contamination: expected three exposed backdrop RGB channels")
    if not all(math.isfinite(value) for value in observed):
        raise RuntimeError(f"Evidence contamination: non-finite exposed backdrop padding {observed}")
    expected = expected_padding_rgb(pattern, meta)
    if any(abs(seen - want) > PADDING_TOLERANCE_RGB for seen, want in zip(observed, expected)):
        raise RuntimeError(
            f"Evidence contamination: exposed backdrop padding {observed} does not match "
            f"declared {pattern!r} backdrop (expected {expected} within {PADDING_TOLERANCE_RGB:g} RGB)")


def move_owned(wid, pid, app_id, x, y):
    """Revalidate owned PID/resourceClass before every window move."""
    owned(wid, pid, app_id)
    run("kdotool", "windowmove", wid, str(x), str(y))


def wait_window(process, app_id):
    deadline = time.monotonic() + 7
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Proof process {process.pid} exited early: {process.returncode}")
        windows = run("kdotool", "search", "--all", "--pid", str(process.pid),
                      "--class", "^" + re.escape(app_id) + "$").splitlines()
        if len(windows) == 1:
            owned(windows[0], process.pid, app_id)
            return windows[0]
        time.sleep(0.1)
    raise RuntimeError("No uniquely identified proof window appeared")


def live_opacity(wid):
    # Transient READ-ONLY KWin query, using kdotool's output channel. No rule,
    # effect, opacity, shortcut, or compositor option is changed.
    source = ('var w = workspace.windowList().find(w => w.internalId.toString() === '
              + json.dumps(wid) + '); output_result(w ? w.opacity : "missing");')
    return float(run("kdotool", "kwinscript", "--inline", source))


def region(text, name):
    match = re.search(re.escape(name) + r": ([\d.]+)x([\d.]+) @ ([\d.]+),([\d.]+)", text)
    if not match:
        raise RuntimeError(f"Missing diagnostic geometry: {name}")
    w, h, x, y = map(float, match.groups())
    return x, y, w, h


def sample_rect(image, meta, rect, inset=0):
    x, y, w, h = rect
    p, s = meta["pad"], meta["pixels_per_logical"]
    box = tuple(round(v * s) for v in
                (p+x+inset, p+y+inset, p+x+w-inset, p+y+h-inset))
    return image[box[1]:box[3], box[0]:box[2], :3].astype(float)


def run_case(output, name, pattern, shell_flags):
    base, socket, display = launcher.wayland_environment()
    tag = "render-" + uuid.uuid4().hex[:12]
    app_id = APP_ID + "." + tag
    backdrop_id = app_id + ".Backdrop"
    temp = launcher.make_temp_tree()
    processes = []
    files = []
    try:
        env = launcher.prepare_child_environment(base, socket, display, temp)
        style_inputs = launcher.snapshot_desktop_style(base, temp)
        env.update(QT_FORCE_STDERR_LOGGING="1", QT_LOGGING_RULES="*.info=true")
        for mode, flags in (("backdrop", ["--backdrop", "--backdrop-pattern", pattern,
                                        "--width", "1400", "--height", "1000"]),
                            ("shell", shell_flags + ["--frame-out", str(output / (name + "-client.png"))])):
            log = (output / (name + "-" + mode + ".log")).open("w")
            files.append(log)
            child_env = env.copy()
            if mode == "shell":
                child_env["WAYLAND_DEBUG"] = "1"
            process = subprocess.Popen([str(launcher.DEFAULT_BINARY), *flags,
                                        "--instance-tag", tag, "--quit-after-ms", "15000"], env=child_env,
                                       stdout=log, stderr=subprocess.STDOUT)
            processes.append(process)
            wid = wait_window(process, backdrop_id if mode == "backdrop" else app_id)
            if mode == "backdrop":
                backdrop_wid = wid
                move_owned(wid, process.pid, backdrop_id, 100, 50)
            else:
                shell_wid = wid
                move_owned(wid, process.pid, app_id, 300, 100)
        # KWin's initial placement/configure can arrive after window discovery.
        # Position only after both clients have mapped; capture performs bounded
        # activation and the active/geometry guards.
        time.sleep(1)
        move_owned(backdrop_wid, processes[0].pid, backdrop_id, 100, 50)
        move_owned(shell_wid, processes[1].pid, app_id, 300, 100)
        time.sleep(0.3)
        meta = capture(shell_wid, processes[1].pid, app_id,
                       backdrop_wid, processes[0].pid, output / (name + "-compositor.png"), backdrop_id)
        shell_opacity, backdrop_opacity = live_opacity(shell_wid), live_opacity(backdrop_wid)
        if shell_opacity != 1 or backdrop_opacity != 1:
            raise RuntimeError("Proof windows are not at compositor opacity 1.0")
        text = (output / (name + "-shell.log")).read_text()
        image = np.array(Image.open(output / (name + "-compositor.png")).convert("RGB"))
        material = sample_rect(image, meta, region(text, "measurement_region"), 8)
        swatch = region(text, "opaque_swatch_region")
        row = region(text, "synthetic_row_region")
        swatch_pixels = sample_rect(image, meta, (swatch[0]+10, swatch[1]+10, 8, 8))
        row_pixels = sample_rect(image, meta, (row[0]+row[2]*0.55, row[1]+12, 12, 12))
        profile = material.mean(axis=(0, 2))
        # Exposed crop padding is outside the client window and its shadow.
        exposed = image[:4, :4, :3].astype(float)
        exposed_rgb = exposed.mean(axis=(0,1)).tolist()
        # The padding must be the declared owned backdrop, not a foreign desktop.
        validate_exposed_padding(pattern, exposed_rgb, meta)
        return dict(name=name, pattern=pattern, flags=shell_flags, geometry=meta,
                    style_inputs_sha256=style_inputs, shell_opacity=shell_opacity,
                    backdrop_opacity=backdrop_opacity, material_mean_rgb=material.mean(axis=(0,1)).tolist(),
                    swatch_mean_rgb=swatch_pixels.mean(axis=(0,1)).tolist(),
                    row_mean_rgb=row_pixels.mean(axis=(0,1)).tolist(),
                    exposed_backdrop_rgb=exposed_rgb,
                    profile_range=float(np.ptp(profile)),
                    largest_step_in_row_averaged_rgb_profile=float(np.abs(np.diff(profile)).max()),
                    native_wayland='set_app_id("' + app_id + '")' in text,
                    ext_blur_request=".set_blur_region(" in text,
                    quick_desktop_style="quick_style: org.kde.desktop" in text,
                    kvantum_style="widget_style: kvantum" in text)
    finally:
        # Only terminate PIDs spawned here; never discover or quit an installed app.
        for process in reversed(processes):
            launcher.stop_process(process)
        for file in files:
            file.close()
        shutil.rmtree(temp)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="store_true", required=True,
                        help="Explicitly open and move disposable proof windows")
    parser.add_argument("--output", type=Path, required=True, help="New local evidence directory")
    args = parser.parse_args()
    output = args.output.absolute()
    output.mkdir(parents=True, mode=0o700, exist_ok=False)
    cases = []
    try:
        for name, pattern, flags in (("black", "black", ["--no-blur"]),
                                     ("white", "white", ["--no-blur"]),
                                     ("magenta", "magenta", ["--no-blur"]),
                                     ("stripes-off", "stripes", ["--no-blur"]),
                                     ("stripes-on", "stripes", []),
                                     ("opaque", "white", ["--opaque"])):
            cases.append(run_case(output, name, pattern, flags))
            (output / "cases.json").write_text(json.dumps(cases, indent=2) + "\n")
        by_name = {c["name"]: c for c in cases}
        step_ratio = (by_name["stripes-on"]["largest_step_in_row_averaged_rgb_profile"] /
                      by_name["stripes-off"]["largest_step_in_row_averaged_rgb_profile"])
        results = dict(
            native_wayland=all(c["native_wayland"] for c in cases),
            target_styles=all(c["quick_desktop_style"] and c["kvantum_style"] for c in cases),
            real_transparency=min(np.array(by_name["white"]["material_mean_rgb"]) -
                                  by_name["black"]["material_mean_rgb"]) > 10,
            opaque_foreground=max(np.max(np.abs(np.array(c["swatch_mean_rgb"]) -
                                                   by_name["black"]["swatch_mean_rgb"]))
                                  for c in cases) <= 2,
            opaque_rows=max(np.max(np.abs(np.array(c["row_mean_rgb"]) -
                                             by_name["black"]["row_mean_rgb"])) for c in cases) <= 2,
            real_blur=bool(by_name["stripes-on"]["ext_blur_request"] and step_ratio < 0.5),
            blur_step_ratio=step_ratio,
            limitation="Rendering subset only: real pointer/IME/accessibility/visual-direction gates remain open")
        # Convert numpy scalars before JSON serialization.
        results = {k: bool(v) if isinstance(v, np.bool_) else v for k,v in results.items()}
        (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(json.dumps(results, indent=2))
        return 0 if all(results[k] for k in ("native_wayland", "target_styles", "real_transparency",
                                             "opaque_foreground", "opaque_rows", "real_blur")) else 1
    except Exception as error:
        (output / "failure.txt").write_text(str(error) + "\n")
        raise


if __name__ == "__main__":
    raise SystemExit(main())
