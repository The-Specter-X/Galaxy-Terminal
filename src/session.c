#include "app.h"
#include <glib/gi18n.h>
#include <unistd.h>
#include <string.h>

static GalaxyTab *page_tab(GtkWidget *page)
{
    return g_object_get_data(G_OBJECT(page), "galaxy-tab");
}

char *galaxy_local_directory_uri(const char *uri)
{
    if (!uri) return NULL;
    g_autofree char *host = NULL;
    char *path = g_filename_from_uri(uri, &host, NULL);
    if (!path) return NULL;
    if ((host && *host && g_ascii_strcasecmp(host, "localhost") &&
         g_ascii_strcasecmp(host, g_get_host_name())) ||
        !g_file_test(path, G_FILE_TEST_IS_DIR)) {
        g_free(path);
        return NULL;
    }
    return path;
}

char *galaxy_tab_directory(GalaxyTab *tab)
{
    g_autoptr(GUri) uri = vte_terminal_ref_termprop_uri(tab->terminal,
                                                       VTE_TERMPROP_CURRENT_DIRECTORY_URI);
    if (uri) {
        g_autofree char *text = g_uri_to_string(uri);
        char *path = galaxy_local_directory_uri(text);
        if (path) return path;
    }
    if (tab->pid > 0) {
        g_autofree char *proc = g_strdup_printf("/proc/%d/cwd", tab->pid);
        char *path = g_file_read_link(proc, NULL);
        if (path && g_file_test(path, G_FILE_TEST_IS_DIR)) return path;
        g_free(path);
    }
    return g_strdup(tab->initial_cwd);
}

void galaxy_tab_update_title(GalaxyTab *tab)
{
    if (tab->closing) return;
    g_autofree char *reported = vte_terminal_dup_termprop_string(tab->terminal,
                                                               VTE_TERMPROP_XTERM_TITLE, NULL);
    g_autofree char *cwd = galaxy_tab_directory(tab);
    g_autofree char *base = g_filename_display_basename(cwd);
    const char *title = tab->custom_title && *tab->custom_title ? tab->custom_title :
                        (reported && *reported ? reported : base);
    gtk_label_set_text(GTK_LABEL(tab->label), title);
    gtk_widget_set_tooltip_text(tab->label, title);
    if (galaxy_current_tab(tab->owner) == tab) {
        g_autofree char *window_title = g_strdup_printf("%s — Galaxy Terminal", title);
        gtk_window_set_title(GTK_WINDOW(tab->owner->window), window_title);
    }
}

static void termprop_changed(VteTerminal *terminal, const char *name, gpointer data)
{
    (void)terminal;
    if (!strcmp(name, VTE_TERMPROP_XTERM_TITLE) ||
        !strcmp(name, VTE_TERMPROP_CURRENT_DIRECTORY_URI)) galaxy_tab_update_title(data);
}

void galaxy_tab_apply(GalaxyTab *tab, guint changes)
{
    if (tab->closing) return;
    GalaxySettings *s = tab->owner->app->settings;
    GalaxyProfile *p = galaxy_settings_profile(s, tab->profile_name);
    if (!p) {
        g_free(tab->profile_name); tab->profile_name = g_strdup(s->default_profile);
        p = galaxy_settings_profile(s, tab->profile_name);
    }
    if (changes & (GALAXY_CHANGE_COLORS | GALAXY_CHANGE_PROFILES | GALAXY_CHANGE_RELOAD)) {
        g_autoptr(GString) state = g_string_new(NULL);
        g_string_append_printf(state, "%s|%s|%s|%s|%g|%d|%d|%d|%d|%d|%d",
            p->font, p->palette, p->foreground, p->background, p->opacity,
            galaxy_desktop_dark(), p->cursor_shape, p->cursor_blink, p->audible_bell,
            p->scroll_on_output, p->scroll_on_keystroke);
        for (int i = 0; i < 16; i++) g_string_append_printf(state, "|%s", p->ansi[i]);
        if (g_strcmp0(state->str, tab->appearance_state)) {
            galaxy_terminal_apply_profile(tab->terminal, p);
            g_free(tab->appearance_state); tab->appearance_state = g_strdup(state->str);
        }
    }
    if (changes & (GALAXY_CHANGE_BEHAVIOR | GALAXY_CHANGE_RELOAD)) {
        g_autofree char *state = g_strdup_printf("%d|%d|%d", s->scrollback,
                                                 s->mouse_autohide, s->show_scrollbar);
        if (g_strcmp0(state, tab->behavior_state)) {
            vte_terminal_set_scrollback_lines(tab->terminal, s->scrollback);
            vte_terminal_set_mouse_autohide(tab->terminal, s->mouse_autohide);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tab->scrolled),
                GTK_POLICY_NEVER, s->show_scrollbar ? GTK_POLICY_ALWAYS : GTK_POLICY_NEVER);
            g_free(tab->behavior_state); tab->behavior_state = g_strdup(state);
        }
    }
}

