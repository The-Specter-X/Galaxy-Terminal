#include "settings.h"
#include <glib/gstdio.h>

static char *directory;

static void test_defaults(void)
{
    GalaxySettings *s = galaxy_settings_new();
    g_assert_cmpint(s->scrollback, ==, 10000);
    g_assert_false(s->auto_copy);
    g_assert_nonnull(galaxy_settings_profile(s, "Default"));
    galaxy_settings_free(s);
}

static void test_round_trip(void)
{
    GalaxySettings *s = galaxy_settings_new();
    GalaxyProfile *p = galaxy_settings_add_profile(s, "Work");
    g_assert_nonnull(p);
    g_free(p->palette);
    p->palette = g_strdup("Custom");
    p->opacity = 0.62;
    s->auto_copy = TRUE;
    s->scrollback = 22000;
    g_free(s->shortcuts[ACT_ZOOM_IN]);
    s->shortcuts[ACT_ZOOM_IN] = g_strdup("");
    galaxy_settings_save(s);
    galaxy_settings_free(s);

    s = galaxy_settings_new();
    p = galaxy_settings_profile(s, "Work");
    g_assert_nonnull(p);
    g_assert_cmpstr(p->palette, ==, "Custom");
    g_assert_cmpfloat(p->opacity, ==, 0.62);
    g_assert_cmpint(s->scrollback, ==, 22000);
    g_assert_true(s->auto_copy);
    g_assert_cmpstr(s->shortcuts[ACT_ZOOM_IN], ==, "");
    galaxy_settings_remove_profile(s, "Work");
    g_assert_null(galaxy_settings_profile(s, "Work"));
    galaxy_settings_free(s);
}

int main(int argc, char **argv)
{
    directory = g_dir_make_tmp("galaxy-terminal-tests-XXXXXX", NULL);
    g_assert_nonnull(directory);
    g_setenv("XDG_CONFIG_HOME", directory, TRUE);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/settings/defaults", test_defaults);
    g_test_add_func("/settings/round-trip", test_round_trip);
    int result = g_test_run();
    g_autofree char *settings = g_build_filename(directory, "galaxy-terminal", "settings.ini", NULL);
    g_autofree char *subdir = g_build_filename(directory, "galaxy-terminal", NULL);
    g_unlink(settings);
    g_rmdir(subdir);
    g_rmdir(directory);
    g_free(directory);
    return result;
}

