#include "app.h"

#include <libxapp/xapp-dark-mode-manager.h>
#include <libxapp/xapp-gtk-window.h>
#include <vte/vte.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <pwd.h>

/* A tab owns a shell and VTE widget; the notebook page owns the tab record. */
static void tab_free(gpointer data)
{
    GalaxyTab *tab = data;
    g_free(tab->profile_name);
    g_free(tab->custom_title);
    g_free(tab->initial_cwd);
    g_free(tab);
}

GalaxyTab *galaxy_current_tab(GalaxyWindow *win)
{
    GtkNotebook *book = GTK_NOTEBOOK(win->notebook);
    int index = gtk_notebook_get_current_page(book);
    GtkWidget *page = gtk_notebook_get_nth_page(book, index);
    return page ? g_object_get_data(G_OBJECT(page), "galaxy-tab") : NULL;
}

static char *tab_directory(GalaxyTab *tab)
{
    if (!tab) return g_strdup(g_get_home_dir());
    const char *uri = vte_terminal_get_current_directory_uri(tab->terminal);
    char *cwd = uri ? g_filename_from_uri(uri, NULL, NULL) : NULL;
    if (!cwd || !g_file_test(cwd, G_FILE_TEST_IS_DIR)) {
        g_free(cwd);
        cwd = g_strdup(tab->initial_cwd);
    }
    return cwd;
}

static gboolean is_dark(GalaxyApp *app)
{
    gboolean dark = FALSE;
    if (app->settings->follow_dark)
        g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", &dark, NULL);
    return dark;
}

static void apply_palette(GalaxyTab *tab)
{
    GalaxyProfile *p = galaxy_settings_profile(tab->owner->app->settings, tab->profile_name);
    if (!p) p = galaxy_settings_profile(tab->owner->app->settings,
                                       tab->owner->app->settings->default_profile);
    if (!p) return;
    gboolean dark = is_dark(tab->owner->app);
    const char *fg = dark ? "#eeeeee" : "#242424";
    const char *bg = dark ? "#22232b" : "#fafafa";
    static const char *const ansi_dark[16] = {
        "#20232c", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#dcdfe4",
        "#5b616e", "#ff7b86", "#b3dd91", "#f5d492", "#80c2fb", "#dc9cf1", "#7dd3db", "#ffffff"
    };
    static const char *const ansi_light[16] = {
        "#343a45", "#b42134", "#25792e", "#8b6200", "#215fbd", "#90469b", "#007c87", "#d5d8dc",
        "#666d77", "#d32e42", "#3a973e", "#a37b08", "#397be0", "#ac63b2", "#159aa5", "#ffffff"
    };
    if (g_strcmp0(p->palette, "Dark") == 0) {
        fg = "#ebedf4"; bg = "#191b24"; dark = TRUE;
    } else if (g_strcmp0(p->palette, "Light") == 0) {
        fg = "#242424"; bg = "#fafafa"; dark = FALSE;
    } else if (g_strcmp0(p->palette, "Custom") == 0) {
        fg = p->foreground; bg = p->background;
    }
    GdkRGBA foreground, background, colors[16];
    if (!gdk_rgba_parse(&foreground, fg)) gdk_rgba_parse(&foreground, "#ebedf4");
    if (!gdk_rgba_parse(&background, bg)) gdk_rgba_parse(&background, "#191b24");
    background.alpha = p->opacity;
    for (int i = 0; i < 16; ++i)
        gdk_rgba_parse(&colors[i], (dark ? ansi_dark : ansi_light)[i]);
    vte_terminal_set_colors(tab->terminal, &foreground, &background, colors, 16);
    vte_terminal_set_color_cursor(tab->terminal, &foreground);
    g_autoptr(PangoFontDescription) font = pango_font_description_from_string(p->font);
    vte_terminal_set_font(tab->terminal, font);
    vte_terminal_set_font_scale(tab->terminal, tab->font_scale);
    GalaxySettings *s = tab->owner->app->settings;
    vte_terminal_set_scrollback_lines(tab->terminal, s->scrollback);
    vte_terminal_set_mouse_autohide(tab->terminal, s->mouse_autohide);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tab->scrolled), GTK_POLICY_NEVER,
        s->show_scrollbar ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER);
}

