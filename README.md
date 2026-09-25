# Galaxy Terminal

Galaxy Terminal is a C/GTK 3 terminal for Cinnamon and Wayland, built with
[XApp](https://github.com/linuxmint/xapp) and the VTE terminal widget. It has
browser-style tabs, named profiles, a graphical preferences window, color
palettes and background opacity, searchable scrollback, editable shortcuts,
and optional copy-on-select.

## Build

On Debian or LMDE, install `meson ninja-build pkg-config libgtk-3-dev
libvte-2.91-dev libxapp-dev` and a C compiler. Then run:

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
./build/galaxy-terminal
```

To install, run `sudo meson install -C build`. To use it for `Terminal=true`
desktop launchers, set Galaxy Terminal as the default terminal in Cinnamon's
Preferred Applications. Debian's `x-terminal-emulator` alternative can also
be set by a distribution package; installation from this source tree does not
change the system's alternatives.

## Usage

`galaxy-terminal` starts a new window. `galaxy-terminal --new-tab` opens a tab
in an existing window. `--working-directory DIR`, `--profile NAME`, and
`--title TITLE` select the initial state. Use `--` to launch an executable
without invoking a shell, for example:

```sh
galaxy-terminal --working-directory /tmp -- htop
```

Each tab has its own PTY and process. A new tab opened with **+** inherits the
current tab's directory when the shell reports it to VTE; otherwise it uses
the directory in which the existing tab started. Changing a profile affects
new tabs and the appearance of existing tabs using that profile. Changing
the profile's shell does not replace already running shells.

The preferences window edits `~/.config/galaxy-terminal/settings.ini` (or the
equivalent under `$XDG_CONFIG_HOME`). Changes to that file are reloaded while
the application runs. A profile can choose the System, Dark, Light, or Custom
palette, 16 editable ANSI colors in the Custom palette, an initial directory,
and background opacity. The default scrollback is 10,000 lines per
tab. Ctrl+Shift+F searches retained output in the active tab; it does not
search the shell's command history. Unlimited scrollback is optional.

Selection normally uses the primary selection, where provided by the desktop.
The optional **Copy mouse selection to clipboard** setting also copies a
completed mouse selection to the regular clipboard. Right-click offers Copy,
Paste, and Select All; Shift+right-click passes the event to the terminal
program. Ctrl+Shift+C/V copy and
paste explicitly. Ctrl+plus/minus/0 zooms the active tab. Ctrl+PageUp and
Ctrl+PageDown switch tabs; all application shortcuts can be reassigned from
Preferences, while Ctrl+Left/Right remains available to shells for moving
between words.

Galaxy Terminal intentionally does not keep shells running after its windows
close. It does not restore sessions or implement terminal image protocols.
