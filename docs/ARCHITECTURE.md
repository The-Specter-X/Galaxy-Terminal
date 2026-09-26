# Architecture and ownership

Galaxy is a C11/GTK3 application. GTK and VTE own rendering, accessibility, Unicode/font fallback, PTY I/O and terminal emulation. There is no custom Vulkan renderer and no additional worker thread touching widgets.

- `main.c`: locale setup, Wayland-only backend selection and process entry.
- `app.c`: application lifecycle, startup environment, XApp dark-mode manager and settings notifications.
- `commands.c`: local option handling and forwarded CLI requests. GLib handles help before application registration, so a secondary `--help` cannot exit the server. `-e` is converted into an argument boundary before option parsing.
- `window.c`: notebook, actions/accelerator groups, search, menus and window close confirmation.
- `session.c`: PTY startup, tab lifecycle, title/directory properties, selection and native VTE context menus.
- `appearance.c`: palettes, font and terminal styling; transparent GTK backing surfaces.
- `settings.c`: initialized defaults, transactional parsing, validation, profile operations, debounced durable writes and external reload monitoring.
- `shortcuts.c`: the shared action/default registry and capture normalization.
- `preferences.c`: XApp settings pages with field bindings, profile operations, live preview, reset controls and asynchronous dialogs.

`GalaxyApp` owns settings and the startup environment. Window/tab records are attached to their owning GtkWindow/page with `g_object_set_data_full`; destroy handlers cancel activity before finalization frees records. Child-exit callbacks disconnect when a tab is destroyed. Spawn completion keeps a weak reference to the page, because VTE can complete after widget destruction. Dialog callbacks are connected to their owning widget, with parent/tab destruction dismissing dependent dialogs. No nested `gtk_dialog_run` loops hold tab pointers across a child exit.

The XApp dark-mode manager is shared through process-wide `GtkSettings`. Its asynchronous portal callback does not retain the manager, so tying it to an individual application teardown would allow a pending callback to access freed memory. Galaxy disconnects its own theme handler at teardown while the shared manager remains valid.

GTK accelerator groups handle translated keys and modifiers. The application does not implement a second terminal keyboard protocol. VTE handles mouse-reporting arbitration before opening the application context menu. tmux receives its regular terminal input when no explicit Galaxy shortcut consumes it.

UI work stays on the main context. VTE launches children asynchronously and manages its own I/O. Settings writes are debounced to avoid a filesystem write per slider movement. Per-tab appearance/behavior signatures avoid resetting unchanged properties when another profile or a general setting changes. File writes themselves are small synchronous durable writes; a future async writer would need ordered snapshots and a shutdown drain, not uncoordinated worker writes.

The VTE floor is 0.80: context-menu APIs and modern terminal-property APIs are used directly. Do not add deprecated title/CWD property fallbacks to support older distribution releases. Version strings come from Meson’s generated `config.h`.
