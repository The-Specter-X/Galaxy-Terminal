#include "app.h"
#include <glib/gstdio.h>
#include <gdk/gdkwayland.h>
#include <signal.h>

static GalaxyApp app;
static void spin(int milliseconds)
{
    gint64 end = g_get_monotonic_time() + milliseconds * 1000;
    do { while (g_main_context_iteration(NULL, FALSE)) {} g_usleep(1000); }
    while (g_get_monotonic_time() < end);
}
static GalaxyTab *sleeper(GalaxyWindow *win, const char *title)
{
    char *argv[] = {"/bin/sleep", "30", NULL};
    return galaxy_tab_new(win, "Default", "/tmp", title, argv, NULL);
}
static void wait_spawn(GalaxyTab *tab)
{
    for (int i = 0; i < 100 && tab->spawn_cancel; i++) spin(10);
    g_assert_cmpint(tab->pid, >, 0);
}
static GtkWidget *find_widget(GtkWidget *root, GType type, const char *text)
{
    if (g_type_is_a(G_OBJECT_TYPE(root), type) && (!text ||
        (!g_strcmp0(gtk_widget_get_name(root), text) ||
         (GTK_IS_BUTTON(root) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(root)), text))))) return root;
    if (GTK_IS_CONTAINER(root)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(root));
        for (GList *node = children; node; node = node->next) {
            GtkWidget *found = find_widget(node->data, type, text);
            if (found) { g_list_free(children); return found; }
        }
        g_list_free(children);
    }
    return NULL;
}
static GtkWidget *dialog_named(const char *title)
{
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *found = NULL;
    for (GList *node = windows; node; node = node->next)
        if (GTK_IS_DIALOG(node->data) && (!title || !g_strcmp0(gtk_window_get_title(node->data), title))) {
            found = node->data; break;
        }
    g_list_free(windows);
    return found;
}
static gboolean activate_physical(GalaxyWindow *win, guint symbol, GdkModifierType state)
{
    GdkKeymap *map = gdk_keymap_get_for_display(gtk_widget_get_display(win->window));
    GdkKeymapKey *entries = NULL;
    int count = 0;
    g_assert_true(gdk_keymap_get_entries_for_keyval(map, symbol, &entries, &count));
    g_assert_cmpint(count, >, 0);
    GdkEventKey event = {0};
    event.type = GDK_KEY_PRESS;
    event.window = gtk_widget_get_window(win->window);
    event.hardware_keycode = entries[0].keycode;
    event.group = entries[0].group;
    event.state = state;
    event.keyval = symbol;
    g_free(entries);
    return gtk_window_activate_key(GTK_WINDOW(win->window), &event);
}

