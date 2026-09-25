#!/usr/bin/env bash
set -euo pipefail

tmp=$(mktemp -d)
weston_pid=''
xvfb_pid=''
cleanup() {
    if [[ -n "$weston_pid" ]]; then
        kill "$weston_pid" 2>/dev/null || true
        wait "$weston_pid" 2>/dev/null || true
    fi
    if [[ -n "$xvfb_pid" ]]; then
        kill "$xvfb_pid" 2>/dev/null || true
        wait "$xvfb_pid" 2>/dev/null || true
    fi
    rm -rf "$tmp"
}
trap cleanup EXIT

mkdir -m 700 "$tmp/runtime" "$tmp/config"
export XDG_RUNTIME_DIR="$tmp/runtime"
export XDG_CONFIG_HOME="$tmp/config"
export WAYLAND_DISPLAY=wayland-galaxy
export GDK_BACKEND=wayland
export G_DEBUG=fatal-criticals
export DISPLAY=:99

Xvfb "$DISPLAY" -screen 0 1280x800x24 -nolisten tcp >"$tmp/xvfb.log" 2>&1 &
xvfb_pid=$!
for attempt in {1..100}; do
    if [[ -S /tmp/.X11-unix/X99 ]]; then break; fi
    if ! kill -0 "$xvfb_pid" 2>/dev/null; then
        cat "$tmp/xvfb.log"
        exit 1
    fi
    sleep 0.1
done

weston --backend=x11-backend.so --socket="$WAYLAND_DISPLAY" --idle-time=0 \
    >"$tmp/weston.log" 2>&1 &
weston_pid=$!
for attempt in {1..100}; do
    if [[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]]; then break; fi
    if ! kill -0 "$weston_pid" 2>/dev/null; then
        cat "$tmp/weston.log"
        exit 1
    fi
    sleep 0.1
done
if [[ ! -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]]; then
    cat "$tmp/weston.log"
    exit 1
fi

dbus-run-session -- timeout 20s ./build/galaxy-terminal -- /bin/true
dbus-run-session -- timeout 20s ./build/test-preferences-smoke
