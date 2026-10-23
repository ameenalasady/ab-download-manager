#!/bin/sh
# Usage: sweep.sh <label> <resource-class-match> <client command...>
# Interactive-resize throughput: a nested KWin (a window on the current session, on a
# private session bus, with private copies of kwinrc/kdeglobals/kcminputrc/Kvantum)
# sweeps the client's width every 8 ms and reports how many sizes KWin applied. KWin
# applies a size only after the client acked and committed it, so this measures the
# configure -> commit round trip that an edge drag depends on. No real config is written.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
label=$1; match=$2; shift 2
tree=$(mktemp -d /tmp/abdm-nested-run-XXXXXX)
mkdir -p "$tree/home" "$tree/config" "$tree/data" "$tree/cache" "$tree/state"
for f in kwinrc kdeglobals kcminputrc; do [ -f "$HOME/.config/$f" ] && cp "$HOME/.config/$f" "$tree/config/"; done
[ -d "$HOME/.config/Kvantum" ] && cp -r "$HOME/.config/Kvantum" "$tree/config/"
sed "s/%TARGET%/$match/g" "$here/sweep.qml" > "$tree/sweep.qml"
cat > "$tree/session.sh" <<INNER
#!/bin/sh
cd "$tree/home"
QT_QPA_PLATFORM=wayland QT_NO_XDG_DESKTOP_PORTAL=1 $* > "$tree/client.log" 2>&1 &
client=\$!
sleep 4
id=\$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadDeclarativeScript "$tree/sweep.qml" abdm-sweep)
qdbus6 org.kde.KWin /Scripting/Script\$id org.kde.kwin.Script.run
sleep 6
kill \$client 2>/dev/null || true
sleep 0.5
INNER
chmod +x "$tree/session.sh"
env HOME="$tree/home" XDG_CONFIG_HOME="$tree/config" XDG_DATA_HOME="$tree/data" XDG_CACHE_HOME="$tree/cache" XDG_STATE_HOME="$tree/state" \
  QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES="kwin_scripting.debug=true;js.debug=true;qml.debug=true" dbus-run-session -- kwin_wayland --wayland-display "$WAYLAND_DISPLAY" --socket "abdm-nested-$$" --width 1500 --height 900 \
  --no-lockscreen --no-global-shortcuts --no-kactivities --exit-with-session "$tree/session.sh" > "$tree/kwin.log" 2>&1 || true
echo "$label $(grep -ho 'ABDM-SWEEP .*' "$tree/kwin.log" | head -1) tree=$tree"
