#include "app.h"
#include "config.h"
#include <glib/gi18n.h>
#include <locale.h>

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    g_autoptr(GError) error = NULL;
    g_auto(GStrv) arguments = galaxy_prepare_arguments(argc, argv, &error);
    if (!arguments) { g_printerr("%s\n", error->message); return 2; }
    gdk_set_allowed_backends("wayland");
    GalaxyApp app = {0};
    galaxy_app_init(&app);
    int result = g_application_run(G_APPLICATION(app.application),
                                   g_strv_length(arguments), arguments);
    galaxy_app_clear(&app);
    return result;
}
