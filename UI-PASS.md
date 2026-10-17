# Desktop UI consistency pass

## Add Download form cohesion follow-up (installed)

The user approved installation of **`ABDownloadManagerNativeWayland-20261017-180057`**.
It is selected by the user-local native `current` link and running from the user desktop
entry. The real-use native profile and its two existing completed downloads were retained;
no active transfers were present. The pacman/AUR XWayland package and its data/integration
were not changed. Desktop-only rollback:
`~/.local/state/abdm-backups/native-desktop-20261017-180532-b52575/restore-desktop.sh`.

- The unlabeled resume-support line is gone. Size has an explicit label in the form's
  existing 128-dp label column; resume support has a check/cross, readable label and
  Yes/No tooltip/accessibility description. It remains visible for valid duplicate
  responses instead of degenerating into an empty line. Metadata can wrap beside the
  consistently sized refresh/settings controls.
- Duplicate, invalid-name and unwritable-folder messages share a reserved 36-dp-minimum
  status lane **outside the scrolling form**, immediately above a subtle footer divider.
  They no longer insert animated rows beneath fields or displace Size/settings. Long
  messages ellipsize with full-text tooltips; the message region uses polite live semantics.
- Show solutions / Change solution is a content-sized, quiet text-style action in that
  status lane, with a dropdown chevron, normal keyboard-focus/hover feedback and a 32-dp
  minimum target. Its panel uses the shared readable 340-dp popup width, independently of
  the small trigger. Shared placement and viewport synchronization are unchanged; no
  positioning offsets were added.
- Selected strategies are shown in neutral secondary text; Add/Download return to the
  footer and Change solution stays available. Unresolved duplicates expose Open File
  when available; Cancel remains separated on the right. The primary Download action
  retains its emphasis and invalid states disable confirmation.
- Default/minimum single-download size is **600×360 logical dp**: 20 dp more height pays
  for the always-reserved status lane without squeezing the form. Field row rhythm,
  full-width text fields, scrolling form and wrapping footer are retained.

Validation: desktop Kotlin compilation, **43 Kotlin tests**, **41 Python tests**, native
bundle build, and both-backend full-app smoke checks (engine download/SHA-256, saved-state
reopen, independent IPC/second launch, graceful quit, protected-file checks) passed.
Smoke profiles were explicitly rooted in `build/ui-review/cohesion-smoke/`. Renderer patch
SHA-256 remains `24773ad1996ff30af8789631e245ebc51f7e6cc8ad0f9f0df4a50b0dccfb422f`.
The renderer matrix was not rerun for this form-only change.

Real-app review used only isolated, integration-disabled `build/ui-review/cohesion-*`
profiles: installed bundle baseline; normal/duplicate/selected strategy; invalid filename
and folder; category/location/settings/queue/solution popups; 150% validation/solutions;
200% duplicate/solutions; XWayland normal/duplicate/solutions. Final packaged candidate
was also reviewed **without** source overlays. Captures: `cohesion-final-{normal,duplicate,
selected,solutions,category,location,settings,queue}.png`,
`cohesion-final-150-{validation,solutions}.png`, `cohesion-200{,-solutions}.png`,
`cohesion-invalid-{name,folder}.png`, `cohesion-x11-{normal,duplicate,solutions}.png`.
Logs: `cohesion-{build,kotlin,python,smoke}.log`. XWayland event coordinates required
accounting for the host's monitor transform; no global input was injected. Synthetic
arrow-key events did not activate the plain solution rows, and one Escape dispatch had
no focused AWT target; keyboard/real OS-input acceptance is not claimed. Screen-reader,
all-locale, light-theme and interactive mixed-monitor checks remain outside this pass.
Review instances and the disposable HTTP fixture were closed after review.

## Previous Add Download popup refinement (subsequently installed as 174708)

At review time the installed baseline was `ABDownloadManagerNativeWayland-20261017-171919`.
This refinement was subsequently installed with approval as **174708** and superseded
by the cohesion follow-up above. The separate system/XWayland installation was not changed.

