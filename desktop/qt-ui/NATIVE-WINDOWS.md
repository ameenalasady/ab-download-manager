# Native windows, review → progress, engine parts and resizing

Design notes for the Qt frontend's top-level windows: decorations and material, the
progress/completion layout, the browser review window and its transition into a
progress window, the read-only `download.parts` projection, and resize performance.

## Decorations and material

All three frontend window kinds (main, native browser review, download) are ordinary
`Qt.Window` toplevels. KWin owns the titlebar, close/minimize/maximize buttons,
window shadow, rounded exterior, move and resize. Title alignment is the user's
KWin decoration setting, not custom QML chrome. Dialogs (`UiDialog`) are native
`Popup.Window` toplevels too; their title appears only in the title bar.

Removed from the production shell: `FramelessWindowHint`, client shadow Canvas,
full-subtree MultiEffect/mask layer, shadow margin/radius, input masks, per-resize
region/diagnostics work, caption buttons and resize MouseAreas. The region-geometry
utility and its tests remain, but the executable no longer links it.

`NativeWindowStyle` samples `QStyle::PE_Widget` on an **unshown, polished
QMainWindow**, the widget type used by Dolphin. This uses the active Kvantum window
material and its translucency decision, including opaque themes; unpainted styles
(such as Breeze) fall back to their Window palette color. It samples the central
flat material, not Dolphin's QWidget-specific view/toolbar gradients. No arbitrary
application opacity, theme-file parsing or forced 80%-opacity setting is used.

