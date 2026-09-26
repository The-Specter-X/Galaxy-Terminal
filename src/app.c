#include "app.h"
#include "config.h"
#include <glib/gi18n.h>
#include <libxapp/xapp-dark-mode-manager.h>
#include <locale.h>

static void on_theme_changed(GtkSettings *settings, GParamSpec *pspec, gpointer data)
{
    (void)settings; (void)pspec;
    GalaxyApp *app = data;
    galaxy_app_refresh(app->settings, GALAXY_CHANGE_COLORS, app);
}

static void on_startup(GApplication *application, gpointer data)
{
    (void)application;
    GalaxyApp *app = data;
    galaxy_install_css();
    app->dark_mode_manager = G_OBJECT(xapp_dark_mode_manager_new(FALSE));
    app->theme_handler = g_signal_connect(gtk_settings_get_default(),
        "notify::gtk-application-prefer-dark-theme", G_CALLBACK(on_theme_changed), app);
}

void galaxy_app_init(GalaxyApp *app)
{
    app->application = gtk_application_new("org.thespecterx.GalaxyTerminal",
        G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_SEND_ENVIRONMENT);
    app->environment = g_get_environ();
    app->settings = galaxy_settings_new();
    galaxy_settings_set_changed(app->settings, galaxy_app_refresh, app);
    g_signal_connect(app->application, "startup", G_CALLBACK(on_startup), app);
    galaxy_register_command_line(app);
}

void galaxy_app_refresh(GalaxySettings *settings, guint changes, gpointer data)
{
    GalaxyApp *app = data;
    (void)settings;
    for (GList *node = app->windows; node; node = node->next)
        galaxy_window_refresh(node->data, changes);
    galaxy_preferences_refresh(app, changes);
}

GalaxyWindow *galaxy_active_window(GalaxyApp *app)
{
    GtkWindow *active = gtk_application_get_active_window(app->application);
    for (GList *node = app->windows; node; node = node->next) {
        GalaxyWindow *win = node->data;
        if (GTK_WINDOW(win->window) == active) return win;
    }
    return app->windows ? app->windows->data : NULL;
}

void galaxy_app_clear(GalaxyApp *app)
{
    if (app->theme_handler)
        g_signal_handler_disconnect(gtk_settings_get_default(), app->theme_handler);
    g_clear_object(&app->dark_mode_manager);
    galaxy_settings_set_changed(app->settings, NULL, NULL);
    if (app->preferences) gtk_widget_destroy(app->preferences);
    while (app->windows) gtk_widget_destroy(((GalaxyWindow *)app->windows->data)->window);
    galaxy_settings_free(app->settings);
    g_strfreev(app->environment);
    g_clear_object(&app->application);
}