- Single-download default/minimum: **600×340 logical dp**, down from 640×400. Tighter
  page padding and row spacing retain a separately scrolling form and wrapping footer.
- `OptionPopupAnchor` measures the opening control in window-local layout pixels.
  `OptionPopup` uses the shared menu gradient, border, rounding, shadow and scroll surface.
  Category/location match their fields; solutions match a 280-dp action button; small
  icon/Add triggers use a consistent 340-dp content panel, bounded by the viewport.
- Shared measurement allows the larger free region above/below the anchor, with the same
  density-scaled 4-dp margin/gap as placement. Placement prefers below when the measured
  menu fits, otherwise above. Long content scrolls rather than being clamped over its
  trigger. **A footer trigger has no usable space below at the compact default size**;
  the short solution menu therefore falls back immediately above, leaving URL/category
  and footer actions visible. It does not attempt to escape the parent viewport.
- Solutions are four plain actions, without heading, introductory text or checkboxes.
  Explanations remain in tooltips and accessibility descriptions, rather than making
  the menu cover most of the form. Choosing a strategy closes the menu as before.
- Settings has a left-aligned compact heading, consistent content padding, no heavy row
  separators or fixed-height dialog shell, and a visible scrollbar. Queue selection is
  also content-sized, retaining its actual radio choices/remember/start options and OK.
  Its old weighted/scrollbar layout was removed: nesting that inside a scrolling menu
  produced an infinite-height measurement error during review; the retest passed.
- Category scrolls its current selection into view when the menu opens. Batch-row
  settings and batch Add use the same anchoring/surface policy.

Validation: **43 Kotlin tests**, **41 Python tests**, and **14 expected renderer-matrix
outcomes**. New measurement/placement cases cover fractional and large densities,
trigger-width matching, viewport-limited panel widths, below preference, and scrolling
on the larger side without covering the anchor; existing RTL/cascade/edge cases remain.
The pinned native viewport synchronization patch was preserved unchanged.

Real-app source-overlay review used only ignored isolated profiles under `build/ui-review/`:
normal, duplicate, selected strategy, category/location/settings/scrolled settings/queue,
batch-row settings at 100%; all single-download picker states at 150%; duplicate form
and solutions at 200%. Representative captures: `refinement-compact.png`,
`refinement-final-solutions.png`, `refinement-final-queue.png`,
`refinement-final150-{form,category,settings,settings-scrolled,location,solutions,queue}.png`,
`refinement-scale200-{form,solutions}.png`, and `refinement-multiple-settings.png`.
Logs: `refinement-final-tests.log`, `refinement-python-tests.log`,
`refinement-renderer-matrix.log`. XWayland's category width/placement was also visually
checked (`refinement-x11-menu.png`); its resize/capture runs showed unstable mixed-density
scaling and are **not** full XWayland visual acceptance. Native mixed-monitor interactive
transitions, real OS input, every locale, and a newly packaged bundle are not implied.

## Follow-up: Add Download sizing and native popup placement (not installed)

The source follow-up leaves the installed release/profile and pacman/AUR app untouched.

Two independent causes were reproduced and corrected:

1. Category, folder, queue, duplicate-solution and extra-settings pickers used modeless,
   undecorated `DialogWindow`s plus `MouseInfo`/absolute `setLocation`. Native Wayland owns
   top-level placement. These are now real, focusable Compose popups anchored to their
   opening control (including multi-download row settings and the batch Add button).
   The unused `BaseOptionDialog` was removed. Escape/outside-click dismissal comes from
   Popup, and density is inherited once rather than reapplying `UiScaledContent`.
