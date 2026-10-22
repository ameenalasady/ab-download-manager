# Desktop UI consistency pass

## Compact spacing installed with approval (600×312)

At the user's request, installed and started **`ABDownloadManagerNativeWayland-20261018-033455`**
with 6-dp form gaps and the reduced gap below Size. Tests were explicitly skipped for this
installation; the release distributable was freshly rebuilt and packaged with `build.py
--skip-gradle` (not reused from the previous release). The idle native instance exited
gracefully; the existing native profile and integrations were retained. Verified the new
installed runtime running with WLToolkit and the expected profile (PID **2552757**).
Protected system/XWayland/integration file hashes matched before/after.

Pre-upgrade profile/install record/previous release backup:
`~/.local/state/abdm-backups/native-compact6-profile-20261018-033520/`.
Build/install logs and verification: `build/ui-review/compact6-{package,build,install}.log`
and `compact6-installed.json`. No smoke or real-use UI test actions were run during this
installation.

## Further compact spacing (initial source review; subsequently installed above)

Following the installed 600×336 version, the user requested tighter spacing: form row gaps
are now **6 dp**, with a **600×312-dp** fixed window. The initial compact review was 600×320;
the subsequent 8-dp height reduction specifically trims the gap below Size/above the divider,
without changing field/button sizes or further changing row gaps. Feedback and shared popup
placement remain unchanged, as does the renderer patch. The installed 032752 app/profile
has not been changed or restarted for this follow-up.

Desktop Kotlin compilation and whitespace checks passed. The first 600×320 iteration also
passed the 43 Kotlin tests; isolated native source-overlay review covered long URL/name,
normal/duplicate, and anchored solutions (`compact6-{normal,duplicate,solutions}.png`).
The final 600×312 bounds and duplicate result were asserted and visually reviewed in
`build/ui-review/compact6-tight-bottom.png`. Review used only the disposable,
integration-disabled `build/ui-review/compact6` profile; the review app and fixture were
closed afterwards. No new bundle or installation for this follow-up.

## Spacing correction installed with approval

The user requested installation of the corrected **600×336-dp / 10-dp row-gap** version.
Installed and selected release: **`ABDownloadManagerNativeWayland-20261018-032752`**;
verified running from that installed runtime with WLToolkit, the existing native profile,
and independent IPC (PID **2517554** at installation). The old native instance was idle
(no active downloads) and exited gracefully before the swap. Its current database contained
no download entries; no production data was imported, deleted or used for review.

The full bundle build/Kotlin tests and native Wayland + XWayland download, persistence,
second-launch/IPC and graceful-shutdown smoke checks passed. All smoke profiles were under
ignored `build/ui-review/spacing-smoke/`. Packaged UI review without source overlays verified
the long URL/name form and actual fixed 600×336 AWT bounds in the integration-disabled
`spacing-package` profile. Screenshot: `build/ui-review/spacing-package-normal.png`.
Logs: `spacing-{build,smoke,package,install}.log`; installed verification:
`spacing-installed.json`. The renderer patch hash is unchanged. Protected file hashes
matched before/after installation; the separate system/XWayland installation, browser/CLI
integration, autostart and KWin files were not changed. The existing native integration
policy/settings were retained. Review instance and disposable HTTP fixture were closed.

Pre-upgrade native app-data snapshot and previous release/install record:
`~/.local/state/abdm-backups/native-spacing-profile-20261018-032936/`.
Installer desktop-only backup:
`~/.local/state/abdm-backups/native-desktop-20261018-032937-dfe31e/restore-desktop.sh`.
That script restores the desktop entry only, not the selected release or profile.

## Fixed Add Download bounds and inline metadata feedback (initial source review; subsequently installed above)

Spacing correction requested after review: restored the original **10-dp row gaps** and
reduced the fixed window height by the same four × 6 dp, to **600×336 dp**. This retains
the removed bottom gap without distributing that space between the fields. The captures
and dimension assertions below describe the preceding 600×360/16-dp review, not this
subsequent sizing correction. The correction passed desktop Kotlin compilation; it has
not been installed or activated.

This follow-up leaves installed **`ABDownloadManagerNativeWayland-20261017-180057`**
running and selected. Neither its real-use profile nor the separate system/XWayland
installation was used for iteration or changed.

- Single-download windows now explicitly have equal **600×360-dp minimum/maximum bounds**
  (following app UI scale), with resize/maximize disabled. The initial fixed size alone
  previously still allowed a much larger window. A baseline with a ~1,000-character URL
  did **not** reproduce automatic content-driven growth; the supplied screenshots establish
  the unwanted larger geometry, not its precise cause.
- URL/name remain full-width, single-line, horizontally scrolling/selectable editors; their
  model values are not truncated. URL now has an accessible full-value Copy action alongside
  Paste. Name/folder retain normal editor selection/copy. The form still scrolls vertically
  independently of the footer when content/font size requires it.
- Removed the separate reserved status lane. Duplicate/name/folder feedback now replaces
  resume-support text **beside Size**, without adding or changing row heights. Size and
  refresh/settings remain visible. Full feedback and resume Yes/No remain available through
  the tooltip and accessibility description; feedback retains polite live-region semantics.
  Normal resume information returns when there is no feedback. The initial review distributed
  the released lane's space into 16-dp field gaps; the correction above restores the original
  spacing and removes the equivalent height from the window instead.
- Show solutions / Change solution uses the existing footer, retaining the common anchored
  picker implementation. Shared popup measurement/positioning and the renderer viewport
  synchronization patch are unchanged; no individual positioning offsets were added.

Validation: desktop Kotlin compilation, **43 Kotlin tests** and **41 Python tests** passed;
`git diff --check` passed. The real native app was reviewed with freshly compiled source
classes over the verified bundle, using only integration-disabled disposable `text-baseline`,
`text-candidate`, and `text-final` profiles under ignored `build/ui-review/`. A loopback HTTP
fixture provided a ~1,000-character URL and 209-character filename; nested folder paths,
normal/duplicate/selected strategy, both validation errors, solution popup, and 150% UI scale
were exercised. A real 1-KiB disposable download produced the persisted duplicate. The driver
asserted actual validation states and current/min/max AWT dimensions (600×360 and 900×540),
plus disabled resizing and no top-level picker dialog. Wait for response/category inference
and validation to settle before asserting or setting a custom folder.

Evidence: `build/ui-review/text-{long-normal,long-duplicate,solutions,selected,invalid-name,
invalid-folder,150-duplicate}.png`, `text-contact.png`, `text-{kotlin,python,assertions}.log`.
The reviewed renderer patch still hashes to
`24773ad1996ff30af8789631e245ebc51f7e6cc8ad0f9f0df4a50b0dccfb422f`.
No new bundle was installed/activated; packaged acceptance, the renderer matrix, XWayland,
all locales/font scales, OS clipboard/IME gestures and screen-reader acceptance were not
rerun for this source-only change. Review instances and the HTTP fixture were closed.

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
