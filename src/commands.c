#include "app.h"
#include "config.h"
#include <glib/gi18n.h>
#include <string.h>

char **galaxy_prepare_arguments(int argc, char **argv, GError **error)
{
    char **copy = g_strdupv(argv);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(copy[i], "--")) break;
        if (!strcmp(copy[i], "-e") || !strcmp(copy[i], "--execute")) {
            if (i + 1 == argc) {
                g_set_error_literal(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                    _("-e requires a command and its arguments."));
                g_strfreev(copy);
                return NULL;
            }
            g_free(copy[i]); copy[i] = g_strdup("--");
            break;
        }
        /* Values may legitimately be named -e. */
        if ((!strcmp(copy[i], "--title") || !strcmp(copy[i], "-T") || !strcmp(copy[i], "--profile") ||
             !strcmp(copy[i], "--working-directory")) && i + 1 < argc) i++;
    }
    return copy;
}

static int on_local_options(GApplication *application, GVariantDict *options, gpointer data)
{
    (void)application; (void)data;
    if (g_variant_dict_contains(options, "version")) {
        g_print("Galaxy Terminal %s\n", GALAXY_VERSION);
        return 0;
    }
    return -1;
}

static int on_command_line(GApplication *application, GApplicationCommandLine *line, gpointer data)
{
    GalaxyApp *app = data;
    (void)application;
    GVariantDict *options = g_application_command_line_get_options_dict(line);
    const char *profile_name = NULL, *cwd = NULL, *title = NULL;
    g_variant_dict_lookup(options, "profile", "&s", &profile_name);
    g_variant_dict_lookup(options, "title", "&s", &title);
    g_variant_dict_lookup(options, "working-directory", "&s", &cwd);
    GalaxyProfile *profile = galaxy_settings_profile(app->settings,
        profile_name ? profile_name : app->settings->default_profile);
    if (!profile) {
        g_application_command_line_printerr(line, _("Unknown profile: %s\n"),
                                            profile_name ? profile_name : "");
        return 2;
    }
    g_auto(GStrv) command = NULL;
    g_variant_dict_lookup(options, G_OPTION_REMAINING, "^as", &command);
    GalaxyProfile launch = *profile;
    if (command && command[0]) launch.shell = "";
    if (cwd) launch.cwd = "";
    g_autofree char *problem = NULL;
    if (!galaxy_profile_validate_command(&launch, &problem)) {
        g_application_command_line_printerr(line, "%s\n", problem);
        /* A usable preferences window is available even when a profile is broken. */
        galaxy_preferences_show(app, NULL);
        return 2;
    }
    const char *base = g_application_command_line_get_cwd(line);
    if (!base) base = g_get_home_dir();
    const char *start = cwd ? cwd : (*profile->cwd ? profile->cwd : base);
    g_autofree char *absolute = g_path_is_absolute(start) ? g_strdup(start) :
        g_build_filename(base, start, NULL);
    if (!g_file_test(absolute, G_FILE_TEST_IS_DIR)) {
        g_application_command_line_printerr(line, _("Directory does not exist: %s\n"), absolute);
        return 2;
    }
    GalaxyWindow *win = NULL;
    if (g_variant_dict_contains(options, "new-tab") && !g_variant_dict_contains(options, "new-window"))
        win = galaxy_active_window(app);
    if (!win) win = galaxy_window_new(app);
    galaxy_tab_new(win, profile->name, absolute, title, command,
                   (char **)g_application_command_line_get_environ(line));
    gtk_window_present(GTK_WINDOW(win->window));
    return 0;
}

void galaxy_register_command_line(GalaxyApp *app)
{
    static const GOptionEntry entries[] = {
        {"new-tab", 0, 0, G_OPTION_ARG_NONE, NULL, N_("Open a tab in the active terminal window"), NULL},
        {"new-window", 0, 0, G_OPTION_ARG_NONE, NULL, N_("Open a new window"), NULL},
        {"working-directory", 0, 0, G_OPTION_ARG_STRING, NULL, N_("Initial directory"), N_("DIR")},
        {"profile", 0, 0, G_OPTION_ARG_STRING, NULL, N_("Profile name"), N_("NAME")},
        {"title", 'T', 0, G_OPTION_ARG_STRING, NULL, N_("Tab title"), N_("TITLE")},
        {"version", 0, 0, G_OPTION_ARG_NONE, NULL, N_("Show version"), NULL},
        {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, NULL, N_("Command after -- or -e"), N_("COMMAND")},
        {NULL}
    };
    g_application_add_main_option_entries(G_APPLICATION(app->application), entries);
    g_application_set_option_context_summary(G_APPLICATION(app->application),
        _("A GTK 3 and XApp terminal for Wayland. Use -- COMMAND or -e COMMAND to run a program."));
    g_signal_connect(app->application, "handle-local-options", G_CALLBACK(on_local_options), app);
    g_signal_connect(app->application, "command-line", G_CALLBACK(on_command_line), app);
}