static void update_title(GalaxyTab *tab)
{
    const char *title = tab->custom_title;
    if (!title || !*title) title = vte_terminal_get_window_title(tab->terminal);
    g_autofree char *cwd = NULL;
    g_autofree char *basename = NULL;
    if (!title || !*title) {
        cwd = tab_directory(tab);
        basename = cwd ? g_path_get_basename(cwd) : g_strdup("Terminal");
        title = basename;
    }
    g_autofree char *short_title = g_utf8_substring(title, 0,
                                                    MIN(g_utf8_strlen(title, -1), 35));
    gtk_label_set_text(GTK_LABEL(tab->label), short_title);
    GalaxyTab *active = galaxy_current_tab(tab->owner);
    if (active == tab) gtk_window_set_title(GTK_WINDOW(tab->owner->window), short_title);
}

static void on_title_changed(VteTerminal *terminal, gpointer user_data)
{
    (void)terminal;
    update_title(user_data);
}

static void on_current_directory_changed(VteTerminal *terminal, gpointer user_data)
{
    (void)terminal;
    update_title(user_data);
}

static gboolean confirm_tab_close(GalaxyTab *tab)
{
    if (!tab || !tab->pid || !tab->owner->app->settings->confirm_close) return TRUE;
    int fd = vte_pty_get_fd(vte_terminal_get_pty(tab->terminal));
    pid_t foreground = fd >= 0 ? tcgetpgrp(fd) : -1;
    if (tab->shell_session && (foreground < 0 || foreground == tab->pid)) return TRUE;
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(tab->owner->window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
        "A command is still running in this terminal.");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
        "Closing the tab will end its terminal session.");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL,
                           "Close tab", GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    gboolean close = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT;
    gtk_widget_destroy(dialog);
    return close;
}

static void close_tab(GalaxyTab *tab)
{
    if (!tab || tab->closing || !confirm_tab_close(tab)) return;
    tab->closing = TRUE;
    GalaxyWindow *win = tab->owner;
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(win->notebook)) == 1) {
        gtk_widget_destroy(win->window);
    } else {
        gtk_notebook_remove_page(GTK_NOTEBOOK(win->notebook),
            gtk_notebook_page_num(GTK_NOTEBOOK(win->notebook), tab->page));
    }
}

static void on_close_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    close_tab(data);
}

static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    GalaxyWindow *win = data;
    (void)widget; (void)event;
    GtkNotebook *book = GTK_NOTEBOOK(win->notebook);
    for (int i = 0; i < gtk_notebook_get_n_pages(book); ++i) {
        GalaxyTab *tab = g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(book, i)),
                                           "galaxy-tab");
        if (!confirm_tab_close(tab)) return TRUE;
    }
    return FALSE;
}

static void on_child_exited(VteTerminal *terminal, int status, gpointer user_data)
{
    GalaxyTab *tab = user_data;
    (void)terminal; (void)status;
    tab->pid = 0;
    if (!tab->closing) close_tab(tab);
}

typedef struct { GWeakRef terminal; } SpawnResult;

static void on_spawn_done(VteTerminal *terminal, GPid pid, GError *error, gpointer data)
{
    SpawnResult *result = data;
    GObject *object = g_weak_ref_get(&result->terminal);
    if (object) {
        GalaxyTab *tab = g_object_get_data(object, "galaxy-tab");
        if (tab && !tab->closing) {
            if (error) {
                g_autofree char *message = g_strdup_printf("\r\nUnable to start terminal: %s\r\n", error->message);
                vte_terminal_feed(terminal, message, -1);
                tab->pid = 0;
            } else {
                tab->pid = pid;
            }
        }
        g_object_unref(object);
    }
    g_weak_ref_clear(&result->terminal);
    g_free(result);
}