static void test_tabs_search_shortcuts(void)
{
    GalaxyWindow *win = galaxy_window_new(&app);
    g_assert_true(GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(win->window)));
    GalaxyTab *a = sleeper(win, "First"), *b;
    wait_spawn(a);
    g_assert_false(gtk_notebook_get_show_tabs(GTK_NOTEBOOK(win->notebook)));
    b = sleeper(win, "Second"); wait_spawn(b);
    g_assert_true(activate_physical(win, GDK_KEY_T, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
    g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(win->notebook)), ==, 3);
    galaxy_tab_destroy(galaxy_current_tab(win));
    g_assert_true(activate_physical(win, GDK_KEY_plus, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
    g_assert_cmpfloat(b->font_scale, >, 1.0);
    galaxy_window_action(win, ACT_ZOOM_RESET);
    g_assert_true(gtk_notebook_get_show_tabs(GTK_NOTEBOOK(win->notebook)));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(win->notebook), 0);
    g_assert_true(galaxy_current_tab(win) == a);
    g_assert_true(g_str_has_prefix(gtk_window_get_title(GTK_WINDOW(win->window)), "First"));
    g_assert_true(gtk_accel_groups_activate(G_OBJECT(win->window), GDK_KEY_Page_Down, GDK_CONTROL_MASK));
    g_assert_true(galaxy_current_tab(win) == b);
    g_assert_true(g_str_has_prefix(gtk_window_get_title(GTK_WINDOW(win->window)), "Second"));
    g_assert_true(gtk_accel_groups_activate(G_OBJECT(win->window), GDK_KEY_plus, GDK_CONTROL_MASK));
    g_assert_cmpfloat(b->font_scale, >, 1.0);
    g_assert_true(gtk_accel_groups_activate(G_OBJECT(win->window), GDK_KEY_0, GDK_CONTROL_MASK));
    g_assert_cmpfloat(b->font_scale, ==, 1.0);
    g_assert_true(gtk_accel_groups_activate(G_OBJECT(win->window), GDK_KEY_f, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
    g_assert_true(gtk_revealer_get_reveal_child(GTK_REVEALER(win->search_revealer)));
    vte_terminal_feed(b->terminal, "searchable unicorn\r\nsecond unicorn\r\n", -1);
    spin(100);
    gtk_entry_set_text(GTK_ENTRY(win->search_entry), "unicorn");
    galaxy_window_search(win, TRUE);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(win->search_status)), ==, "Match found");
    galaxy_window_search(win, TRUE);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(win->search_status)), ==, "Match found");
    gtk_notebook_set_current_page(GTK_NOTEBOOK(win->notebook), 0);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(win->search_status)), ==, "No matches");
    gtk_notebook_reorder_child(GTK_NOTEBOOK(win->notebook), b->page, 0);
    g_assert_true(gtk_notebook_get_nth_page(GTK_NOTEBOOK(win->notebook), 0) == b->page);
    gtk_widget_destroy(win->window); spin(30);
}
static void test_dialog_lifetime(void)
{
    GalaxyWindow *win = galaxy_window_new(&app);
    sleeper(win, "Keeper");
    GalaxyTab *tab = sleeper(win, "Exiting"); wait_spawn(tab);
    galaxy_tab_request_close(tab);
    galaxy_tab_rename(tab);
    g_assert_nonnull(dialog_named("Tab title"));
    GdkEvent *event = gdk_event_new(GDK_BUTTON_PRESS);
    event->button.window = g_object_ref(gtk_widget_get_window(GTK_WIDGET(tab->terminal)));
    event->button.send_event = TRUE;
    event->button.time = GDK_CURRENT_TIME;
    event->button.button = 3;
    event->button.x = 20; event->button.y = 20;
    gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));
    gtk_main_do_event(event);
    gdk_event_free(event);
    g_assert_nonnull(vte_terminal_get_context_menu(tab->terminal));
    GPid child = tab->pid;
    kill(child, SIGTERM);
    for (int i = 0; i < 100 && gtk_notebook_get_n_pages(GTK_NOTEBOOK(win->notebook)) > 1; i++) spin(10);
    g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(win->notebook)), ==, 1);
    g_assert_null(dialog_named("Tab title"));
    galaxy_window_request_close(win);
    g_assert_true(win->close_requested);
    GtkWidget *dialog = dialog_named(NULL);
    g_assert_nonnull(dialog);
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    g_assert_false(win->close_requested);
    gtk_widget_destroy(win->window);
    /* Closing a tab before spawn completion exercises the weak-reference path. */
    win = galaxy_window_new(&app);
    tab = sleeper(win, "Pending");
    galaxy_tab_destroy(tab);
    spin(100);
}
static void test_preferences(void)
{
    GalaxyWindow *win = galaxy_window_new(&app); sleeper(win, "Preferences");
    galaxy_preferences_show(&app, GTK_WINDOW(win->window));
    GtkWidget *prefs = app.preferences;
    const char *buttons[] = {"Add", "Rename", "Duplicate"};
    for (guint i = 0; i < G_N_ELEMENTS(buttons); i++) {
        GtkWidget *button = find_widget(prefs, GTK_TYPE_BUTTON, buttons[i]);
        g_assert_nonnull(button); gtk_button_clicked(GTK_BUTTON(button));
        GtkWidget *dialog = dialog_named("Profile name"); g_assert_nonnull(dialog);
        GtkWidget *entry = find_widget(dialog, GTK_TYPE_ENTRY, NULL); g_assert_nonnull(entry);
        gtk_entry_set_text(GTK_ENTRY(entry), "[invalid]");
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        g_assert_nonnull(dialog_named("Profile name"));
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    }
    GtkWidget *duplicate = find_widget(prefs, GTK_TYPE_BUTTON, "Duplicate");
    gtk_button_clicked(GTK_BUTTON(duplicate));
    GtkWidget *dialog = dialog_named("Profile name");
    gtk_entry_set_text(GTK_ENTRY(find_widget(dialog, GTK_TYPE_ENTRY, NULL)), "Preview copy");
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    g_assert_nonnull(galaxy_settings_profile(app.settings, "Preview copy"));
    GalaxyTab *tab = galaxy_current_tab(win);
    g_free(tab->profile_name); tab->profile_name = g_strdup("Preview copy");
    gtk_button_clicked(GTK_BUTTON(find_widget(prefs, GTK_TYPE_BUTTON, "Remove")));
    g_assert_cmpstr(tab->profile_name, ==, app.settings->default_profile);
    GtkWidget *spin_button = find_widget(prefs, GTK_TYPE_SPIN_BUTTON, NULL);
    g_assert_nonnull(spin_button);
    g_assert_true(galaxy_settings_flush(app.settings));
    g_assert_true(g_file_set_contents(app.settings->path, "[General]\nScrollbackLines=4321\n", -1, NULL));
    g_assert_true(galaxy_settings_reload(app.settings));
    g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_button)), ==, 4321);
    g_assert_false(app.settings->dirty); /* Refresh must not emit a save. */
    gtk_button_clicked(GTK_BUTTON(find_widget(prefs, GTK_TYPE_BUTTON, "Restore general defaults")));
    g_assert_cmpint(app.settings->scrollback, ==, 10000);
    /* Capture must normalize uppercase keys, and cancel cleanly on parent destruction. */
    GtkWidget *shortcut = find_widget(prefs, GTK_TYPE_BUTTON, "NewTab");
    g_assert_nonnull(shortcut);
    gtk_button_clicked(GTK_BUTTON(shortcut));
    dialog = dialog_named("Set shortcut"); g_assert_nonnull(dialog);
    GdkEventKey event = {0}; event.type = GDK_KEY_PRESS; event.keyval = GDK_KEY_K;
    event.state = GDK_CONTROL_MASK | GDK_SHIFT_MASK;
    gboolean handled = FALSE;
    g_signal_emit_by_name(dialog, "key-press-event", &event, &handled);
    g_assert_true(handled);
    guint key; GdkModifierType mods;
    galaxy_shortcut_parse(app.settings->shortcuts[ACT_NEW_TAB], &key, &mods);
    g_assert_cmpuint(key, ==, GDK_KEY_k);
    g_assert_cmpuint(mods, ==, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
    gtk_button_clicked(GTK_BUTTON(find_widget(prefs, GTK_TYPE_BUTTON, "Restore all shortcut defaults")));
    gtk_button_clicked(GTK_BUTTON(duplicate));
    gtk_widget_destroy(prefs); g_assert_null(app.preferences);
    gtk_widget_destroy(win->window); spin(30);
}
static void test_opacity(void)
{
    GalaxyWindow *win = galaxy_window_new(&app);
    GalaxyTab *tab = sleeper(win, "Opacity"); wait_spawn(tab);
    GalaxyProfile *profile = galaxy_settings_profile(app.settings, "Default");
    profile->opacity = 0.5;
    galaxy_settings_changed(app.settings, GALAXY_CHANGE_COLORS);
    spin(200);
    int width = gtk_widget_get_allocated_width(win->window);
    int height = gtk_widget_get_allocated_height(win->window);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);
    gtk_widget_draw(win->window, cr);
    cairo_destroy(cr); cairo_surface_flush(surface);
    int x = 0, y = 0;
    g_assert_true(gtk_widget_translate_coordinates(GTK_WIDGET(tab->terminal), win->window,
        gtk_widget_get_allocated_width(GTK_WIDGET(tab->terminal)) - 12,
        gtk_widget_get_allocated_height(GTK_WIDGET(tab->terminal)) - 12, &x, &y));
    g_assert_cmpint(x, >=, 0); g_assert_cmpint(x, <, width);
    g_assert_cmpint(y, >=, 0); g_assert_cmpint(y, <, height);
    guint32 *pixels = (guint32 *)(cairo_image_surface_get_data(surface) +
                                y * cairo_image_surface_get_stride(surface));
    guint alpha = pixels[x] >> 24;
    g_test_message("Terminal background alpha: %u (expected approximately 128)", alpha);
    cairo_surface_write_to_png(surface, "build/meson-logs/opacity-preview.png");
    cairo_surface_destroy(surface);
    g_assert_cmpuint(alpha, >, 110); g_assert_cmpuint(alpha, <, 145);
    profile->opacity = 1.0;
    galaxy_settings_changed(app.settings, GALAXY_CHANGE_COLORS);
    gtk_widget_destroy(win->window); spin(30);
}