2. At non-unit monitor density, the experimental Swing renderer updated the scene's pixel
   density/size without updating `PlatformWindowContext`. An actual category picker reported
   a **640×400 viewport with 1280×800 layout coordinates**, clamping it toward the upper-left.
   The pinned, opt-in native ComposeWindowPanel patch now refreshes the window context from
   the current AWT component before Swing painting. The same case now reports **1280×800**
   for both coordinate spaces. This fixes shared menu/popup clipping at the rendering
   boundary; no offsets or Wayland-specific corrections were added to individual menus.
   XToolkit and the normal packaged Compose dependency remain unchanged.

Add Download now defaults to **640×400 logical dp** (following the app UI scale), with
aligned label/control columns, full-width location/name fields, and a single metadata/
refresh/settings row. The form scrolls independently of the footer; footer actions wrap
when needed, so validation messages or larger text cannot squeeze buttons out of view.
Option surfaces are bounded by the parent window and scroll instead of becoming misplaced
independent windows. Duplicate-solution rows also share a consistent checkbox gutter.

Verification: **39 Kotlin tests**, **41 Python tests**, all **14 expected window-matrix
outcomes** (11 successes / 3 deliberate unsupported-renderer failures), a native bundle
build, and the full-app native Wayland/XWayland download/persistence/IPC/quit smoke suite.
The native standalone window/dialog probes now assert that the popup viewport equals the
measured layout in device pixels; both exercised density 2 on this host. Isolated UI review
covered normal/duplicate states, category/folder/settings/queue pickers, batch-row settings,
Escape dismissal, 150% app UI scale, and an XWayland category control. The final bundle was
also reviewed without source-class overrides. Mixed-monitor **interactive transitions**,
all locales and real compositor keyboard/pointer acceptance are not implied.

Evidence stays under ignored `build/ui-review/`: `popup-acceptance-form.png`,
`popup-acceptance-category.png`, `popup-acceptance-solutions.png`,
`popup-viewport-folder.png`, `popup-viewport-settings.png`, `popup-viewport-queue.png`,
`popup-multiple-settings.png`, `popup-scale150.png`, `popup-x11-category.png`,
`popup-final-tests.log`, `popup-python-tests.log`, `popup-window-matrix.log`, and
`popup-full-app-smoke.log`. No real-use instance was restarted or upgraded.

## Scope

Implemented and visually iterated on the real Compose desktop application, not a mock-up.
The approved build is **installed**: release
`~/.local/opt/ab-download-manager-native-wayland/releases/ABDownloadManagerNativeWayland-20261017-041008`
is selected by the native `current` link and the user desktop entry.
`scripts/native-wayland/install.py --activate-desktop` performed the swap after the prior
idle instance exited. The pacman/AUR package at `/usr/bin/ABDownloadManager`, the browser
host manifests, CLI/generic aliases, autostart, and the separate native data profile were not
rewritten. Desktop-only rollback (desktop entry only):
`~/.local/state/abdm-backups/native-desktop-20261017-042057-7ef7a3/restore-desktop.sh`.
Review profiles and screenshots are in ignored `build/ui-review/`.

### Shared design language

- Desktop body text: 13sp; secondary text: 11sp; small text: 10sp. Existing font/UI scale settings remain supported.
- 8dp common corner radius; 32dp minimum shared control height; 36dp menu rows; 40dp navigation/tab targets.
- Consistent tonal primary actions, subtle surface borders, one accent for selection and checked states.
- Hover/focus feedback on buttons, and correct field content colors and disabled-field interaction.
- Shared dialog footer and auxiliary picker surfaces instead of independent gradient/border variants.

### Menus and pickers

- Menubar, download/context menus, cascading menus, and setting dropdowns use the same rounded,
  lightly bordered surface, restrained shadow, row spacing, and separators.
- Icons always reserve the same gutter, including for items without icons. Shortcut labels are
  readable secondary text rather than tiny badges. Long labels truncate instead of marquee-scrolling.
- Arrow/Home/End navigation, Enter activation, Escape dismissal, and left/right submenu navigation.
  Disabled menu items are excluded from keyboard activation; hover opening has a short delay.
