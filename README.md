# Galaxy Terminal

A C terminal for Wayland, built with **GTK 3, XApp and VTE**. The target platform is **Debian 14 (forky/testing)** and distributions based on it. Galaxy keeps terminal emulation in VTE and focuses on a configurable desktop interface.

## Features

- Browser-style tabs with close buttons, a plus button, drag reordering, custom titles and full-title tooltips. The tab bar appears automatically with multiple tabs; “Always show” is optional. A new-tab button remains available in the header.
- Profiles with live text previews, font selection, Dark/Light/System/Custom palettes, all 16 ANSI colors, background opacity, cursor shape/blink, bell and scrolling behavior. Add, duplicate, rename, remove and reset profiles.
- An XApp preferences window with per-setting reset buttons, editable shortcuts, validation messages and save-error reporting. Configuration edits outside the application reload into both open tabs and preferences.
- **10,000 lines of scrollback per tab by default**, with configurable limits or unlimited retention. Search the current tab’s retained output with Ctrl+Shift+F, case matching, next/previous navigation and no-match feedback.
- Mouse selection, clipboard copy/paste, optional copy-on-selection, scroll wheel and tmux/application mouse reporting. Hold Shift to use terminal selection/context-menu behavior while a program captures the mouse.
- Ctrl-click opens HTTP(S) links and supported OSC 8 web/mail links; Select all is available in the context menu.
- Unicode and font fallback through VTE/Pango, including emoji where your installed fonts support them. Sixel is disabled; Galaxy adds no Kitty image protocol or image-helper integration.
- Asynchronous shell startup, safe process-close confirmations and local `--help`/`--version` handling, including while another instance is running.

The terminal starts with a **dark palette**. Application controls follow the desktop’s appearance; terminal colors are independent unless a profile selects **Follow desktop**. Background opacity does not dim text or window controls, and does not implement blur.

Closing a tab ends its terminal session. There is no session restoration or background session keeper. tmux can provide persistence when you explicitly use it.

## Build on Debian 14/testing

Requirements: GTK ≥3.24, **VTE ≥0.80**, XApp ≥2.8, PCRE2, Meson, Ninja, a C11 compiler and gettext. The application connects only to Wayland; XWayland is not a runtime requirement.

```sh
sudo apt install build-essential meson ninja-build pkgconf gettext \
  libgtk-3-dev libvte-2.91-dev libxapp-dev libpcre2-dev
meson setup build --prefix=/usr
meson compile -C build
meson test -C build --print-errorlogs
sudo meson install -C build
```

For a Debian package, install `debhelper` and `dpkg-dev`, then run:

```sh
dpkg-buildpackage --no-sign -b
```

The package registers a priority-40 `x-terminal-emulator` alternative. Use `sudo update-alternatives --config x-terminal-emulator` to select it. It requires a Wayland session even when called through that alternative. Desktop-specific default-terminal settings may need separate configuration. Plain `meson install` does not change system alternatives.

## Usage

```sh
galaxy-terminal
galaxy-terminal --new-tab
galaxy-terminal --profile Work --working-directory ~/Projects
galaxy-terminal --title Build -- make -j4
galaxy-terminal -T Monitor -e htop
```

`--` or `-e` introduces an executable and its arguments, without implicit shell evaluation. Use `-- sh -c '…'` when shell syntax is needed. The application reports launch-request status; it does not wait for or return the terminal command’s exit status. A spawn failure remains visible in its tab.

Command-line launches receive the **invoking process’s environment and directory**, including when forwarded to an existing instance. UI-created tabs use the application’s startup environment and inherit the active tab’s directory/profile. Shell exports are not shared between neighboring tabs. Galaxy sets `TERM=xterm-256color` and `COLORTERM=truecolor`, and removes inherited `TMUX`/`TMUX_PANE` markers from a newly created PTY.

## Default shortcuts

| Action | Shortcut |
|---|---|
| New tab / window | Ctrl+Shift+T / Ctrl+Shift+N |
| Close tab | Ctrl+Shift+W |
| Copy / paste | Ctrl+Shift+C / Ctrl+Shift+V |
| Search scrollback | Ctrl+Shift+F |
| Next / previous tab | Ctrl+Page Down / Ctrl+Page Up |
| Tabs 1–8 / last tab | Alt+1…8 / Alt+9 |
| Zoom in / out / reset | Ctrl + / Ctrl − / Ctrl+0 |
| Preferences | Ctrl+, |
| Fullscreen | F11 |

“Ctrl +” means Ctrl with the **plus character**, including Shift when required by the keyboard layout. Ctrl+Left/Right remain available for shell word navigation, but can be assigned to tab switching in preferences. Search uses Enter/Shift+Enter for next/previous and Escape to return to the terminal.

## Configuration and development

Settings are stored in `$XDG_CONFIG_HOME/galaxy-terminal/settings.ini` (normally `~/.config/galaxy-terminal/settings.ini`). See [configuration](docs/CONFIGURATION.md), [architecture](docs/ARCHITECTURE.md), [testing](docs/TESTING.md), and `man galaxy-terminal`.

CI builds on `debian:forky`, runs settings and Wayland integration tests, repeats them under ASan/UBSan, and checks installation and Debian packaging. GTK/VTE/XApp process-global allocations are excluded from leak detection; use-after-free and undefined-behavior checks remain enabled. Testing details and the manual compositor/tmux checklist are documented separately.

The interface is gettext-ready. English is currently supplied; see [translation instructions](po/README.md) to contribute another language. The source is MIT-licensed; dependencies retain their own licenses.