static void test_uri_and_arguments(void)
{
    g_autofree char *local = galaxy_local_directory_uri("file://localhost/tmp");
    g_assert_cmpstr(local, ==, "/tmp");
    g_assert_null(galaxy_local_directory_uri("file://remote.invalid/tmp"));
    g_assert_null(galaxy_local_directory_uri("https://localhost/tmp"));
    char *args[] = {"galaxy-terminal", "--title", "-e", "-e", "echo", "--help", NULL};
    g_auto(GStrv) copy = galaxy_prepare_arguments(6, args, NULL);
    g_assert_cmpstr(copy[2], ==, "-e"); g_assert_cmpstr(copy[3], ==, "--");
    g_assert_cmpstr(copy[5], ==, "--help");
    char *bad[] = {"galaxy-terminal", "-e", NULL};
    g_autoptr(GError) error = NULL;
    g_assert_null(galaxy_prepare_arguments(2, bad, &error)); g_assert_nonnull(error);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    gdk_set_allowed_backends("wayland");
    gtk_init(&argc, &argv);
    galaxy_app_init(&app);
    g_autoptr(GError) error = NULL;
    g_assert_true(g_application_register(G_APPLICATION(app.application), NULL, &error));
    g_assert_no_error(error);
    g_test_add_func("/ui/tabs-search-shortcuts", test_tabs_search_shortcuts);
    g_test_add_func("/ui/dialog-lifetime", test_dialog_lifetime);
    g_test_add_func("/ui/preferences-reload", test_preferences);
    g_test_add_func("/ui/local-uri-arguments", test_uri_and_arguments);
    g_test_add_func("/ui/opacity-backing", test_opacity);
    int result = g_test_run();
    galaxy_app_clear(&app); spin(50);
    return result;
}
