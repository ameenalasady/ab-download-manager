"""Local compositor proof helper; captures ONLY retained crops over an owned public backdrop."""
from pathlib import Path
import json
import re
import subprocess
import tempfile
import time
from PIL import Image

ACTIVATION_TIMEOUT_SECONDS = 5.0
ACTIVATION_POLL_SECONDS = 0.1
ACTIVATION_RETRY_SECONDS = 0.5
ACTIVATION_SETTLE_SECONDS = 0.8

TRANSIENT_ERRORS = (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError)


class WindowIdentityError(RuntimeError):
    """Target window does not match the expected owned PID/resourceClass."""

    attempts = None


class ActivationError(RuntimeError):
    """Owned shell did not become active within the bounded retry window."""

    def __init__(self, message, attempts):
        super().__init__(message)
        self.attempts = attempts


def run(*args):
    return subprocess.check_output(args, text=True, timeout=20).strip()


def info(wid):
    text = run('qdbus6', 'org.kde.KWin', '/KWin', 'org.kde.KWin.getWindowInfo', wid)
    return dict(line.split(': ', 1) for line in text.splitlines() if ': ' in line)


def geometry(wid):
    d = info(wid)
    return tuple(int(d[k]) for k in ('x', 'y', 'width', 'height'))


def owned(wid, pid, app_id):
    d = info(wid)
    if int(d.get('pid', -1)) != pid or d.get('resourceClass') != app_id:
        raise WindowIdentityError('Window identity/PID mismatch; refusing desktop action')
    return d


def active_window():
    return run('kdotool', 'getactivewindow')


def activate(wid, pid, app_id, timeout=ACTIVATION_TIMEOUT_SECONDS,
             poll=ACTIVATION_POLL_SECONDS, retry=ACTIVATION_RETRY_SECONDS,
             settle=ACTIVATION_SETTLE_SECONDS):
    """Activate the owned shell with bounded polling and retries.

    Every activation request revalidates the shell PID/resourceClass first.
    A slow focus handoff or a stolen focus is retried instead of failing on one
    fixed sleep. Diagnostics contain only owned-window identity and booleans;
    no data about an unrelated active window is read or recorded.
    """
    start = time.monotonic()
    deadline = start + timeout
    attempts = []
    last_request = None
    active_since = None
    while True:
        now = time.monotonic()
        if active_since is None and (last_request is None or now - last_request >= retry):
            attempt = {'attempt': len(attempts) + 1, 'at_seconds': round(now - start, 3)}
            try:
                owned(wid, pid, app_id)
            except WindowIdentityError as error:
                attempts.append(attempt)
                error.attempts = attempts
                raise
            except TRANSIENT_ERRORS as error:
                attempt['requested'] = False
                attempt['error'] = str(error)[:200]
            else:
                try:
                    run('kdotool', 'windowactivate', wid)
                    attempt['requested'] = True
                except TRANSIENT_ERRORS as error:
                    attempt['requested'] = False
                    attempt['error'] = str(error)[:200]
            attempts.append(attempt)
            last_request = now
        try:
            active = active_window()
        except TRANSIENT_ERRORS as error:
            active = None
            attempts[-1]['query_error'] = str(error)[:200]
        attempts[-1]['polls'] = attempts[-1].get('polls', 0) + 1
        if active == wid:
            try:
                owned(active, pid, app_id)
            except TRANSIENT_ERRORS as error:
                attempts[-1]['query_error'] = str(error)[:200]
                active_since = None
            except Exception as error:
                error.attempts = attempts
                raise
            else:
                if active_since is None:
                    active_since = time.monotonic()
                # KWin's active ID can change before the raised surface is presented.
                # Require continuous verified focus through the settling interval;
                # a stolen focus resets the interval and resumes activation retries.
                if time.monotonic() - active_since >= settle:
                    attempts[-1]['active_after_seconds'] = round(active_since - start, 3)
                    attempts[-1]['settled_after_seconds'] = round(time.monotonic() - start, 3)
                    return attempts
        else:
            active_since = None
        if time.monotonic() >= deadline:
            raise ActivationError(
                f'Foreground proof window is not active after {len(attempts)} '
                f'activation attempt(s) in {timeout:.1f}s', attempts)
        time.sleep(poll)


def assert_active_owned(wid, pid, app_id):
    active = active_window()
    if active != wid:
        raise RuntimeError(f'Foreground proof window is not active (expected {wid})')
    owned(active, pid, app_id)


def write_meta(output, meta):
    Path(str(output) + '.json').write_text(json.dumps(meta, indent=2) + '\n')


