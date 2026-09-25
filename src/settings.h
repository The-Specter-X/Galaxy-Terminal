#pragma once

#include <gio/gio.h>
#include <gtk/gtk.h>

typedef enum {
    ACT_NEW_TAB, ACT_NEW_WINDOW, ACT_CLOSE_TAB, ACT_COPY, ACT_PASTE,
    ACT_FIND, ACT_NEXT_TAB, ACT_PREV_TAB, ACT_ZOOM_IN, ACT_ZOOM_OUT,
    ACT_ZOOM_RESET, ACT_PREFERENCES, ACT_FULLSCREEN, ACT_TAB_1,
    ACT_TAB_2, ACT_TAB_3, ACT_TAB_4, ACT_TAB_5, ACT_TAB_6,
    ACT_TAB_7, ACT_TAB_8, ACT_TAB_9, ACT_COUNT
} GalaxyAction;

extern const char *const galaxy_action_names[ACT_COUNT];
extern const char *const galaxy_action_labels[ACT_COUNT];

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
} GalaxyProfile;

typedef struct {
    GPtrArray *profiles;
    char *path;
    char *default_profile;
    char *shortcuts[ACT_COUNT];
    char *contents;
    int scrollback;
    gboolean auto_copy;
    gboolean confirm_close;
    gboolean show_tabs;
    gboolean show_scrollbar;
    gboolean mouse_autohide;
    gboolean follow_dark;
    GFileMonitor *monitor;
    guint reload_source;
    void (*changed)(gpointer user_data);
    gpointer user_data;
} GalaxySettings;

GalaxySettings *galaxy_settings_new(void);
void galaxy_settings_free(GalaxySettings *settings);
void galaxy_settings_set_changed(GalaxySettings *settings,
                                 void (*changed)(gpointer), gpointer user_data);
void galaxy_settings_save(GalaxySettings *settings);
GalaxyProfile *galaxy_settings_profile(GalaxySettings *settings, const char *name);
GalaxyProfile *galaxy_settings_add_profile(GalaxySettings *settings, const char *name);
void galaxy_settings_remove_profile(GalaxySettings *settings, const char *name);