static char *preferred_shell(GalaxyProfile *p)
{
    if (p && p->shell && *p->shell) return g_strdup(p->shell);
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_shell && *pw->pw_shell) return g_strdup(pw->pw_shell);
    return g_strdup("/bin/sh");
}

static gboolean on_terminal_mouse_release(GtkWidget *widget, GdkEventButton *event,
                                          gpointer data)
{
    GalaxyTab *tab = data;
    if (event->button == 1 && (event->state & GDK_CONTROL_MASK)) {
        char *link = vte_terminal_hyperlink_check_event(tab->terminal, (GdkEvent *)event);
        if (!link) link = vte_terminal_match_check_event(tab->terminal, (GdkEvent *)event, NULL);
        if (link) {
            g_autoptr(GError) error = NULL;
            if (g_str_has_prefix(link, "https://") || g_str_has_prefix(link, "http://") ||
                g_str_has_prefix(link, "mailto:"))
                if (!gtk_show_uri_on_window(GTK_WINDOW(tab->owner->window), link,
                                            event->time, &error))
                    g_warning("Could not open link: %s", error->message);
            g_free(link);
            return TRUE;
        }
    }
    if (event->button == 1 && tab->owner->app->settings->auto_copy &&
        vte_terminal_get_has_selection(tab->terminal))
        vte_terminal_copy_clipboard_format(tab->terminal, VTE_FORMAT_TEXT);
    (void)widget;
    return FALSE;
}

static void search_update(GalaxyWindow *win)
{
    GalaxyTab *tab = galaxy_current_tab(win);
    if (!tab) return;
    const char *text = gtk_entry_get_text(GTK_ENTRY(win->search_entry));
    if (!*text) {
        vte_terminal_search_set_regex(tab->terminal, NULL, 0);
        return;
    }
    g_autofree char *escaped = g_regex_escape_string(text, -1);
    g_autofree char *pattern = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(win->case_button))
        ? g_strdup(escaped) : g_strconcat("(?i)", escaped, NULL);
    g_autoptr(GError) error = NULL;
    VteRegex *regex = vte_regex_new_for_search(pattern, -1, VTE_REGEX_FLAGS_DEFAULT, &error);
    if (!regex) {
        g_warning("Could not search terminal: %s", error->message);
        return;
    }
    vte_terminal_search_set_regex(tab->terminal, regex, 0);
    vte_terminal_search_set_wrap_around(tab->terminal, TRUE);
    vte_regex_unref(regex);
}

static void on_search_changed(GtkEditable *editable, gpointer data)
{
    (void)editable;
    search_update(data);
}

static void on_search_next(GtkButton *button, gpointer data)
{
    GalaxyTab *tab = galaxy_current_tab(data);
    (void)button;
    if (tab) vte_terminal_search_find_next(tab->terminal);
}

static void on_search_previous(GtkButton *button, gpointer data)
{
    GalaxyTab *tab = galaxy_current_tab(data);
    (void)button;
    if (tab) vte_terminal_search_find_previous(tab->terminal);
}

static void search_hide(GalaxyWindow *win)
{
    gtk_revealer_set_reveal_child(GTK_REVEALER(win->search_revealer), FALSE);
    GalaxyTab *tab = galaxy_current_tab(win);
    if (tab) gtk_widget_grab_focus(GTK_WIDGET(tab->terminal));
}

static void on_search_close(GtkButton *button, gpointer data)
{
    (void)button;
    search_hide(data);
}

static gboolean on_search_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    GalaxyWindow *win = data;
    (void)widget;
    if (event->keyval == GDK_KEY_Escape) {
        search_hide(win);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        GalaxyTab *tab = galaxy_current_tab(win);
        if (tab) {
            if (event->state & GDK_SHIFT_MASK)
                vte_terminal_search_find_previous(tab->terminal);
            else vte_terminal_search_find_next(tab->terminal);
        }
        return TRUE;
    }
    return FALSE;
}

