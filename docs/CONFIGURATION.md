# Configuration

The settings file is `$XDG_CONFIG_HOME/galaxy-terminal/settings.ini`, falling back to `~/.config/galaxy-terminal/settings.ini`. The directory is created with mode 0700 and new files with mode 0600. Preferences save automatically after 350 ms without another change, using a consistent replacement and durable write. On save failure, changes remain active in memory and an error is shown; Preferences → General offers **Retry saving settings**. The application also retries outstanding changes when shutting down.

External edits are watched, including atomic file replacement. A valid external edit wins over pending GUI edits. A malformed file keeps the last valid in-memory settings (or defaults at startup) and shows an error; it is not overwritten merely by opening the application. Editing preferences subsequently saves the current usable model. Unknown keys and comments are not preserved by GUI saves. Back up hand-edited files if those annotations matter.

Invalid booleans, integers, colors, opacity, palette names and shortcuts fall back to safe values with a visible warning. Duplicate shortcuts are resolved in registry order: the later conflicting action is disabled and reported. A profile always exists. Deleting an active profile switches its open tabs to the default profile immediately; creating a new profile with the old name does not reattach those tabs.

Example:

```ini
[General]
DefaultProfile=Default
ScrollbackLines=10000
CopyOnSelect=false
ConfirmClose=true
AlwaysShowTabs=false
ShowScrollbar=true
HidePointerWhileTyping=false

[Profile Default]
Shell=
WorkingDirectory=
Font=Monospace 11
Palette=Dark
Foreground=#ebedf4
Background=#191b24
Opacity=1.0
CursorShape=0
CursorBlink=0
AudibleBell=false
ScrollOnOutput=false
ScrollOnKeystroke=true

[Shortcuts]
NewTab=<Control><Shift>t
NextTab=<Control>Page_Down
PreviousTab=<Control>Page_Up
ZoomIn=<Control>plus
ZoomOut=<Control>minus
```

Missing settings use defaults. Profile sections use `[Profile NAME]`; names are unique, at most 100 UTF-8 bytes, and cannot contain brackets or line breaks.

- `ScrollbackLines`: −1 for unlimited, 0 to retain no lines beyond the screen, or up to 1,000,000 lines. The default is 10,000. This is terminal **output**, including program output, not `.bash_history`, `.zsh_history` or another command-history file. VTE manages its backing storage; unlimited retention is not resource-free. Search covers only retained output and has no separate larger history source.
- `CopyOnSelect`: copies a completed mouse selection into CLIPBOARD when enabled. Normal explicit copy/paste remains available. PRIMARY selection/middle-click behavior depends on the compositor’s primary-selection support. Clipboard contents can outlive the tab through a desktop clipboard manager.
- `AlwaysShowTabs=false`: hides the bar for a single tab, displays it for multiple tabs. The header’s new-tab button remains accessible.
- `Shell`: one executable path/name, not a shell command line. Empty uses the account’s login shell. No automatic evaluation of shell syntax occurs.
- `WorkingDirectory`: an accessible absolute path, or empty to inherit the CLI caller’s directory. CLI `--working-directory` takes precedence; relative CLI paths are resolved against that caller. UI-created tabs/windows inherit the active tab’s directory. Local OSC 7 file URIs are accepted only for an empty/local/localhost host; remote hosts are rejected. `/proc/PID/cwd` provides a fallback when shells do not report OSC 7.
- `Palette`: `Dark`, `Light`, `System`, or `Custom`. `System` follows the desktop preference. The application’s chrome always follows the desktop via XApp.
- `Foreground`, `Background`, `AnsiColor0` through `AnsiColor15`: GTK color strings used by `Custom`. Foreground/ANSI alpha is forced opaque; use `Opacity` for the background.
- `Opacity`: finite number from 0.0 to 1.0, where 1.0 is opaque. GTK backing surfaces remain transparent so the compositor can blend the terminal background. This does not implement blur or make text translucent.
- `CursorShape`: 0 block, 1 I-beam, 2 underline. `CursorBlink`: 0 desktop setting, 1 on, 2 off.
- `AudibleBell`, `ScrollOnOutput`, `ScrollOnKeystroke`: booleans. Audible bell delivery also depends on desktop sound settings.

Shortcuts accept GTK key names with Control/Ctrl/Primary, Shift, Alt or Super modifiers. A non-function key must include Control, Alt or Super. An empty value disables the action. Capture in preferences normalizes letter case and the plus key. All default actions and configuration keys are listed in `src/shortcuts.c`.

Each tab has its own PTY, child shell/process, current directory, scrollback and zoom. Tabs share application code and settings but not changes made inside another shell. There is no restoration of running sessions. Close confirmation detects foreground jobs and explicitly launched commands, not every possible shell background job; use tmux for jobs you intentionally want to detach.
