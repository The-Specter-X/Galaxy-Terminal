#pragma once

#include <gtk/gtk.h>
#include "shortcuts.h"

typedef enum {
    GALAXY_CHANGE_COLORS = 1 << 0,
    GALAXY_CHANGE_BEHAVIOR = 1 << 1,
    GALAXY_CHANGE_PROFILES = 1 << 2,
    GALAXY_CHANGE_SHORTCUTS = 1 << 3,
    GALAXY_CHANGE_STATUS = 1 << 4,
    GALAXY_CHANGE_RELOAD = 1 << 5,
    GALAXY_CHANGE_ALL = 0x3f
} GalaxyChange;

typedef struct {
    char *name;
    char *shell;
    char *cwd;
    char *font;
    char *palette;
    char *foreground;
    char *background;
    char *ansi[16];
    double opacity;
    int cursor_shape;
    int cursor_blink;
    gboolean audible_bell;
    gboolean scroll_on_output;
    gboolean scroll_on_keystroke;
} GalaxyProfile;

typedef struct _GalaxySettings GalaxySettings;
struct _GalaxySettings {
    GPtrArray *profiles;
    char *path;
    char *default_profile;
    char *shortcuts[ACT_COUNT];
    char *contents;
    char *error;
    int scrollback;
    gboolean auto_copy;
    gboolean confirm_close;
    gboolean show_tabs; /* Otherwise show only with multiple tabs. */
    gboolean show_scrollbar;
    gboolean mouse_autohide;
    GFileMonitor *monitor;
    guint reload_source;
    guint save_source;
    gboolean dirty;
    void (*changed)(GalaxySettings *, guint, gpointer);
    gpointer user_data;
};

GalaxySettings *galaxy_settings_new(void);
void galaxy_settings_free(GalaxySettings *settings);
void galaxy_settings_set_changed(GalaxySettings *settings,
    void (*changed)(GalaxySettings *, guint, gpointer), gpointer user_data);
void galaxy_settings_changed(GalaxySettings *settings, guint changes);
gboolean galaxy_settings_flush(GalaxySettings *settings);
gboolean galaxy_settings_reload(GalaxySettings *settings);
void galaxy_settings_reset_general(GalaxySettings *settings);
void galaxy_settings_reset_shortcuts(GalaxySettings *settings);
GalaxyProfile *galaxy_settings_profile(GalaxySettings *settings, const char *name);
GalaxyProfile *galaxy_settings_add_profile(GalaxySettings *settings, const char *name);
GalaxyProfile *galaxy_settings_duplicate_profile(GalaxySettings *settings,
    const char *source, const char *name);
gboolean galaxy_settings_rename_profile(GalaxySettings *settings,
    const char *old_name, const char *new_name);
void galaxy_settings_remove_profile(GalaxySettings *settings, const char *name);
void galaxy_profile_reset(GalaxyProfile *profile);
gboolean galaxy_profile_validate_command(const GalaxyProfile *profile, char **message);
