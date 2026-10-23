#!/bin/sh
# Usage: capture.sh <qt-capability-shell> <out.png>
# Blur-region check: the shell's own striped backdrop fills a nested KWin, the synthetic
# downloads view sits on top, and the nested screen is captured through ScreenShot2.
# The permission bypass applies to the nested KWin only. Stripes must be blurred inside
# the window and sharp right outside it.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
bin=$1; out=$2
tree=$(mktemp -d /tmp/abdm-nested-cap-XXXXXX)
mkdir -p "$tree/home" "$tree/config" "$tree/data" "$tree/cache" "$tree/state"
for f in kwinrc kdeglobals; do [ -f "$HOME/.config/$f" ] && cp "$HOME/.config/$f" "$tree/config/"; done
[ -d "$HOME/.config/Kvantum" ] && cp -r "$HOME/.config/Kvantum" "$tree/config/"
cat > "$tree/session.sh" <<INNER
#!/bin/sh
cd "$tree/home"
QT_QPA_PLATFORM=wayland QT_NO_XDG_DESKTOP_PORTAL=1 $bin --backdrop --backdrop-pattern stripes > "$tree/backdrop.log" 2>&1 &
sleep 1.5
QT_QPA_PLATFORM=wayland QT_NO_XDG_DESKTOP_PORTAL=1 $bin --view downloads > "$tree/client.log" 2>&1 &
sleep 3
id=\$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadDeclarativeScript "$here/place.qml" abdm-place)
qdbus6 org.kde.KWin /Scripting/Script\$id org.kde.kwin.Script.run
sleep 2.5
python3 "$here/capture.py" "$out"
kill %1 %2 2>/dev/null || true
sleep 0.5
INNER
chmod +x "$tree/session.sh"
env HOME="$tree/home" XDG_CONFIG_HOME="$tree/config" XDG_DATA_HOME="$tree/data" XDG_CACHE_HOME="$tree/cache" XDG_STATE_HOME="$tree/state" \
  KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 QT_FORCE_STDERR_LOGGING=1 \
  dbus-run-session -- kwin_wayland --wayland-display "$WAYLAND_DISPLAY" --socket "abdm-cap-$$" --width 1500 --height 900 \
  --no-lockscreen --no-global-shortcuts --no-kactivities --exit-with-session "$tree/session.sh" > "$tree/kwin.log" 2>&1 || true
grep -ho 'ABDM-PLACED.*\|captured.*' "$tree/kwin.log" | head -2; echo "tree=$tree"