gboolean galaxy_tab_busy(GalaxyTab *tab)
{
    if (tab->closing) return FALSE;
    if (tab->pid <= 0) return tab->spawn_cancel != NULL;
    if (!tab->shell_session) return TRUE;
    VtePty *pty = vte_terminal_get_pty(tab->terminal);
    if (!pty) return FALSE;
    pid_t foreground = tcgetpgrp(vte_pty_get_fd(pty));
    return foreground > 0 && foreground != tab->pid;
}

static void tab_free(gpointer data)
{
    GalaxyTab *tab = data;
    g_clear_object(&tab->spawn_cancel);
    g_free(tab->profile_name); g_free(tab->custom_title); g_free(tab->initial_cwd);
    g_free(tab->appearance_state); g_free(tab->behavior_state); g_free(tab->search_state);
    g_free(tab);
}

static void page_destroyed(GtkWidget *page, gpointer data)
{
    (void)page;
    GalaxyTab *tab = data;
    tab->closing = TRUE;
    g_signal_handlers_disconnect_by_data(tab->terminal, tab);
    if (tab->spawn_cancel) g_cancellable_cancel(tab->spawn_cancel);
    g_object_set_data(G_OBJECT(tab->terminal), "galaxy-tab", NULL);
}

void galaxy_tab_destroy(GalaxyTab *tab)
{
    if (!tab->closing) gtk_widget_destroy(tab->page);
}

