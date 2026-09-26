# Testing

The supported CI target is Debian 14/testing (`debian:forky`). Compilation treats warnings as errors. The workflow runs a normal build and an ASan/UBSan build, then validates desktop installation, man-page installation and Debian packaging on the normal build.

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
sudo apt install weston xvfb dbus-x11 tmux fonts-noto-color-emoji
bash tests/wayland-smoke.sh
```

Despite its historical name, `wayland-smoke.sh` runs the interactive regression executable and CLI integration script as well as a launch test. It starts nested Weston on Xvfb; Galaxy itself is constrained to the Wayland backend. `G_DEBUG=fatal-criticals` catches invalid GTK casts and API misuse. Each GUI case and the CLI suite get separate D-Bus sessions; all cases run even if one fails. Use a disposable configuration directory when running `test-integration` directly.

Automated coverage:

- Defaults, serialization, profile copy/rename/remove, malformed startup/reload, invalid typed settings, duplicate shortcuts, failed saves and debounce/external-edit precedence.
- Tab switching, window titles, reordering, tab-bar visibility, physical Shift-modified shortcuts, zoom, search matching and per-tab search selection.
- A rendered alpha sample verifies 50% terminal background opacity; a preview PNG is kept with CI logs.
- Child exit with close/rename/context-menu UI open, close cancellation and destruction before asynchronous spawn finishes.
- Preferences name validation, duplication/removal, external reload synchronization, reset controls, shortcut capture and parent destruction.
- CLI help/version while another instance runs, `-e` argument preservation, relative working directory, caller environment isolation and invalid requests.
- Local-vs-remote OSC 7 directory handling and argument normalization.

ASan leak detection is disabled because GTK/XApp/VTE keep process-global caches. Address errors and UBSan remain fatal. This is not proof of race freedom or exhaustive terminal-protocol conformance; VTE provides terminal emulation.

Before a release, also perform these manual checks on the distro’s actual Wayland compositor:

1. Run `tmux -L galaxy-check -f /dev/null new-session`, then `tmux -L galaxy-check set-option -g mouse on`. Test pane selection, wheel scrolling and mouse applications (for example vim with `:set mouse=a`). Normal right-click belongs to the application when it requests mouse reporting. Shift+right-click opens Galaxy’s menu. Verify Shift-drag selection, explicit clipboard copy/paste, optional auto-copy, middle paste where PRIMARY is supported, and bracketed paste into a shell/editor.
2. Put the window over a contrasting patterned background. Compare 100%, 50% and 0% opacity; text, header and menus must remain opaque. Test desktop light/dark switching with Dark, Light, System and Custom profiles. Verify profile font changes, per-tab zoom, Unicode combining marks, wide characters and emoji font fallback. Repeat with HiDPI/fractional scaling and more than one keyboard layout.
3. Open add/rename/duplicate/reset/shortcut dialogs, edit the config externally, and close parent windows. Test shell exit with menus open and a running foreground command during tab/window close. Check that a deleted profile is not resurrected in open tabs.
4. Search output near the scrollback limit, change the limit downward, test case matching and Enter/Shift+Enter/Escape, and confirm ordinary scrolling does not open shell-history files.
5. Launch through the desktop, `x-terminal-emulator -e ...`, and a long-running app instance with a changed caller PATH/environment. Test config-write failures and recovery with the visible Retry button.

Automated compositor tests cannot establish that transparency, physical key layouts, accessibility and clipboard integration look and feel correct on every desktop. Record the compositor, scale, locale and dependency versions when reporting a regression.
