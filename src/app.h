#pragma once

#include "settings.h"
#include <vte/vte.h>

typedef struct _GalaxyWindow GalaxyWindow;
typedef struct _GalaxyTab GalaxyTab;

typedef struct {
    GtkApplication *application;
    GalaxySettings *settings;
    GList *windows;
    GtkWidget *preferences;
    GObject *dark_mode_manager;
} GalaxyApp;

struct _GalaxyWindow {
    GalaxyApp *app;
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *search_revealer;
    GtkWidget *search_entry;
    GtkWidget *case_button;
    GtkWidget *profiles_menu;
};

struct _GalaxyTab {
    GalaxyWindow *owner;
    GtkWidget *page;
    VteTerminal *terminal;
    GtkWidget *scrolled;
    GtkWidget *label;
    char *profile_name;
    char *custom_title;
    char *initial_cwd;
    double font_scale;
    GPid pid;
    GCancellable *spawn_cancel;
    gboolean shell_session;
    gboolean closing;
};

GalaxyWindow *galaxy_window_new(GalaxyApp *app);
GalaxyTab *galaxy_tab_new(GalaxyWindow *win, const char *profile_name,
                          const char *cwd, const char *title, char **command);
GalaxyTab *galaxy_current_tab(GalaxyWindow *win);
void galaxy_window_show_search(GalaxyWindow *win);
void galaxy_app_refresh(gpointer app);
void galaxy_preferences_show(GalaxyApp *app, GtkWindow *parent);
