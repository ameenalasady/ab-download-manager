# Local KDE Plasma / Wayland build

## Origin and installation

- Original modified checkout: `~/Code/ab-download-manager`, based on `v1.10.2`. Left unchanged.
- Saved original patch: `~/Downloads/abdm-linux-wayland-fixes.patch`.
- This worktree: `~/Code/ab-download-manager-plasma`, branch `local/plasma-wayland-1.11.0`, based on upstream `v1.11.0` (`5b2cffe4`).
- Installed app version: `1.11.0+plasma.1`.
- User-local installation: `~/.local/opt/ab-download-manager-plasma/current`, pointing at a versioned release directory.
- Main/CLI/native-host wrappers: `~/.local/bin/ABDownloadManager*`.
- KDE launcher override: `~/.local/share/applications/com.abdownloadmanager.desktop`.
- Autostart: `~/.config/autostart/com.abdownloadmanager.desktop`.
- Native messaging manifests regenerated using the installed local CLI. Their paths point at the versioned release (jpackage resolves symlinks).
- Stock pacman package `ab-download-manager-bin 1.11.0-1` remains unchanged and available as a fallback.

## Audit findings

Retained: null-safe pointer lookup, multi-monitor work-area placement, restoration of reachable geometry, system/fractional UI scaling and font-size support, Linux window activation workaround, Plasma 6 proxy settings, nonblocking external-app launches, file/folder-opening corrections, Linux notification sound fallbacks, power-action support, and settings flush on exit.

Corrected during the port:

1. KDE decoration-section matching previously used `endsWith("kdecoration")`, which failed for this machine's `[org.kde.kdecoration2]`. It now accepts the numbered sections and honors `XDG_CONFIG_HOME`.
2. The title-bar settings-command timeout previously followed blocking `readText()`, making it ineffective for hung commands. Timeout now precedes reading the small settings output.
3. Ported all affected windows to Compose's v2 window API, guarding access until initialization and using request-based state changes.
4. Removed the redundant initial resize of the completed-download dialog. Used upstream minimum-size handling instead of repeating AWT size hints on recomposition.
5. Retained upstream fixes already present for external-library and per-host-settings window scaling rather than duplicating them.
6. Serialize debounced persistence and shutdown flushes. A delayed old snapshot cannot overwrite the current state. Preserve cancellation and log flush errors.
7. Copy live home/settings component geometry into storage before flushing; otherwise their own debounce can leave the storage snapshot behind.
8. Reject nonfinite/out-of-range UI-scale overrides. Recognize `plasma` as well as `kde` for font detection.
9. Restrict the always-on-top activation workaround to Linux.
10. Disable launcher startup notifications when no completion hook exists; enabling them is not itself a fix for a lingering busy cursor.
11. Added the null-safe pointer fix to the newer location-selector dialog as well.

The existing KWin remember-window-positions script blacklist entry for `com-abdownloadmanager-desktop-AppKt` is still present and was not modified. This prevents the script from competing with the app's own restoration.

The user-local main wrapper preserves the Arch launcher's `SKIKO_RENDER_API=SOFTWARE` default. An explicitly set value overrides it.

## Verification

Successful full release build with Java 25:

```sh
JAVA_HOME=/usr/lib/jvm/java-25-openjdk ./gradlew \
  -PlocalAppVersion=1.11.0+plasma.1 \
  :desktop:app-utils:test :desktop:app:createReleaseDistributable
```

Six regression tests passed, with zero failures/errors:

- KDE decoration versions/default right-hand controls
- Left-hand controls
- Missing decoration section
- Fractional window-size round trips (including unscaling height)
- Hung command timeout
- Invalid scale overrides

`git diff --check` and installer `bash -n` passed. Desktop files validated (only the pre-existing multiple-main-category menu hint remains). The installed GUI starts successfully, exposes integration port 15151, and a second launch leaves one GUI process. WM_CLASS matches the launcher and KWin blacklist. Main/CLI versions and native-host manifest paths were checked.

Not automated: visual correctness on every monitor, focus behavior under every KWin policy, browser-extension round trips, audio playback, and suspend/hibernate/shutdown. Power actions were intentionally not invoked. Scaling is detected at startup and uses a global correction; mixed-DPI screens/hotplug still need manual checks. AWT is running through XWayland, not native Wayland.

## Rebuild / reinstall

```sh
cd ~/Code/ab-download-manager-plasma
scripts/install-plasma-local.sh
```

The script builds/tests, requests a graceful exit of any running app, backs up data/integration files, installs a new versioned release, and refreshes KDE/browser integration. Launch afterwards with `ABDownloadManager`.

Package updates do not overwrite the user-local build. Do not install an upstream in-app update over it if you want to retain these patches. To adopt another upstream version, port/audit the patch first and update the local version marker; this is not an automatic patch updater.

## Backups and rollback

Original source/data backup: `~/.local/state/abdm-backups/20261012-182330/`.
Pre-install integration/data backup: `~/.local/state/abdm-backups/install-20261012-183333/`.

To return to the stock package (without rolling back download data):

```sh
ABDownloadManagerCli gui exit
rm ~/.local/bin/ABDownloadManager ~/.local/bin/ABDownloadManagerCli \
   ~/.local/bin/ABDownloadManagerNativeMessagingHost \
   ~/.local/share/applications/com.abdownloadmanager.desktop
cp ~/.local/state/abdm-backups/install-20261012-183333/home/alasady/.config/autostart/com.abdownloadmanager.desktop \
   ~/.config/autostart/com.abdownloadmanager.desktop
/usr/bin/ABDownloadManagerCli native-messaging install
kbuildsycoca6 --noincremental
/usr/bin/ABDownloadManager
```

The local release directory can remain for recovery. Do not restore old `.abdm` data unless you intentionally want to revert intervening settings/download state.
