# Qt/KDE frontend

A Qt 6 Quick + Kirigami frontend for AB Download Manager, built for KDE Plasma on
native Wayland. It contains no download logic: every list, transfer, queue, category
and setting comes from the toolkit-neutral Kotlin service in
[`../service/`](../service/README.md), which owns the existing `downloader:core`
engine and the original on-disk formats. The frontend runs inside the Qt host
executable in [`../qt-capability/`](../qt-capability/README.md) (`--view downloads`).

The Compose desktop application remains the reference implementation. This port is
not yet a full replacement; see [Limitations](#limitations).

## Architecture

| Path | Role |
|---|---|
| `src/servicedownloads.*` | `ServiceDownloads`: authenticated snapshot/command adapter for the service's private socket. Validates every snapshot before replacing state, never replays commands after a lost reply, keeps stale rows visible (actions disabled) while disconnected. |
| `src/previewdownloads.*` | `PreviewDownloads`: five labelled in-memory example rows for the synthetic preview. No I/O. |
| `src/nativewindowstyle.*` | Window material: samples the desktop style's window color/translucency and requests KWin blur behind each toplevel. |
| `src/desktopopener.*` | Opens completed files (OpenURI portal) and shows them in the file manager over D-Bus in the user's session; the frontend's private HOME/XDG tree would break `xdg-open`. |
| `src/downloadpartslots.*` | Stable presentation rows for engine parts in progress windows. |
| `src/aboutcredits.*` | License and translator credits for About. |
| `qml/DownloadsView.qml` | Main window: menus, toolbar, category/queue sidebar, sortable table, status bar, Add/Remove/Move/Reset dialogs. |
| `qml/DownloadWindow.qml`, `DownloadContent.qml`, `DownloadCompletion.qml` | Per-download progress window and its completion page. |
| `qml/BrowserRequests.qml` | Review window for links from the browser extension, clipboard or Multiple Add; becomes the progress window in place. |
| `qml/ServiceWorkflows.qml`, `CategoryWorkflows.qml`, `HttpOptions.qml`, `DownloadDetails.qml`, `ChecksumWorkflow.qml` | Settings, Queues, Categories, connection/authentication options, details/error and checksum dialogs. |
| `qml/Tokens.qml`, `PreviewTheme.qml` | The single source of spacing/sizing/type tokens and theme-derived colors. |
| `qml/Ui*.qml`, `DialogFooter.qml`, `WindowFooter.qml`, `PaneSeparator.qml`, `DuplicateNotice.qml`, … | Shared controls and chrome. |

### UI conventions

- Every dialog is a `UiDialog` (`Popup.Window`): a native decorated window whose title
  appears only in the title bar. The only window-level horizontal line is the one above
  the button footer (`DialogFooter` for dialogs, `WindowFooter` for top-level windows);
  sidebars are divided from their page by a vertical `PaneSeparator`; content sections
  use spacing and headings, not lines.
- Footers follow KDE button order (affirmative first, Cancel/Close last). The primary
  button is the first visible non-flat AcceptRole button.
- Combo lists stay in their window's overlay (a nested popup window loses its grab on
  Wayland). They open below the field when they fit, otherwise on the roomier side.
- Duplicates are resolved inline: Add/review banners show the existing download with
  Resume/Open/Show and a Keep both / Replace choice that Download applies directly.
- Windows are content-sized and measured before mapping, so the first frame shown is
  already the natural size.

## Running

Synthetic preview (example data, no service):

```sh
./scripts/qt-capability/build.sh
python3 scripts/qt-capability/run.py --snapshot-desktop-style -- \
  --view downloads --instance-tag migration-preview --width 1054 --height 640
```

Real engine, isolated fixture profile: start the service as described in
[`../service/README.md`](../service/README.md#build-run-test), then

```sh
python3 scripts/qt-capability/run.py --snapshot-desktop-style -- \
  --view downloads --instance-tag migration-service --width 1054 --height 640 \
  --service-descriptor "$PWD/build/qt-service/<new-profile>/qt-service.json"
```

Real engine with an explicit runtime policy (HTTP(S), chosen destination roots,
proxies): use `scripts/qt-service/runtime.py init|launch|stop`, also documented in the
service README. `run.py` always gives the frontend private HOME/XDG state and a
disposable app identity; it never uses a production profile or registration.

Closing the frontend leaves the service and its transfers running.

## Features

Implemented end to end against the real engine (each gated on the capability the
service advertises, so older services simply hide the control):

- Add (single and Multiple Add with search, per-link names and Check Sizes), Download
  or Add Paused, connection/authentication/header options, category Auto/None/fixed,
  optional destination override; inline duplicate handling.
- Pause/resume/retry, Stop All, queue Start/Stop All; Remove (keep files or delete
  files), Move/Rename, Reset progress, copy links, details and error reasons.
- Stable-ID multiselection and bulk actions, persistent column sorting, search,
  category and queue filters, status bar totals.
- Progress windows with live engine parts, completion page with Open File/Folder.
- Queues: configuration, items/reordering, scheduler, completion power actions with a
  cancellable countdown.
- Categories: create/edit/reorder/delete, icons, file-type and URL rules, optional
  per-category destination.
- Completed-file checksums (MD5/SHA-1/SHA-256/SHA-512) with saved expectations.
- Settings for display units, engine (threads, retries, part size, speed limits),
  integration, notification sounds and duplicate handling.
- Browser extension intake and native-messaging host, tray, notifications, drag and
  drop of links, File → Exit with active-work confirmation, About/Licenses/Translators.

## Testing

See [TESTING.md](TESTING.md) for the CTest suites, the service and Python tests, the
offscreen gallery and focused native-Wayland runs. Window and resize design notes are in
[NATIVE-WINDOWS.md](NATIVE-WINDOWS.md).

## Limitations

- No large-history paging: the frontend consumes full snapshots, which fail above the
  2 MiB frame bound. The service already has `history.page`/`history.delta`.
- No import of an existing Compose profile; the Qt runtime starts from a new profile.
- HLS, system/PAC proxies and authenticated SOCKS are not supported.
- No updater UI and no translation catalogs.
- The service-adapter test suite runs each case in its own process: repeated QML-engine
  teardown in one process can corrupt the heap in the Qt/KDE stack. This is test
  isolation, not a fix of that defect.
- Manual keyboard/focus/accessibility acceptance and packaging/redistribution review
  are still open.
