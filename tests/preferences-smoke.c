#include "app.h"

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    GalaxyApp app = {0};
    app.application = gtk_application_new("org.thespecterx.GalaxyTerminal.Test",
                                           G_APPLICATION_NON_UNIQUE);
    g_autoptr(GError) error = NULL;
    if (!g_application_register(G_APPLICATION(app.application), NULL, &error)) {
        g_printerr("Could not register test application: %s\n", error->message);
        g_object_unref(app.application);
        return 1;
    }
    app.settings = galaxy_settings_new();
    galaxy_preferences_show(&app, NULL);
    if (!app.preferences) {
        galaxy_settings_free(app.settings);
        g_object_unref(app.application);
        return 1;
    }
    gtk_widget_destroy(app.preferences);
    galaxy_settings_free(app.settings);
    g_object_unref(app.application);
    return 0;
}