void galaxy_window_show_search(GalaxyWindow *win)
{
    gtk_revealer_set_reveal_child(GTK_REVEALER(win->search_revealer), TRUE);
    gtk_widget_grab_focus(win->search_entry);
    gtk_editable_select_region(GTK_EDITABLE(win->search_entry), 0, -1);
}

static void on_page_changed(GtkNotebook *book, GtkWidget *page, guint number, gpointer data)
{
    GalaxyWindow *win = data;
    (void)book; (void)page; (void)number;
    GalaxyTab *tab = galaxy_current_tab(win);
    if (tab) {
        update_title(tab);
        if (gtk_revealer_get_reveal_child(GTK_REVEALER(win->search_revealer)))
            search_update(win);
        else gtk_widget_grab_focus(GTK_WIDGET(tab->terminal));
    }
}

static void on_plus_clicked(GtkButton *button, gpointer data)
{
    GalaxyWindow *win = data;
    (void)button;
    GalaxyTab *current = galaxy_current_tab(win);
    g_autofree char *cwd = tab_directory(current);
    galaxy_tab_new(win, current ? current->profile_name : NULL, cwd, NULL, NULL);
}

static void on_profile_item(GtkMenuItem *item, gpointer data)
{
    GalaxyWindow *win = data;
    const char *name = g_object_get_data(G_OBJECT(item), "profile-name");
    g_autofree char *cwd = tab_directory(galaxy_current_tab(win));
    galaxy_tab_new(win, name, cwd, NULL, NULL);
}

static void rebuild_profiles(GalaxyWindow *win)
{
    GtkWidget *menu = gtk_menu_new();
    for (guint i = 0; i < win->app->settings->profiles->len; ++i) {
        GalaxyProfile *p = g_ptr_array_index(win->app->settings->profiles, i);
        GtkWidget *item = gtk_menu_item_new_with_label(p->name);
        g_object_set_data_full(G_OBJECT(item), "profile-name", g_strdup(p->name), g_free);
        g_signal_connect(item, "activate", G_CALLBACK(on_profile_item), win);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_widget_show_all(menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(win->profiles_menu), menu);
}

GalaxyTab *galaxy_tab_new(GalaxyWindow *win, const char *profile_name,
                          const char *cwd, const char *title, char **command)
{
    GalaxySettings *s = win->app->settings;
    GalaxyProfile *profile = galaxy_settings_profile(s, profile_name);
    if (!profile) profile = galaxy_settings_profile(s, s->default_profile);
    if (!profile) return NULL;
    GalaxyTab *tab = g_new0(GalaxyTab, 1);
    tab->owner = win;
    tab->profile_name = g_strdup(profile->name);
    tab->custom_title = g_strdup(title);
    tab->initial_cwd = cwd && g_file_test(cwd, G_FILE_TEST_IS_DIR)
        ? g_strdup(cwd) : g_strdup(g_get_home_dir());
    tab->font_scale = 1.0;
    tab->shell_session = !command || !command[0];
    tab->terminal = VTE_TERMINAL(vte_terminal_new());
    vte_terminal_set_enable_sixel(tab->terminal, FALSE);
    vte_terminal_set_allow_hyperlink(tab->terminal, TRUE);
    tab->scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(tab->scrolled), GTK_WIDGET(tab->terminal));
    tab->page = tab->scrolled;
    g_object_set_data_full(G_OBJECT(tab->page), "galaxy-tab", tab, tab_free);
    GtkWidget *label_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    tab->label = gtk_label_new("Terminal");
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(tab->label, 96, -1);
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(close, "Close tab");
    gtk_box_pack_start(GTK_BOX(label_box), tab->label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(label_box), close, FALSE, FALSE, 0);
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), tab);
    int page = gtk_notebook_append_page(GTK_NOTEBOOK(win->notebook), tab->page, label_box);
    gtk_widget_show_all(tab->page);
    gtk_widget_show_all(label_box);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(win->notebook), page);
    apply_palette(tab);
    g_signal_connect(tab->terminal, "window-title-changed", G_CALLBACK(on_title_changed), tab);
    g_signal_connect(tab->terminal, "current-directory-uri-changed",
                     G_CALLBACK(on_current_directory_changed), tab);
    g_signal_connect(tab->terminal, "child-exited", G_CALLBACK(on_child_exited), tab);
    g_signal_connect_after(tab->terminal, "button-release-event",
                           G_CALLBACK(on_terminal_mouse_release), tab);

    /* Plain URLs complement OSC 8 hyperlinks supplied by terminal programs. */
    g_autoptr(GError) regex_error = NULL;
    VteRegex *links = vte_regex_new_for_match("https?://[^[:space:]<>\"']+", -1,
                                              VTE_REGEX_FLAGS_DEFAULT, &regex_error);
    if (links) {
        int tag = vte_terminal_match_add_regex(tab->terminal, links, 0);
        vte_terminal_match_set_cursor_type(tab->terminal, tag, GDK_HAND2);
        vte_regex_unref(links);
    }

    g_autofree char *shell = tab->shell_session ? preferred_shell(profile) : NULL;
    char *shell_argv[] = {shell, NULL};
    char **argv = tab->shell_session ? shell_argv : command;
    SpawnResult *result = g_new0(SpawnResult, 1);
    g_weak_ref_init(&result->terminal, G_OBJECT(tab->page));
    vte_terminal_spawn_async(tab->terminal, VTE_PTY_DEFAULT, tab->initial_cwd,
                             argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                             NULL, -1, NULL, on_spawn_done, result);
    update_title(tab);
    gtk_widget_grab_focus(GTK_WIDGET(tab->terminal));
    return tab;
}