Main, review and progress use that material as their background. Opaque View-colored
lists remain opaque, as in a file manager; foreground items retain opacity 1.
When the material is translucent, the KF6 KWindowEffects bridge requests KWin blur
behind the whole surface: one size-independent rectangle (see
[resizing](#interactive-resize-pacing)), sent on show/expose and never on resize.
The development launcher (`scripts/qt-capability/run.py --snapshot-desktop-style`)
gives the frontend private writable HOME/XDG; kdeglobals and Kvantum selection/theme
data are copied, never linked back. Theme changes apply on
next frontend launch. The executable remains native Wayland only, with no X11 fallback.

## Progress and completion layout

The progress window and its completion page share the app's footer convention: a
separator above right-aligned actions (`WindowFooter`), wrapping at narrow widths.
Progress offers Pause or Start/Resume/Retry and Close; completion offers Open File,
Open Folder and Close. Closing never pauses, cancels or removes a download.

`DownloadContent.qml` follows the original Compose single-download ordering:
- Info section: name, status, size, downloaded bytes/percent, speed, time left,
  resume support; location/source retained from the MVP;
- progress bar;
- engine pieces/ranges visualization, then # / Status / Downloaded / Total table;
- explicit errors, unknown command outcomes, disconnected/stale and removed states;
- the action footer.

Unknown total/length/support remain unknown, never estimated locally. Parts use
engine byte offsets and counters; unknown-length pieces are not drawn as a made-up
fraction. Completed/retired parts are explicitly unavailable rather than fabricated.
The table is bounded/scrollable; omitted parts have a visible truncation notice.

**Not yet ported:** the original live speed/threads tab and per-download
on-completion settings. The paused-only Connection & Authentication editor is
reachable from the main list; reusing it as a live tab would change the safe
edit/queue/drain semantics.

## Native browser review and transition

The review is a separate native `QQC2.ApplicationWindow`, `transientParent: null`,
not a main-window UiDialog/overlay. New intake opens it even while the list is hidden
in the tray, without unhiding the list. Main overlays do not block it.

No client positioning, output coordinates, input injection, forced focus, activation
retry loops or synthetic activation tokens. Initial placement is KWin's active-output
policy. Normal tokenless activation is allowed to be denied; a real notification's
xdg-activation token is routed directly to the review, not consumed by the main list.
An existing/reused toplevel keeps its compositor-chosen placement; there is no promise
to forcibly move an existing window to another output.

**Download:** wait for a fully successful `browser.accept` reply **and its coherent
resulting snapshot**, then reuse the same review native surface for the first selected
ID. This preserves screen/position/activation context without a move request.
The window registry keys it by stable string ID, never selection or row index.
For batches, additional IDs open immediately in remaining automatic slots, including
queued IDs. At most **three automatic progress windows** exist at once; the rest stay
in the downloads list. Existing windows count against the cap. If full, or progress
windows are disabled, the acknowledged review closes instead. Explicit reopen is
uncapped. Subsequent engine hints reuse the registered ID and cannot duplicate it.

Add Paused closes only after acknowledgement/snapshot, adds to the list and opens
no progress window. Later or the window-manager Close postpones locally, with no
command. Discard removes only the service review record; its window now also waits
for acknowledgement, so rejection/unknown errors remain visible even with the main
window hidden. The review actions wrap into rows at narrow widths. Single-link reviews
resolve duplicates inline (Keep both / Replace existing) before Download.

Unknown/partial outcomes never transition and never auto-replay acceptance. The
durable `outcomeUnknown` reservation continues disabling acceptance. Cookies,
authentication, headers and raw part exceptions are never displayed. The frontend
receipt is memory-only and discarded across disconnect/epoch changes. Reconnect's
first window-hint snapshot is a fresh baseline, even within the same epoch, while
same-epoch serial regressions still reject. No old hints are replayed.

## Bounded `download.parts` API

Capability: `download.parts`. It is a read-only **on-demand snapshot projection**,
not a command and not a new transfer/persistence implementation:

```json
{"type":"snapshot", "partsFor":["9007199254740993"]}
```

Optional `partsFor` accepts at most **16 unique canonical nonnegative decimal
64-bit string IDs**. Bad arrays, duplicates, numbers/negative/overflow IDs close that
client under the existing malformed-protocol contract; no command is applied.
Omitting it returns `downloadParts:{}`. Unknown/retired IDs return unavailable.
Each selected ID has `available`, nullable boolean `resumeSupport`, `totalCount`,
`truncated`, and at most **256** `items` with only:

```json
{"id":"0", "offset":"0", "length":"1048576", "downloaded":"4096", "state":"downloading"}
```

Length is nullable; all byte positions/counters are strings. State is one of `idle`,
`connecting`, `downloading`, `completed`, `disconnected`. Source: existing
HttpDownloadJob/getParts and engine resume-support state, on the service owner
context. Volatile endpoints may change on dynamic splitting, so counters are
bounded to the sampled range length. There are **no** URLs, paths, headers,
credentials or exception messages in this projection, and reading does not save,
reserve IDs, probe, resume or alter parts. Existing 2 MiB frame bounds remain.

Qt refcounts visible-window interests, requests no more than 16 IDs per poll, validates
the complete projection before replacing coherent state, and whitelists the fields
exposed to QML. Extra manual windows explicitly report when part-detail capacity is
exhausted; reopen after closing another such window. Disconnect retains last-known
parts under the same stale-state warning and disabled-command policy.

## Resize profiling

The opt-in `--resize-profile FILE` is refused with a service descriptor. It measures
180 sequential synthetic resize-to-frame steps, 780×610 through 1200×820, against
five preview rows, on native Wayland under the copied desktop style. Reports include
raw sample latencies, process CPU and wall time, graphics API/backend. This is not
an interactive pointer/compositor-latency or large-history benchmark.

| Native Wayland workload | Before p50 / p95 | After p50 / p95 | Before / after CPU |
|---|---:|---:|---:|
| Default OpenGL | 157.08 / 294.91 ms | 1.58 / 11.82 ms | 41,712 / 512 ms |
| Software | 225.74 / 311.10 ms | 4.83 / 5.83 ms | 43,165 / 1,027 ms |

The baseline resize log shows **three WINDOW_STATE/region updates per size**
(widthChanged, heightChanged and resizeEvent), in addition to full-size Canvas shadow
repainting and whole-subtree masking. Both backends improved sharply after removing
that architecture (client-side decorations, shadow and mask) while retaining a
translucent client surface. No table/model rewrite or backend override was needed.

## Interactive resize pacing

Edge drags were visibly choppier than Dolphin. The resize probe showed QML polish,
sync and GPU render at ~0 ms per frame; the time was in swap. Qt Wayland emulates a
blocking swap by waiting for the compositor's frame callback, and KWin sends the next
interactive size only after the previous one is committed, so each resize waited up
to a refresh. `scripts/qt-capability/resize-sweep/sweep.sh` (nested KWin, private bus
and config copies, 8 ms width steps) measures sizes KWin actually applied:

| Client | Applied / 240 | Gap p50 / p95 / max |
|---|---:|---:|
| Dolphin | 238 | 8 / 11 / 12 ms |
| Before | 108 | 18 / 19 / 24 ms |
| `QSG_RENDER_LOOP=basic` | 159 | 12 / 13 / 19 ms |
| `QT_QUICK_BACKEND=software` | 224 | 9 / 10 / 13 ms |
| After (`swapInterval 0`) | 239 | 8 / 9–10 / 10–11 ms |

The default surface format now requests swap interval 0 (not `QSG_NO_VSYNC`, which
external apps launched by the opener would inherit). Wayland presents whole buffers,
and a QML animation probe stays at exactly the refresh rate with unchanged CPU time.
The on-desktop resize probe went from p50/p95/max 2.0/12.2/16.6 ms (2.2 s wall) to
1.8/3.3/8.3 ms (1.1 s wall), CPU unchanged.

NativeWindowStyle no longer resends a blur region on every resize. It sends one
size-independent rectangle (KWindowEffects' own "whole window" value) on show/expose;
KWin clips it. `resize-sweep/capture.sh` compares a striped backdrop through the
window: bands just outside every edge match the bare backdrop exactly in both
versions (no bleed). The old per-size region trailed the resized buffer and could
leave a stale unblurred band after a resize settled; the new region blurs the whole
window.

## Open items

- Supervised pointer/keyboard/IME/screen-reader and multi-output acceptance.
- KWin active-output placement and blur/decoration appearance across themes.
- The original live speed/threads and per-download on-completion tabs.