def destroy_private_capture(path, directory):
    """Best-effort removal of the full frame; returns a diagnostic on failure.

    Cleanup must never replace the original capture error, so callers record the
    returned text instead of letting an OSError escape.
    """
    errors = []
    try:
        Path(path).unlink(missing_ok=True)
    except OSError as error:
        errors.append(f'unlink {path}: {error}')
    try:
        directory.rmdir()
    except OSError as error:
        errors.append(f'rmdir {directory}: {error}')
    return '; '.join(errors) or None


def capture(wid, pid, app_id, backdrop_wid, backdrop_pid, output,
            backdrop_app_id='org.abdownloadmanager.QtCapability.Backdrop'):
    activation = dict(timeout_seconds=ACTIVATION_TIMEOUT_SECONDS,
                      poll_seconds=ACTIVATION_POLL_SECONDS,
                      retry_seconds=ACTIVATION_RETRY_SECONDS,
                      settle_seconds=ACTIVATION_SETTLE_SECONDS, attempts=[])
    meta = dict(window=wid, pid=pid, app_id=app_id,
                capture='compositor workspace uniform-scale crop; public owned backdrop only',
                status='failed', capture_retained=False, full_capture_retained=False,
                activation=activation)
    try:
        owned(wid, pid, app_id)
        owned(backdrop_wid, backdrop_pid, backdrop_app_id)
        activation['attempts'] = activate(wid, pid, app_id)
        x, y, w, h = geometry(wid)
        bx, by, bw, bh = geometry(backdrop_wid)
        pad = 8
        left, top, right, bottom = x-pad, y-pad, x+w+pad, y+h+pad
        if not (bx <= left and by <= top and bx+bw >= right and by+bh >= bottom):
            raise RuntimeError(f'Owned public backdrop must cover entire retained crop: shell={(x,y,w,h)} backdrop={(bx,by,bw,bh)}')
        # --scaled requests uniform pixels/logical-coordinate across mixed-DPI screens.
        # Assert the scale from BOTH dimensions; never inherit old audit's 2x factor.
        screens = re.sub(r'\x1b\[[0-9;]*m', '', run('kscreen-doctor', '-o'))
        extents = [tuple(map(int, m)) for m in re.findall(r'Geometry:\s*(\d+),(\d+) (\d+)x(\d+)', screens)]
        if not extents:
            raise RuntimeError('Cannot establish screenshot coordinate mapping')
        full_w = max(a+c for a,b,c,d in extents)
        full_h = max(b+d for a,b,c,d in extents)
        private_dir = Path(tempfile.mkdtemp(prefix='abdm-qt-private-full-', dir='/dev/shm'))
        private_dir.chmod(0o700)
        temporary = str(private_dir / 'full.png')
        crop_written = False
        cleanup_error = None
        try:
            owned(backdrop_wid, backdrop_pid, backdrop_app_id)
            if geometry(backdrop_wid) != (bx,by,bw,bh):
                raise RuntimeError('Backdrop changed before capture')
            assert_active_owned(wid, pid, app_id)
            subprocess.run(['/usr/bin/spectacle', '-i', '-b', '-n', '-f', '--scaled', '-o', temporary],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
            assert_active_owned(wid, pid, app_id)
            owned(backdrop_wid, backdrop_pid, backdrop_app_id)
            if geometry(backdrop_wid) != (bx,by,bw,bh):
                raise RuntimeError('Backdrop changed during capture')
            if geometry(wid) != (x,y,w,h):
                raise RuntimeError('Foreground/geometry changed during capture')
            with Image.open(temporary) as image:
                sx, sy = image.width/full_w, image.height/full_h
                if abs(sx-sy) > 0.001:
                    raise RuntimeError(f'Nonuniform screenshot mapping: {sx}, {sy}')
                crop = tuple(round(v*sx) for v in (left,top,right,bottom))
                image.crop(crop).save(output)
                crop_written = True
                meta.update(window=wid, pid=pid, app_id=app_id, geometry=[x,y,w,h], pad=pad,
                            backdrop_geometry=[bx,by,bw,bh], screenshot_size=list(image.size),
                            pixels_per_logical=sx, capture='compositor workspace uniform-scale crop; public owned backdrop only',
                            full_capture_retained=False)
        finally:
            cleanup_error = destroy_private_capture(temporary, private_dir)
            meta['capture_retained'] = crop_written
            if cleanup_error is not None:
                meta['full_capture_retained'] = True
                meta['cleanup_error'] = cleanup_error
        if cleanup_error is not None:
            # Never report a successful capture while the full desktop frame may remain.
            raise RuntimeError(f'Full desktop capture cleanup failed: {cleanup_error}')
        meta['status'] = 'captured'
        meta['capture_retained'] = True
        write_meta(output, meta)
        return meta
    except Exception as error:
        attempts = getattr(error, 'attempts', None)
        if attempts is not None:
            activation['attempts'] = attempts
        meta['error'] = str(error)
        write_meta(output, meta)
        raise