static void on_window_destroy(GtkWidget *widget, gpointer data)
{
    GalaxyWindow *win = data;
    (void)widget;
    win->app->windows = g_list_remove(win->app->windows, win);
    g_free(win);
}

static void on_settings_dark(GtkSettings *settings, GParamSpec *pspec, gpointer data)
{
    (void)settings; (void)pspec;
    galaxy_app_refresh(data);
}

static void on_menu_new_tab(GtkMenuItem *item, gpointer data)
{
    (void)item;
    on_plus_clicked(NULL, data);
}

static void on_menu_new_window(GtkMenuItem *item, gpointer data)
{
    GalaxyWindow *win = data;
    (void)item;
    GalaxyWindow *next = galaxy_window_new(win->app);
    galaxy_tab_new(next, NULL, g_get_home_dir(), NULL, NULL);
}

static void on_menu_find(GtkMenuItem *item, gpointer data)
{
    (void)item;
    galaxy_window_show_search(data);
}

static void on_menu_preferences(GtkMenuItem *item, gpointer data)
{
    GalaxyWindow *win = data;
    (void)item;
    galaxy_preferences_show(win->app, GTK_WINDOW(win->window));
}

static void on_menu_about(GtkMenuItem *item, gpointer data)
{
    GalaxyWindow *win = data;
    (void)item;
    gtk_show_about_dialog(GTK_WINDOW(win->window),
                          "program-name", "Galaxy Terminal", "version", "0.1.0",
                          "comments", "A terminal for the Cinnamon desktop",
                          "website", "https://github.com/The-Specter-X/Galaxy-Terminal", NULL);
}