static void close_response(GtkDialog *dialog, int response, GtkWidget *page)
{
    GalaxyTab *tab = page_tab(page);
    if (tab && !tab->closing) {
        tab->close_requested = FALSE;
        if (response == GTK_RESPONSE_ACCEPT) galaxy_tab_destroy(tab);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void galaxy_tab_request_close(GalaxyTab *tab)
{
    if (tab->closing || tab->close_requested) return;
    if (!tab->owner->app->settings->confirm_close || !galaxy_tab_busy(tab)) {
        galaxy_tab_destroy(tab);
        return;
    }
    tab->close_requested = TRUE;
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(tab->owner->window),
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_CANCEL,
        "%s", _("Close this tab and its running process?"));
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Close tab"), GTK_RESPONSE_ACCEPT);
    g_signal_connect_object(dialog, "response", G_CALLBACK(close_response), tab->page, 0);
    g_signal_connect_object(tab->page, "destroy", G_CALLBACK(gtk_widget_destroy), dialog,
                            G_CONNECT_SWAPPED);
    gtk_widget_show(dialog);
}

static void rename_response(GtkDialog *dialog, int response, GtkWidget *page)
{
    GalaxyTab *tab = page_tab(page);
    if (tab && !tab->closing && response == GTK_RESPONSE_ACCEPT) {
        GtkWidget *entry = g_object_get_data(G_OBJECT(dialog), "entry");
        g_free(tab->custom_title);
        tab->custom_title = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
        galaxy_tab_update_title(tab);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void galaxy_tab_rename(GalaxyTab *tab)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Tab title"), GTK_WINDOW(tab->owner->window),
        GTK_DIALOG_DESTROY_WITH_PARENT, _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Apply"), GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), tab->custom_title ? tab->custom_title : "");
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Empty uses the shell title"));
    gtk_widget_set_margin_start(entry, 16); gtk_widget_set_margin_end(entry, 16);
    gtk_widget_set_margin_top(entry, 16); gtk_widget_set_margin_bottom(entry, 16);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry);
    g_object_set_data(G_OBJECT(dialog), "entry", entry);
    g_signal_connect_object(dialog, "response", G_CALLBACK(rename_response), tab->page, 0);
    g_signal_connect_object(tab->page, "destroy", G_CALLBACK(gtk_widget_destroy), dialog,
                            G_CONNECT_SWAPPED);
    gtk_widget_show_all(dialog);
}

static void child_exited(VteTerminal *terminal, int status, gpointer data)
{
    (void)terminal; (void)status;
    GalaxyTab *tab = data;
    tab->pid = 0;
    galaxy_tab_destroy(tab);
}

/* VTE may complete a spawn after its widget has gone away. Never retain a raw tab. */
static void spawned(VteTerminal *terminal, GPid pid, GError *error, gpointer data)
{
    GWeakRef *weak = data;
    g_autoptr(GObject) page = g_weak_ref_get(weak);
    g_weak_ref_clear(weak); g_free(weak);
    if (!terminal || !page) return;
    GalaxyTab *tab = page_tab(GTK_WIDGET(page));
    if (!tab || tab->closing) return;
    g_clear_object(&tab->spawn_cancel);
    if (error) {
        g_autofree char *message = g_strdup_printf(_("\r\nCould not start the terminal: %s\r\nCheck the profile in Preferences.\r\n"), error->message);
        vte_terminal_feed(terminal, message, -1);
    } else tab->pid = pid;
}

static gboolean copy_selection(gpointer data)
{
    VteTerminal *terminal = data;
    GalaxyTab *tab = g_object_get_data(G_OBJECT(terminal), "galaxy-tab");
    if (tab && !tab->closing && tab->owner->app->settings->auto_copy &&
        vte_terminal_get_has_selection(terminal))
        vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
    return G_SOURCE_REMOVE;
}

static gboolean button_released(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)data;
    if (event->button == 1)
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, copy_selection, g_object_ref(widget), g_object_unref);
    return FALSE;
}

static void menu_action(GtkMenuItem *item, VteTerminal *terminal)
{
    GalaxyTab *tab = g_object_get_data(G_OBJECT(terminal), "galaxy-tab");
    if (!tab || tab->closing) return;
    GalaxyAction action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "action"));
    if (action == ACT_COPY) vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
    else if (action == ACT_PASTE) vte_terminal_paste_clipboard(terminal);
    else galaxy_window_action(tab->owner, action);
}

