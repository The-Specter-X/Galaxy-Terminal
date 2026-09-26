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
    char **environment;
    gulong theme_handler;
} GalaxyApp;

struct _GalaxyWindow {
    GalaxyApp *app;
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *search_revealer;
    GtkWidget *search_entry;
    GtkWidget *search_status;
    GtkWidget *case_button;
    GtkWidget *profiles_menu;
    GtkWidget *settings_error;
    GtkAccelGroup *accelerators;
    guint search_source;
    gboolean closing;
    gboolean close_requested;
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
    char *appearance_state;
    char *behavior_state;
    char *search_state;
    double font_scale;
    GPid pid;
    GCancellable *spawn_cancel;
    gboolean shell_session;
    gboolean closing;
    gboolean close_requested;
};

void galaxy_app_init(GalaxyApp *app);
void galaxy_app_clear(GalaxyApp *app);
void galaxy_app_refresh(GalaxySettings *settings, guint changes, gpointer data);
GalaxyWindow *galaxy_window_new(GalaxyApp *app);
GalaxyWindow *galaxy_active_window(GalaxyApp *app);
void galaxy_window_refresh(GalaxyWindow *win, guint changes);
void galaxy_window_update_tabs(GalaxyWindow *win);
void galaxy_window_show_search(GalaxyWindow *win);
void galaxy_window_search(GalaxyWindow *win, gboolean forward);
void galaxy_window_request_close(GalaxyWindow *win);
void galaxy_window_action(GalaxyWindow *win, GalaxyAction action);
void galaxy_window_install_shortcuts(GalaxyWindow *win);
GalaxyTab *galaxy_current_tab(GalaxyWindow *win);
GalaxyTab *galaxy_tab_new(GalaxyWindow *win, const char *profile_name,
    const char *cwd, const char *title, char **command, char **environment);
void galaxy_tab_destroy(GalaxyTab *tab);
void galaxy_tab_request_close(GalaxyTab *tab);
void galaxy_tab_rename(GalaxyTab *tab);
gboolean galaxy_tab_busy(GalaxyTab *tab);
char *galaxy_tab_directory(GalaxyTab *tab);
char *galaxy_local_directory_uri(const char *uri);
void galaxy_tab_update_title(GalaxyTab *tab);
void galaxy_tab_apply(GalaxyTab *tab, guint changes);
gboolean galaxy_desktop_dark(void);
void galaxy_terminal_apply_profile(VteTerminal *terminal, GalaxyProfile *profile);
void galaxy_install_css(void);
void galaxy_preferences_show(GalaxyApp *app, GtkWindow *parent);
void galaxy_preferences_refresh(GalaxyApp *app, guint changes);
void galaxy_register_command_line(GalaxyApp *app);
char **galaxy_prepare_arguments(int argc, char **argv, GError **error);