static void append_menu(GtkWidget *menu, const char *label, GCallback callback, gpointer data)
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    g_signal_connect(item, "activate", callback, data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static gboolean on_window_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    GalaxyWindow *win = data;
    GalaxySettings *s = win->app->settings;
    (void)widget;
    /* When the search entry is focused it owns ordinary text-editing keys. */
    for (int i = 0; i < ACT_COUNT; ++i) {
        guint key = 0;
        GdkModifierType mods = 0;
        gtk_accelerator_parse(s->shortcuts[i], &key, &mods);
        GdkModifierType pressed = event->state & gtk_accelerator_get_default_mod_mask();
        gboolean plus = key == GDK_KEY_plus && event->keyval == GDK_KEY_plus &&
                        (pressed & ~GDK_SHIFT_MASK) == mods;
        if (!key || (!plus && (event->keyval != key || pressed != mods))) continue;
        GalaxyTab *tab = galaxy_current_tab(win);
        if (gtk_widget_has_focus(win->search_entry) && i != ACT_FIND && i != ACT_PREFERENCES)
            return FALSE;
        switch (i) {
        case ACT_NEW_TAB: on_plus_clicked(NULL, win); break;
        case ACT_NEW_WINDOW: on_menu_new_window(NULL, win); break;
        case ACT_CLOSE_TAB: close_tab(tab); break;
        case ACT_COPY: if (tab) vte_terminal_copy_clipboard_format(tab->terminal, VTE_FORMAT_TEXT); break;
        case ACT_PASTE: if (tab) vte_terminal_paste_clipboard(tab->terminal); break;
        case ACT_FIND: galaxy_window_show_search(win); break;
        case ACT_NEXT_TAB: gtk_notebook_next_page(GTK_NOTEBOOK(win->notebook)); break;
        case ACT_PREV_TAB: gtk_notebook_prev_page(GTK_NOTEBOOK(win->notebook)); break;
        case ACT_ZOOM_IN: if (tab) vte_terminal_set_font_scale(tab->terminal, tab->font_scale = MIN(3.0, tab->font_scale * 1.1)); break;
        case ACT_ZOOM_OUT: if (tab) vte_terminal_set_font_scale(tab->terminal, tab->font_scale = MAX(0.5, tab->font_scale / 1.1)); break;
        case ACT_ZOOM_RESET: if (tab) vte_terminal_set_font_scale(tab->terminal, tab->font_scale = 1.0); break;
        case ACT_PREFERENCES: on_menu_preferences(NULL, win); break;
        case ACT_FULLSCREEN:
            if (gdk_window_get_state(gtk_widget_get_window(win->window)) & GDK_WINDOW_STATE_FULLSCREEN)
                gtk_window_unfullscreen(GTK_WINDOW(win->window));
            else gtk_window_fullscreen(GTK_WINDOW(win->window));
            break;
        default:
            if (i >= ACT_TAB_1 && i <= ACT_TAB_9)
                gtk_notebook_set_current_page(GTK_NOTEBOOK(win->notebook), i - ACT_TAB_1);
            break;
        }
        return TRUE;
    }
    return FALSE;
}

