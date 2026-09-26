#include "settings.h"
#include <glib/gstdio.h>

static char *directory;
static char *path;
static void clean(void) { g_unlink(path); }
static void write_config(const char *text)
{
    g_autofree char *parent = g_path_get_dirname(path);
    g_mkdir_with_parents(parent, 0700);
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
}
static void test_defaults(void)
{
    clean();
    GalaxySettings *s = galaxy_settings_new();
    g_assert_cmpint(s->scrollback, ==, 10000);
    g_assert_false(s->auto_copy);
    g_assert_false(s->show_tabs);
    g_assert_cmpstr(galaxy_settings_profile(s, "Default")->palette, ==, "Dark");
    galaxy_settings_free(s);
}
static void test_round_trip(void)
{
    clean();
    GalaxySettings *s = galaxy_settings_new();
    GalaxyProfile *p = galaxy_settings_add_profile(s, "Work");
    g_assert_nonnull(p);
    g_free(p->palette); p->palette = g_strdup("Custom");
    p->opacity = 0.62;
    g_free(p->ansi[3]); p->ansi[3] = g_strdup("#123456");
    p->cursor_shape = 2; p->scroll_on_output = TRUE;
    s->auto_copy = TRUE; s->scrollback = 22000;
    g_free(s->shortcuts[ACT_ZOOM_IN]); s->shortcuts[ACT_ZOOM_IN] = g_strdup("");
    galaxy_settings_changed(s, GALAXY_CHANGE_ALL);
    g_assert_true(galaxy_settings_flush(s));
    galaxy_settings_free(s);
    s = galaxy_settings_new();
    p = galaxy_settings_profile(s, "Work");
    g_assert_nonnull(p);
    g_assert_cmpstr(p->palette, ==, "Custom");
    g_assert_cmpfloat(p->opacity, ==, 0.62);
    g_assert_cmpstr(p->ansi[3], ==, "#123456");
    g_assert_cmpint(p->cursor_shape, ==, 2);
    g_assert_true(p->scroll_on_output);
    g_assert_cmpint(s->scrollback, ==, 22000);
    g_assert_true(s->auto_copy);
    g_assert_cmpstr(s->shortcuts[ACT_ZOOM_IN], ==, "");
    GalaxyProfile *copy = galaxy_settings_duplicate_profile(s, "Work", "Copy");
    g_assert_nonnull(copy); g_assert_true(copy->font != p->font);
    g_assert_cmpfloat(copy->opacity, ==, p->opacity);
    g_assert_true(galaxy_settings_rename_profile(s, p->name, "Renamed"));
    g_assert_false(galaxy_settings_rename_profile(s, "Renamed", "Default"));
    galaxy_settings_remove_profile(s, p->name); /* Aliased model-owned name. */
    g_assert_null(galaxy_settings_profile(s, "Renamed"));
    galaxy_profile_reset(copy);
    g_assert_cmpfloat(copy->opacity, ==, 1.0);
    galaxy_settings_free(s);
}
static void test_malformed(void)
{
    write_config("[broken\n");
    GalaxySettings *s = galaxy_settings_new();
    g_assert_nonnull(s->error);
    g_assert_nonnull(galaxy_settings_profile(s, s->default_profile));
    g_assert_cmpint(s->scrollback, ==, 10000);
    g_assert_false(s->dirty);
    galaxy_settings_free(s);
    clean(); s = galaxy_settings_new();
    s->scrollback = 2345;
    galaxy_settings_changed(s, GALAXY_CHANGE_BEHAVIOR);
    g_assert_true(galaxy_settings_flush(s));
    write_config("[also broken\n");
    g_assert_false(galaxy_settings_reload(s));
    g_assert_cmpint(s->scrollback, ==, 2345);
    g_assert_nonnull(s->error);
    galaxy_settings_free(s);
}
static void test_invalid_values(void)
{
    write_config("[General]\nScrollbackLines=bad\nConfirmClose=perhaps\nAlwaysShowTabs=bad\n"
        "[Profile Default]\nOpacity=nan\nFont=Monospace 999\nCursorShape=999\nForeground=notacolor\nPalette=bogus\n"
        "[Shortcuts]\nNewTab=<Control><Shift>T\nNewWindow=<Control><Shift>t\nCopy=unassignedGarbage\n");
    GalaxySettings *s = galaxy_settings_new();
    GalaxyProfile *p = galaxy_settings_profile(s, "Default");
    g_assert_cmpint(s->scrollback, ==, 10000);
    g_assert_true(s->confirm_close); g_assert_false(s->show_tabs);
    g_assert_cmpfloat(p->opacity, ==, 1.0);
    g_assert_cmpint(p->cursor_shape, ==, 0);
    g_assert_cmpstr(p->font, ==, "Monospace 11");
    g_assert_cmpstr(p->palette, ==, "Dark");
    g_assert_cmpstr(p->foreground, ==, "#ebedf4");
    g_assert_cmpstr(s->shortcuts[ACT_NEW_WINDOW], ==, "");
    g_assert_nonnull(s->error);
    galaxy_settings_free(s);
}
static void test_failed_save(void)
{
    clean(); GalaxySettings *s = galaxy_settings_new();
    g_free(s->path); s->path = g_strdup(directory); /* A directory cannot be replaced by a file. */
    galaxy_settings_changed(s, GALAXY_CHANGE_BEHAVIOR);
    g_assert_false(galaxy_settings_flush(s));
    g_assert_true(s->dirty); g_assert_nonnull(s->error);
    g_free(s->path); s->path = g_strdup(path);
    g_assert_true(galaxy_settings_flush(s));
    g_assert_false(s->dirty); g_assert_null(s->error);
    galaxy_settings_free(s);
}
static void test_debounce_reload(void)
{
    clean(); GalaxySettings *s = galaxy_settings_new();
    for (int i = 0; i < 20; i++) {
        s->scrollback = 100 + i;
        galaxy_settings_changed(s, GALAXY_CHANGE_BEHAVIOR);
    }
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    gint64 until = g_get_monotonic_time() + 600 * 1000;
    while (g_get_monotonic_time() < until) { while (g_main_context_iteration(NULL, FALSE)) {} g_usleep(1000); }
    g_assert_false(s->dirty);
    s->scrollback = 555;
    galaxy_settings_changed(s, GALAXY_CHANGE_BEHAVIOR);
    write_config("[General]\nScrollbackLines=777\n");
    g_assert_true(galaxy_settings_reload(s));
    g_assert_cmpint(s->scrollback, ==, 777);
    g_assert_false(s->dirty); g_assert_cmpuint(s->save_source, ==, 0);
    galaxy_settings_free(s);
}
static void test_shortcuts(void)
{
    guint key; GdkModifierType mods;
    g_assert_true(galaxy_shortcut_parse("<Control><Shift>T", &key, &mods));
    g_assert_cmpuint(key, ==, GDK_KEY_t);
    g_assert_cmpuint(mods, ==, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
    g_assert_true(galaxy_shortcut_parse("<Ctrl><Shift>plus", &key, &mods));
    g_assert_cmpuint(key, ==, GDK_KEY_plus); g_assert_cmpuint(mods, ==, GDK_CONTROL_MASK);
    g_assert_false(galaxy_shortcut_parse("t", &key, &mods));
    g_assert_false(galaxy_shortcut_parse("<Unknown>t", &key, &mods));
    g_assert_true(galaxy_shortcut_parse("", &key, &mods));
    g_assert_cmpuint(key, ==, 0);
}
int main(int argc, char **argv)
{
    directory = g_dir_make_tmp("galaxy-settings-XXXXXX", NULL);
    g_assert_nonnull(directory);
    g_setenv("XDG_CONFIG_HOME", directory, TRUE);
    path = g_build_filename(directory, "galaxy-terminal", "settings.ini", NULL);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/settings/defaults", test_defaults);
    g_test_add_func("/settings/round-trip", test_round_trip);
    g_test_add_func("/settings/malformed", test_malformed);
    g_test_add_func("/settings/invalid-values", test_invalid_values);
    g_test_add_func("/settings/failed-save", test_failed_save);
    g_test_add_func("/settings/debounce-reload", test_debounce_reload);
    g_test_add_func("/settings/shortcuts", test_shortcuts);
    int result = g_test_run();
    clean();
    g_autofree char *subdir = g_path_get_dirname(path);
    g_rmdir(subdir); g_rmdir(directory); g_free(directory); g_free(path);
    return result;
}
