#!/usr/bin/env bash
# Rebuild and install without modifying pacman-owned files.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=${ABDM_LOCAL_VERSION:-1.11.0+plasma.1}
if [[ ${1:-} != --skip-build ]]; then
    cd "$repo"
    JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-25-openjdk} ./gradlew \
        "-PlocalAppVersion=$version" :desktop:app-utils:test :desktop:app:createReleaseDistributable
fi
src="$repo/desktop/app/build/compose/binaries/main-release/app/ABDownloadManager"
[[ -x "$src/bin/ABDownloadManager" ]] || { echo "Build missing: $src" >&2; exit 1; }
actual=$("$src/bin/ABDownloadManagerCli" --version)
[[ $actual == *"$version"* ]] || { echo "Wrong build version: $actual" >&2; exit 1; }
root="$HOME/.local/opt/ab-download-manager-plasma"
backup="$HOME/.local/state/abdm-backups/install-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$root/releases" "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.config/autostart" "$backup"
# Ask a running instance to exit normally, allowing downloads/settings to be saved.
"$src/bin/ABDownloadManagerCli" gui exit
for _ in {1..30}; do
    if ! pgrep -u "$(id -u)" -x ABDownloadManag >/dev/null && ! pgrep -u "$(id -u)" -f '^ABDownloadManager$|/bin/ABDownloadManager( |$)' >/dev/null; then break; fi
    sleep 1
done
if pgrep -u "$(id -u)" -f '^ABDownloadManager$|/bin/ABDownloadManager( |$)' >/dev/null; then
    echo "Close AB Download Manager before reinstalling." >&2
    exit 1
fi
[[ ! -d "$HOME/.abdm" ]] || cp -a "$HOME/.abdm" "$backup/data"
for file in \
    "$HOME/.local/share/applications/com.abdownloadmanager.desktop" \
    "$HOME/.config/autostart/com.abdownloadmanager.desktop" \
    "$HOME/.local/bin/ABDownloadManager" \
    "$HOME/.local/bin/ABDownloadManagerCli" \
    "$HOME/.local/bin/ABDownloadManagerNativeMessagingHost"; do
    if [[ -e $file || -L $file ]]; then cp -a --parents "$file" "$backup"; fi
done
while IFS= read -r -d '' file; do cp -a --parents "$file" "$backup"; done < <(
    find "$HOME/.config" "$HOME/.mozilla/native-messaging-hosts" -name com.abdownloadmanager.json -print0 2>/dev/null || true
)
release="$root/releases/$version-$(date +%Y%m%d-%H%M%S)"
cp -a "$src" "$release"
[[ ! -L "$root/current" ]] || readlink "$root/current" > "$backup/previous-release"
ln -s "$release" "$root/current.new"
mv -Tf "$root/current.new" "$root/current"
for name in ABDownloadManager ABDownloadManagerCli ABDownloadManagerNativeMessagingHost; do
    printf '#!/bin/sh\n' > "$HOME/.local/bin/$name"
    if [[ $name == ABDownloadManager ]]; then
        # Preserve the stock Arch launcher's software-rendering workaround; allow overrides.
        printf 'export SKIKO_RENDER_API="${SKIKO_RENDER_API:-SOFTWARE}"\n' >> "$HOME/.local/bin/$name"
    fi
    printf 'exec "%s/current/bin/%s" "$@"\n' "$root" "$name" >> "$HOME/.local/bin/$name"
    chmod +x "$HOME/.local/bin/$name"
done
printf '%s\n' \
    '[Desktop Entry]' 'Type=Application' 'Name=AB Download Manager' \
    'Comment=Manage and organize your download files better than before (local Plasma fixes)' \
    'GenericName=Download Manager' 'Categories=Network;Utility;FileTransfer;' \
    'Keywords=download;downloader;manager;' \
    "Exec=\"$HOME/.local/bin/ABDownloadManager\"" \
    "Icon=$root/current/lib/ABDownloadManager.png" \
    'Terminal=false' 'StartupNotify=false' \
    'StartupWMClass=com-abdownloadmanager-desktop-AppKt' \
    > "$HOME/.local/share/applications/com.abdownloadmanager.desktop"
printf '%s\n' \
    '[Desktop Entry]' 'Type=Application' 'Name=AB Download Manager' \
    "Exec=\"$HOME/.local/bin/ABDownloadManager\" --background" \
    'Terminal=false' 'NoDisplay=true' 'StartupNotify=false' \
    'X-GNOME-Autostart-enabled=true' 'X-KDE-autostart-phase=2' \
    > "$HOME/.config/autostart/com.abdownloadmanager.desktop"
"$HOME/.local/bin/ABDownloadManagerCli" native-messaging install
if command -v desktop-file-validate >/dev/null; then
    desktop-file-validate "$HOME/.local/share/applications/com.abdownloadmanager.desktop" "$HOME/.config/autostart/com.abdownloadmanager.desktop"
fi
command -v update-desktop-database >/dev/null && update-desktop-database "$HOME/.local/share/applications" || true
command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
printf 'Installed %s\nBackup: %s\nLaunch: %s/.local/bin/ABDownloadManager\n' "$version" "$backup" "$HOME"