GalaxyWindow *galaxy_window_new(GalaxyApp *app)
{
    GalaxyWindow *win = g_new0(GalaxyWindow, 1);
    win->app = app;
    win->window = xapp_gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_application_add_window(app->application, GTK_WINDOW(win->window));
    gtk_window_set_title(GTK_WINDOW(win->window), "Galaxy Terminal");
    xapp_gtk_window_set_icon_name(XAPP_GTK_WINDOW(win->window), "utilities-terminal");
    gtk_window_set_default_size(GTK_WINDOW(win->window), 880, 560);
    GdkScreen *screen = gtk_widget_get_screen(win->window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(win->window, visual);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Galaxy Terminal");
    gtk_window_set_titlebar(GTK_WINDOW(win->window), header);
    win->profiles_menu = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(win->profiles_menu), "Profiles ▾");
    gtk_widget_set_tooltip_text(win->profiles_menu, "Open a profile in a new tab");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), win->profiles_menu);
    rebuild_profiles(win);

    GtkWidget *menu_button = gtk_menu_button_new();
    GtkWidget *menu_image = gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(menu_button), menu_image);
    gtk_widget_set_tooltip_text(menu_button, "Terminal menu");
    GtkWidget *menu = gtk_menu_new();
    append_menu(menu, "New Tab", G_CALLBACK(on_menu_new_tab), win);
    append_menu(menu, "New Window", G_CALLBACK(on_menu_new_window), win);
    append_menu(menu, "Find", G_CALLBACK(on_menu_find), win);
    append_menu(menu, "Preferences", G_CALLBACK(on_menu_preferences), win);
    append_menu(menu, "About", G_CALLBACK(on_menu_about), win);
    gtk_widget_show_all(menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(menu_button), menu);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button);

    GtkWidget *layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(win->window), layout);
    win->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(win->notebook), TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(win->notebook), FALSE);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(win->notebook), app->settings->show_tabs);
    GtkWidget *plus = gtk_button_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(plus, "New tab");
    gtk_button_set_relief(GTK_BUTTON(plus), GTK_RELIEF_NONE);
    gtk_notebook_set_action_widget(GTK_NOTEBOOK(win->notebook), plus, GTK_PACK_END);
    gtk_widget_show(plus);
    g_signal_connect(plus, "clicked", G_CALLBACK(on_plus_clicked), win);
    gtk_box_pack_start(GTK_BOX(layout), win->notebook, TRUE, TRUE, 0);

    win->search_revealer = gtk_revealer_new();
    GtkWidget *search = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(search), 7);
    gtk_container_add(GTK_CONTAINER(win->search_revealer), search);
    win->search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(win->search_entry), "Find in terminal output");
    gtk_box_pack_start(GTK_BOX(search), win->search_entry, TRUE, TRUE, 0);
    win->case_button = gtk_check_button_new_with_label("Match case");
    gtk_box_pack_start(GTK_BOX(search), win->case_button, FALSE, FALSE, 0);
    GtkWidget *previous = gtk_button_new_from_icon_name("go-up-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *next = gtk_button_new_from_icon_name("go-down-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(previous, "Previous match");
    gtk_widget_set_tooltip_text(next, "Next match");
    gtk_box_pack_start(GTK_BOX(search), previous, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(search), next, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(search), close, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(layout), win->search_revealer, FALSE, FALSE, 0);
    g_signal_connect(win->search_entry, "changed", G_CALLBACK(on_search_changed), win);
    g_signal_connect(win->search_entry, "key-press-event", G_CALLBACK(on_search_key), win);
    g_signal_connect(win->case_button, "toggled", G_CALLBACK(on_search_changed), win);
    g_signal_connect(previous, "clicked", G_CALLBACK(on_search_previous), win);
    g_signal_connect(next, "clicked", G_CALLBACK(on_search_next), win);
    g_signal_connect(close, "clicked", G_CALLBACK(on_search_close), win);
    g_signal_connect(win->notebook, "switch-page", G_CALLBACK(on_page_changed), win);
    g_signal_connect(win->window, "key-press-event", G_CALLBACK(on_window_key), win);
    g_signal_connect(win->window, "delete-event", G_CALLBACK(on_window_delete), win);
    g_signal_connect(win->window, "destroy", G_CALLBACK(on_window_destroy), win);
    app->windows = g_list_prepend(app->windows, win);
    gtk_widget_show_all(win->window);
    gtk_revealer_set_reveal_child(GTK_REVEALER(win->search_revealer), FALSE);
    return win;
}

void galaxy_app_refresh(gpointer data)
{
    GalaxyApp *app = data;
    if (!app || !app->settings) return;
    for (GList *w = app->windows; w; w = w->next) {
        GalaxyWindow *win = w->data;
        GtkNotebook *book = GTK_NOTEBOOK(win->notebook);
        gtk_notebook_set_show_tabs(book, app->settings->show_tabs);
        rebuild_profiles(win);
        for (int i = 0; i < gtk_notebook_get_n_pages(book); ++i) {
            GtkWidget *page = gtk_notebook_get_nth_page(book, i);
            GalaxyTab *tab = g_object_get_data(G_OBJECT(page), "galaxy-tab");
            apply_palette(tab);
        }
    }
}