- Long menus scroll and reveal the keyboard-highlighted item.
- Anchored pickers flip above a footer when needed and stay within window bounds. Cascading menus
  flip horizontally at the edges, including RTL. Eight new unit tests cover these placements.
- The new-download transport selector reuses the normal dropdown instead of opening a differently
  styled, separate option window.

### Main workspace

- Primary New Download action and secondary actions share consistent height and baseline.
- Responsive toolbar uses icon/tooltips when labels would overflow, and restores labels on wider windows.
- More consistent category/queue indentation, spacing, selection, and table/toolbar insets.
- Increased row breathing room and accent-based selection.
- Explicit first-use empty state with an action, and a different empty state for searches/filters.
- Cleaner status-bar spacing; search adapts with focus; compact titlebar is used only when it fits.

### Settings, queues, dialogs

- Settings use clearer page headings, a consistent navigation sidebar, aligned control columns,
  quieter grouped panels, and a more useful initial window size. Existing persisted sizes are retained
  subject to the new minimum needed for the two-column form.
- Queue navigation, tabs, configurable panels, and footer follow the same design language.
- New Download has a visible field label, explanatory hint, more room, keyboard submission,
  and Cancel / Continue actions with clear priority.
- Shared confirmation prompts have a consistent surface, spacing, and action order. Destructive
  prompts initially focus Cancel, not Delete. General confirmation titles and semantic colors are corrected.
- Category, folder/history, queue, extra-settings, duplicate-download, library, and non-resumable
  warning surfaces use the shared dialog treatment.

## Verification

- `:shared:app:desktopTest`, `:desktop:shared:test`, `:desktop:app-utils:test`: **36 tests**, zero failures.
- Desktop Kotlin compilation and a complete native Wayland distributable build.
- Full-app native Wayland **and XWayland** smoke checks: settings/URL windows, maximize/restore,
  real loopback engine download with SHA-256 verification, persistence/reopen, single-instance IPC,
  graceful quit, and protected production integration/store checks.
- Visual review in dark and light themes, narrow and wide windows, settings, new-download dialog,
  queue configuration, empty/filtered/populated lists, menubar/context menus, and a footer dropdown.
- In-process keyboard check: navigate File menu to New Download; change theme using dropdown keys;
  navigate a download context menu to its deletion prompt and cancel with Escape. Fixture download
  entries remain intact. No power action was invoked.

Representative images:

- `build/ui-review/comparison.png` — before/after contact sheet.
- `build/ui-review/acceptance-home.png`, `acceptance-home-light.png` — home themes.
- `build/ui-review/after-settings.png`, `acceptance-settings-light.png` — settings.
- `build/ui-review/acceptance-narrow.png`, `acceptance-wide.png` — responsive workspace.
- `build/ui-review/acceptance-footer-menu.png` — dropdown opens above dialog footer.
- `build/ui-review/acceptance-context-menu.png`, `acceptance-delete-prompt.png` — menu and safe confirmation.
- `build/ui-review/acceptance-queues.png`, `acceptance-populated.png` — nonempty layouts.

New copy is in the existing English resource bundle; other languages use the existing fallback
until translated. Exhaustive screen-reader, IME, every locale/theme, mobile/macOS/Windows, and
mixed-monitor DPI acceptance are not implied by these checks. Existing native Wayland limitations
and installation approval requirements in `NATIVE-WAYLAND.md` still apply.

## Reproduce or preview

See [`scripts/ui-review/README.md`](scripts/ui-review/README.md) for the isolated, in-process
review driver and capture workflow. To preview the built UI without switching the installed app:

```sh
cd ~/Code/ab-download-manager-plasma
build/native-wayland/latest/bin/ABDownloadManagerNativeWayland \
  --profile "$PWD/build/ui-review/preview"
```

This creates its own integration-disabled profile. Installation/restarting the real-use app
remains a separate approval step; do not edit an immutable installed bundle in place.