static void setup_menu(VteTerminal *terminal, const VteEventContext *context, gpointer data)
{
    (void)data;
    if (!context) return;
    GtkWidget *menu = gtk_menu_new();
    const GalaxyAction actions[] = {ACT_COPY, ACT_PASTE, ACT_NEW_TAB, ACT_FIND, ACT_PREFERENCES};
    for (guint i = 0; i < G_N_ELEMENTS(actions); i++) {
        GalaxyAction action = actions[i];
        GtkWidget *item = gtk_menu_item_new_with_label(_(galaxy_shortcuts[action].label));
        g_object_set_data(G_OBJECT(item), "action", GINT_TO_POINTER(action));
        if (action == ACT_COPY) gtk_widget_set_sensitive(item, vte_terminal_get_has_selection(terminal));
        g_signal_connect_object(item, "activate", G_CALLBACK(menu_action), terminal, 0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_widget_show_all(menu);
    vte_terminal_set_context_menu(terminal, menu);
}

static void close_clicked(GtkButton *button, GtkWidget *page)
{
    (void)button;
    GalaxyTab *tab = page_tab(page);
    if (tab && !tab->closing) galaxy_tab_request_close(tab);
}

GalaxyTab *galaxy_tab_new(GalaxyWindow *win, const char *profile_name,
    const char *cwd, const char *title, char **command, char **environment)
{
    GalaxyProfile *profile = galaxy_settings_profile(win->app->settings, profile_name);
    if (!profile) profile = galaxy_settings_profile(win->app->settings, win->app->settings->default_profile);
    GalaxyTab *tab = g_new0(GalaxyTab, 1);
    tab->owner = win; tab->profile_name = g_strdup(profile->name);
    tab->custom_title = g_strdup(title); tab->font_scale = 1.0;
    tab->initial_cwd = g_strdup(cwd ? cwd : (*profile->cwd ? profile->cwd : g_get_home_dir()));
    tab->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data_full(G_OBJECT(tab->page), "galaxy-tab", tab, tab_free);
    tab->terminal = VTE_TERMINAL(vte_terminal_new());
    g_object_set_data(G_OBJECT(tab->terminal), "galaxy-tab", tab);
    tab->scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(tab->scrolled), "galaxy-terminal");
    gtk_container_add(GTK_CONTAINER(tab->scrolled), GTK_WIDGET(tab->terminal));
    gtk_box_pack_start(GTK_BOX(tab->page), tab->scrolled, TRUE, TRUE, 0);
    vte_terminal_set_allow_hyperlink(tab->terminal, TRUE);
    vte_terminal_set_enable_sixel(tab->terminal, FALSE);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    tab->label = gtk_label_new(_("Terminal"));
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(tab->label), 24);
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(close, _("Close tab"));
    gtk_box_pack_start(GTK_BOX(header), tab->label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(header), close, FALSE, FALSE, 0);
    g_signal_connect_object(close, "clicked", G_CALLBACK(close_clicked), tab->page, 0);
    g_signal_connect(tab->page, "destroy", G_CALLBACK(page_destroyed), tab);
    g_signal_connect(tab->terminal, "child-exited", G_CALLBACK(child_exited), tab);
    g_signal_connect(tab->terminal, "termprop-changed", G_CALLBACK(termprop_changed), tab);
    g_signal_connect(tab->terminal, "button-release-event", G_CALLBACK(button_released), NULL);
    g_signal_connect(tab->terminal, "setup-context-menu", G_CALLBACK(setup_menu), NULL);
    gtk_widget_show_all(tab->page); gtk_widget_show_all(header);
    int index = gtk_notebook_append_page(GTK_NOTEBOOK(win->notebook), tab->page, header);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(win->notebook), tab->page, TRUE);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(win->notebook), index);
    galaxy_tab_apply(tab, GALAXY_CHANGE_ALL);
    galaxy_window_update_tabs(win);
    galaxy_tab_update_title(tab);
    gtk_widget_grab_focus(GTK_WIDGET(tab->terminal));
    g_autofree char *user_shell = vte_get_user_shell();
    char *shell_argv[] = {*profile->shell ? profile->shell : (user_shell ? user_shell : "/bin/sh"), NULL};
    tab->shell_session = !command || !command[0];
    char **argv = tab->shell_session ? shell_argv : command;
    char **source_env = environment ? environment : win->app->environment;
    g_auto(GStrv) env = g_strdupv(source_env);
    env = g_environ_setenv(env, "TERM", "xterm-256color", TRUE);
    env = g_environ_setenv(env, "COLORTERM", "truecolor", TRUE);
    /* Nested GUI launches must not pretend to still be inside the parent's tmux. */
    env = g_environ_unsetenv(env, "TMUX");
    env = g_environ_unsetenv(env, "TMUX_PANE");
    tab->spawn_cancel = g_cancellable_new();
    GWeakRef *weak = g_new0(GWeakRef, 1);
    g_weak_ref_init(weak, tab->page);
    vte_terminal_spawn_async(tab->terminal, VTE_PTY_DEFAULT, tab->initial_cwd, argv, env,
        G_SPAWN_SEARCH_PATH_FROM_ENVP | VTE_SPAWN_NO_PARENT_ENVV,
        NULL, NULL, NULL, -1, tab->spawn_cancel, spawned, weak);
    return tab;
}