static int on_command_line(GApplication *application, GApplicationCommandLine *line,
                           gpointer data)
{
    GalaxyApp *app = data;
    int argc = 0;
    char **original = g_application_command_line_get_arguments(line, &argc);
    g_auto(GStrv) args = original;
    gboolean new_tab = FALSE, new_window = FALSE;
    char *cwd = NULL, *profile = NULL, *title = NULL;
    char **command = NULL;
    GOptionEntry options[] = {
        {"new-tab", 0, 0, G_OPTION_ARG_NONE, &new_tab, "Open a tab in the latest window", NULL},
        {"new-window", 0, 0, G_OPTION_ARG_NONE, &new_window, "Open a new window", NULL},
        {"working-directory", 0, 0, G_OPTION_ARG_FILENAME, &cwd, "Initial directory", "DIR"},
        {"profile", 0, 0, G_OPTION_ARG_STRING, &profile, "Profile name", "NAME"},
        {"title", 0, 0, G_OPTION_ARG_STRING, &title, "Tab title", "TITLE"},
        {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &command, "Command and arguments", "COMMAND"},
        {NULL}
    };
    g_autoptr(GOptionContext) context = g_option_context_new("-- [COMMAND [ARG...]]");
    g_option_context_add_main_entries(context, options, NULL);
    g_autoptr(GError) error = NULL;
    if (!g_option_context_parse_strv(context, &args, &error)) {
        g_application_command_line_printerr(line, "%s\n", error->message);
        g_free(cwd); g_free(profile); g_free(title); g_strfreev(command);
        return 2;
    }
    if (profile && !galaxy_settings_profile(app->settings, profile)) {
        g_application_command_line_printerr(line, "Unknown profile: %s\n", profile);
        g_free(cwd); g_free(profile); g_free(title); g_strfreev(command);
        return 2;
    }
    const char *requested_cwd = cwd ? cwd : g_application_command_line_get_cwd(line);
    g_autofree char *absolute = g_canonicalize_filename(requested_cwd, g_application_command_line_get_cwd(line));
    if (!g_file_test(absolute, G_FILE_TEST_IS_DIR)) {
        g_application_command_line_printerr(line, "Directory does not exist: %s\n", absolute);
        g_free(cwd); g_free(profile); g_free(title); g_strfreev(command);
        return 2;
    }
    GalaxyWindow *win = (new_tab && !new_window && app->windows)
        ? app->windows->data : galaxy_window_new(app);
    galaxy_tab_new(win, profile, absolute, title, command);
    gtk_window_present(GTK_WINDOW(win->window));
    g_free(cwd); g_free(profile); g_free(title); g_strfreev(command);
    (void)application; (void)argc;
    return 0;
}

int main(int argc, char **argv)
{
    GalaxyApp app = {0};
    app.application = gtk_application_new("org.thespecterx.GalaxyTerminal", G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(app.application, "command-line", G_CALLBACK(on_command_line), &app);
    app.settings = galaxy_settings_new();
    galaxy_settings_set_changed(app.settings, galaxy_app_refresh, &app);
    /* XApp tracks the desktop's color scheme for the GTK chrome. */
    app.dark_mode_manager = G_OBJECT(xapp_dark_mode_manager_new(FALSE));
    g_signal_connect(gtk_settings_get_default(), "notify::gtk-application-prefer-dark-theme",
                     G_CALLBACK(on_settings_dark), &app);
    int status = g_application_run(G_APPLICATION(app.application), argc, argv);
    galaxy_settings_free(app.settings);
    g_clear_object(&app.dark_mode_manager);
    g_object_unref(app.application);
    return status;
}
