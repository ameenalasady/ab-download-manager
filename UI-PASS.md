# Desktop UI consistency pass

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
