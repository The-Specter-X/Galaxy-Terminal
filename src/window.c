#include "app.h"
#include "config.h"
#include <glib/gi18n.h>
#include <libxapp/xapp-gtk-window.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

GalaxyTab *galaxy_current_tab(GalaxyWindow *win)
{
    int index = gtk_notebook_get_current_page(GTK_NOTEBOOK(win->notebook));
    GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(win->notebook), index);
    return page ? g_object_get_data(G_OBJECT(page), "galaxy-tab") : NULL;
}

void galaxy_window_update_tabs(GalaxyWindow *win)
{
    if (win->closing) return;
    int count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(win->notebook));
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(win->notebook),
                               win->app->settings->show_tabs || count > 1);
}

void galaxy_window_search(GalaxyWindow *win, gboolean forward)
{
    if (win->closing) return;
    GalaxyTab *tab = galaxy_current_tab(win);
    if (!tab) return;
    const char *text = gtk_entry_get_text(GTK_ENTRY(win->search_entry));
    if (!*text || !gtk_revealer_get_reveal_child(GTK_REVEALER(win->search_revealer))) {
        vte_terminal_search_set_regex(tab->terminal, NULL, 0);
        g_clear_pointer(&tab->search_state, g_free);
        gtk_label_set_text(GTK_LABEL(win->search_status), "");
        return;
    }
    g_autofree char *escaped = g_regex_escape_string(text, -1);
    guint32 flags = PCRE2_MULTILINE | PCRE2_UTF | PCRE2_UCP;
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(win->case_button))) flags |= PCRE2_CASELESS;
    g_autofree char *state = g_strdup_printf("%u:%s", flags, text);
    if (g_strcmp0(state, tab->search_state)) {
        g_autoptr(GError) error = NULL;
        VteRegex *regex = vte_regex_new_for_search(escaped, -1, flags, &error);
        if (!regex) {
            gtk_label_set_text(GTK_LABEL(win->search_status), error->message);
            return;
        }
        vte_terminal_search_set_regex(tab->terminal, regex, 0);
        vte_regex_unref(regex);
        g_free(tab->search_state); tab->search_state = g_strdup(state);
    }
    vte_terminal_search_set_wrap_around(tab->terminal, TRUE);
    gboolean found = forward ? vte_terminal_search_find_next(tab->terminal) :
                               vte_terminal_search_find_previous(tab->terminal);
    gtk_label_set_text(GTK_LABEL(win->search_status), found ? _("Match found") : _("No matches"));
}

static gboolean search_timeout(gpointer data)
{
    GalaxyWindow *win = data;
    win->search_source = 0;
    galaxy_window_search(win, TRUE);
    return G_SOURCE_REMOVE;
}

static void search_changed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    GalaxyWindow *win = data;
    if (win->search_source) g_source_remove(win->search_source);
    win->search_source = g_timeout_add(150, search_timeout, win);
}

static void search_next(GtkWidget *widget, gpointer data)
{
    (void)widget;
    galaxy_window_search(data, TRUE);
}

static void search_previous(GtkWidget *widget, gpointer data)
{
    (void)widget;
    galaxy_window_search(data, FALSE);
}

static void search_hide(GtkWidget *widget, gpointer data)
{
    (void)widget;
    GalaxyWindow *win = data;
    gtk_revealer_set_reveal_child(GTK_REVEALER(win->search_revealer), FALSE);
    galaxy_window_search(win, TRUE);
    GalaxyTab *tab = galaxy_current_tab(win);
    if (tab) gtk_widget_grab_focus(GTK_WIDGET(tab->terminal));
}

static gboolean search_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    if (event->keyval == GDK_KEY_Escape) { search_hide(widget, data); return TRUE; }
    if (event->keyval == GDK_KEY_Return && (event->state & GDK_SHIFT_MASK)) {
        search_previous(widget, data); return TRUE;
    }
    return FALSE;
}

void galaxy_window_show_search(GalaxyWindow *win)
{
    gtk_revealer_set_reveal_child(GTK_REVEALER(win->search_revealer), TRUE);
    gtk_widget_grab_focus(win->search_entry);
    gtk_editable_select_region(GTK_EDITABLE(win->search_entry), 0, -1);
    galaxy_window_search(win, TRUE);
}

static void switched(GtkNotebook *notebook, GtkWidget *page, guint index, gpointer data)
{
    (void)notebook; (void)index;
    GalaxyWindow *win = data;
    if (win->closing) return;
    GalaxyTab *tab = g_object_get_data(G_OBJECT(page), "galaxy-tab");
    if (!tab) return;
    galaxy_tab_update_title(tab);
    galaxy_window_search(win, TRUE);
    if (gtk_revealer_get_reveal_child(GTK_REVEALER(win->search_revealer)))
        gtk_widget_grab_focus(win->search_entry);
    else gtk_widget_grab_focus(GTK_WIDGET(tab->terminal));
}

static void page_removed(GtkNotebook *notebook, GtkWidget *page, guint index, gpointer data)
{
    (void)page; (void)index;
    GalaxyWindow *win = data;
    if (win->closing) return;
    if (!gtk_notebook_get_n_pages(notebook)) gtk_widget_destroy(win->window);
    else galaxy_window_update_tabs(win);
}

void galaxy_window_action(GalaxyWindow *win, GalaxyAction action)
{
    if (win->closing) return;
    GalaxyTab *tab = galaxy_current_tab(win);
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(win->window));
    switch (action) {
    case ACT_NEW_TAB:
    case ACT_NEW_WINDOW: {
        g_autofree char *cwd = tab ? galaxy_tab_directory(tab) : NULL;
        g_autofree char *profile = g_strdup(tab ? tab->profile_name : win->app->settings->default_profile);
        GalaxyWindow *target = action == ACT_NEW_WINDOW ? galaxy_window_new(win->app) : win;
        galaxy_tab_new(target, profile, cwd, NULL, NULL, NULL);
        gtk_window_present(GTK_WINDOW(target->window));
        break;
    }
    case ACT_CLOSE_TAB: if (tab) galaxy_tab_request_close(tab); break;
    case ACT_COPY:
        if (GTK_IS_EDITABLE(focus)) gtk_editable_copy_clipboard(GTK_EDITABLE(focus));
        else if (tab) vte_terminal_copy_clipboard_format(tab->terminal, VTE_FORMAT_TEXT);
        break;
    case ACT_PASTE:
        if (GTK_IS_EDITABLE(focus)) gtk_editable_paste_clipboard(GTK_EDITABLE(focus));
        else if (tab) vte_terminal_paste_clipboard(tab->terminal);
        break;
    case ACT_FIND: galaxy_window_show_search(win); break;
    case ACT_NEXT_TAB:
    case ACT_PREV_TAB: {
        int count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(win->notebook));
        int index = gtk_notebook_get_current_page(GTK_NOTEBOOK(win->notebook));
        if (count) gtk_notebook_set_current_page(GTK_NOTEBOOK(win->notebook),
            (index + (action == ACT_NEXT_TAB ? 1 : count - 1)) % count);
        break;
    }
    case ACT_ZOOM_IN: case ACT_ZOOM_OUT: case ACT_ZOOM_RESET:
        if (tab) {
            tab->font_scale = action == ACT_ZOOM_RESET ? 1.0 :
                CLAMP(tab->font_scale + (action == ACT_ZOOM_IN ? 0.1 : -0.1), 0.5, 3.0);
            vte_terminal_set_font_scale(tab->terminal, tab->font_scale);
        }
        break;
    case ACT_PREFERENCES: galaxy_preferences_show(win->app, GTK_WINDOW(win->window)); break;
    case ACT_FULLSCREEN: {
        GdkWindow *window = gtk_widget_get_window(win->window);
        if (window && (gdk_window_get_state(window) & GDK_WINDOW_STATE_FULLSCREEN))
            gtk_window_unfullscreen(GTK_WINDOW(win->window));
        else gtk_window_fullscreen(GTK_WINDOW(win->window));
        break;
    }
    default:
        if (action >= ACT_TAB_1 && action <= ACT_TAB_9) {
            int target = action == ACT_TAB_9 ? -1 : (int)action - ACT_TAB_1;
            if (target < gtk_notebook_get_n_pages(GTK_NOTEBOOK(win->notebook)))
                gtk_notebook_set_current_page(GTK_NOTEBOOK(win->notebook), target);
        }
    }
}

typedef struct { GalaxyWindow *win; GalaxyAction action; } ActionData;
static gboolean accelerator(GtkAccelGroup *group, GObject *object, guint key,
                            GdkModifierType mods, gpointer data)
{
    (void)group; (void)object; (void)key; (void)mods;
    ActionData *action = data;
    galaxy_window_action(action->win, action->action);
    return TRUE;
}
static void action_free(gpointer data, GClosure *closure) { (void)closure; g_free(data); }

void galaxy_window_install_shortcuts(GalaxyWindow *win)
{
    if (win->accelerators) {
        gtk_window_remove_accel_group(GTK_WINDOW(win->window), win->accelerators);
        g_clear_object(&win->accelerators);
    }
    win->accelerators = gtk_accel_group_new();
    for (int i = 0; i < ACT_COUNT; i++) {
        guint key; GdkModifierType mods;
        if (!galaxy_shortcut_parse(win->app->settings->shortcuts[i], &key, &mods) || !key) continue;
        ActionData *data = g_new(ActionData, 1);
        data->win = win; data->action = i;
        GClosure *closure = g_cclosure_new(G_CALLBACK(accelerator), data, action_free);
        gtk_accel_group_connect(win->accelerators, key, mods, GTK_ACCEL_VISIBLE, closure);
    }
    gtk_window_add_accel_group(GTK_WINDOW(win->window), win->accelerators);
}

static void action_clicked(GtkWidget *widget, GtkWidget *window)
{
    GalaxyWindow *win = g_object_get_data(G_OBJECT(window), "galaxy-window");
    if (win && !win->closing)
        galaxy_window_action(win, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "action")));
}
static GtkWidget *action_button(GalaxyWindow *win, const char *icon, GalaxyAction action)
{
    GtkWidget *button = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(button, _(galaxy_shortcuts[action].label));
    g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(action));
    g_signal_connect_object(button, "clicked", G_CALLBACK(action_clicked), win->window, 0);
    return button;
}

static void profile_selected(GtkMenuItem *item, GtkWidget *window)
{
    GalaxyWindow *win = g_object_get_data(G_OBJECT(window), "galaxy-window");
    if (!win || win->closing) return;
    GalaxyTab *tab = galaxy_current_tab(win);
    const char *name = g_object_get_data(G_OBJECT(item), "profile");
    if (tab) {
        g_free(tab->profile_name); tab->profile_name = g_strdup(name);
        galaxy_tab_apply(tab, GALAXY_CHANGE_ALL);
    }
}
static void profiles_rebuild(GalaxyWindow *win)
{
    GtkWidget *menu = gtk_menu_new();
    GalaxySettings *s = win->app->settings;
    for (guint i = 0; i < s->profiles->len; i++) {
        GalaxyProfile *profile = s->profiles->pdata[i];
        GtkWidget *item = gtk_menu_item_new_with_label(profile->name);
        g_object_set_data_full(G_OBJECT(item), "profile", g_strdup(profile->name), g_free);
        g_signal_connect_object(item, "activate", G_CALLBACK(profile_selected), win->window, 0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_widget_show_all(menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(win->profiles_menu), menu);
}

void galaxy_window_refresh(GalaxyWindow *win, guint changes)
{
    if (win->closing) return;
    if (changes & (GALAXY_CHANGE_BEHAVIOR | GALAXY_CHANGE_RELOAD)) galaxy_window_update_tabs(win);
    if (changes & (GALAXY_CHANGE_PROFILES | GALAXY_CHANGE_RELOAD)) profiles_rebuild(win);
    if (changes & (GALAXY_CHANGE_SHORTCUTS | GALAXY_CHANGE_RELOAD)) galaxy_window_install_shortcuts(win);
    if (changes & GALAXY_CHANGE_STATUS) {
        const char *error = win->app->settings->error;
        gtk_label_set_text(GTK_LABEL(win->settings_error), error ? error : "");
        gtk_widget_set_visible(win->settings_error, error != NULL);
    }
    int count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(win->notebook));
    for (int i = 0; i < count; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(win->notebook), i);
        GalaxyTab *tab = g_object_get_data(G_OBJECT(page), "galaxy-tab");
        if (tab) galaxy_tab_apply(tab, changes);
    }
}

static void close_response(GtkDialog *dialog, int response, GtkWidget *window)
{
    GalaxyWindow *win = g_object_get_data(G_OBJECT(window), "galaxy-window");
    win->close_requested = FALSE;
    if (response == GTK_RESPONSE_ACCEPT) gtk_widget_destroy(window);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}
void galaxy_window_request_close(GalaxyWindow *win)
{
    if (win->closing || win->close_requested) return;
    gboolean busy = FALSE;
    int count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(win->notebook));
    for (int i = 0; i < count; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(win->notebook), i);
        if (galaxy_tab_busy(g_object_get_data(G_OBJECT(page), "galaxy-tab"))) busy = TRUE;
    }
    if (!busy || !win->app->settings->confirm_close) { gtk_widget_destroy(win->window); return; }
    win->close_requested = TRUE;
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(win->window),
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_CANCEL,
        "%s", _("Close this window and its running processes?"));
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Close window"), GTK_RESPONSE_ACCEPT);
    g_signal_connect_object(dialog, "response", G_CALLBACK(close_response), win->window, 0);
    gtk_widget_show(dialog);
}
static gboolean delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    (void)widget; (void)event;
    galaxy_window_request_close(data);
    return TRUE;
}
static void window_destroyed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    GalaxyWindow *win = data;
    win->closing = TRUE;
    if (win->search_source) { g_source_remove(win->search_source); win->search_source = 0; }
    win->app->windows = g_list_remove(win->app->windows, win);
}
static void window_free(gpointer data)
{
    GalaxyWindow *win = data;
    g_clear_object(&win->accelerators);
    g_free(win);
}
static void rename_clicked(GtkMenuItem *item, GtkWidget *window)
{
    (void)item;
    GalaxyWindow *win = g_object_get_data(G_OBJECT(window), "galaxy-window");
    if (!win || win->closing) return;
    GalaxyTab *tab = galaxy_current_tab(win);
    if (tab) galaxy_tab_rename(tab);
}
static void about_clicked(GtkMenuItem *item, GtkWidget *window)
{
    (void)item;
    GalaxyWindow *win = g_object_get_data(G_OBJECT(window), "galaxy-window");
    if (!win || win->closing) return;
    gtk_show_about_dialog(GTK_WINDOW(win->window), "program-name", "Galaxy Terminal",
        "version", GALAXY_VERSION, "license-type", GTK_LICENSE_MIT_X11,
        "website", "https://github.com/The-Specter-X/Galaxy-Terminal",
        "comments", _("A GTK 3 and XApp terminal for Wayland"), NULL);
}

GalaxyWindow *galaxy_window_new(GalaxyApp *app)
{
    GalaxyWindow *win = g_new0(GalaxyWindow, 1);
    win->app = app;
    win->window = xapp_gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_object_set_data_full(G_OBJECT(win->window), "galaxy-window", win, window_free);
    app->windows = g_list_append(app->windows, win);
    gtk_window_set_application(GTK_WINDOW(win->window), app->application);
    gtk_window_set_default_size(GTK_WINDOW(win->window), 960, 620);
    gtk_window_set_title(GTK_WINDOW(win->window), "Galaxy Terminal");
    gtk_window_set_icon_name(GTK_WINDOW(win->window), "utilities-terminal");
    gtk_widget_set_app_paintable(win->window, TRUE);
    GdkVisual *visual = gdk_screen_get_rgba_visual(gtk_widget_get_screen(win->window));
    if (visual) gtk_widget_set_visual(win->window, visual);
    gtk_style_context_add_class(gtk_widget_get_style_context(win->window), "galaxy-window");
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Galaxy Terminal");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), action_button(win, "tab-new-symbolic", ACT_NEW_TAB));
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), action_button(win, "edit-find-symbolic", ACT_FIND));
    win->profiles_menu = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(win->profiles_menu), _("Profile"));
    gtk_widget_set_tooltip_text(win->profiles_menu, _("Apply a profile to the current tab"));
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), win->profiles_menu);
    GtkWidget *menu_button = gtk_menu_button_new();
    gtk_button_set_image(GTK_BUTTON(menu_button), gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON));
    GtkWidget *menu = gtk_menu_new();
    const GalaxyAction actions[] = {ACT_NEW_WINDOW, ACT_FIND, ACT_PREFERENCES, ACT_FULLSCREEN};
    for (guint i = 0; i < G_N_ELEMENTS(actions); i++) {
        GtkWidget *item = gtk_menu_item_new_with_label(_(galaxy_shortcuts[actions[i]].label));
        g_object_set_data(G_OBJECT(item), "action", GINT_TO_POINTER(actions[i]));
        g_signal_connect_object(item, "activate", G_CALLBACK(action_clicked), win->window, 0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    GtkWidget *rename = gtk_menu_item_new_with_label(_("Rename tab…"));
    g_signal_connect_object(rename, "activate", G_CALLBACK(rename_clicked), win->window, 0);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), rename);
    GtkWidget *about = gtk_menu_item_new_with_label(_("About Galaxy Terminal"));
    g_signal_connect_object(about, "activate", G_CALLBACK(about_clicked), win->window, 0);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), about);
    gtk_widget_show_all(menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(menu_button), menu);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button);
    gtk_window_set_titlebar(GTK_WINDOW(win->window), header);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(win->window), box);
    win->settings_error = gtk_label_new(NULL);
    gtk_label_set_line_wrap(GTK_LABEL(win->settings_error), TRUE);
    gtk_label_set_selectable(GTK_LABEL(win->settings_error), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(win->settings_error), "galaxy-status");
    gtk_box_pack_start(GTK_BOX(box), win->settings_error, FALSE, FALSE, 0);
    win->search_revealer = gtk_revealer_new();
    GtkWidget *search = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(search), 6);
    gtk_container_add(GTK_CONTAINER(win->search_revealer), search);
    win->search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(win->search_entry), _("Search this tab’s scrollback"));
    gtk_box_pack_start(GTK_BOX(search), win->search_entry, TRUE, TRUE, 0);
    win->case_button = gtk_check_button_new_with_label(_("Match case"));
    gtk_box_pack_start(GTK_BOX(search), win->case_button, FALSE, FALSE, 0);
    win->search_status = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(search), win->search_status, FALSE, FALSE, 0);
    const char *icons[] = {"go-up-symbolic", "go-down-symbolic", "window-close-symbolic"};
    GCallback callbacks[] = {G_CALLBACK(search_previous), G_CALLBACK(search_next), G_CALLBACK(search_hide)};
    const char *tips[] = {N_("Previous match"), N_("Next match"), N_("Close search")};
    for (int i = 0; i < 3; i++) {
        GtkWidget *button = gtk_button_new_from_icon_name(icons[i], GTK_ICON_SIZE_BUTTON);
        gtk_widget_set_tooltip_text(button, _(tips[i]));
        gtk_box_pack_start(GTK_BOX(search), button, FALSE, FALSE, 0);
        g_signal_connect(button, "clicked", callbacks[i], win);
    }
    gtk_box_pack_start(GTK_BOX(box), win->search_revealer, FALSE, FALSE, 0);
    win->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(win->notebook), TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(win->notebook), FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(win->notebook), "galaxy-notebook");
    GtkWidget *plus = action_button(win, "list-add-symbolic", ACT_NEW_TAB);
    gtk_notebook_set_action_widget(GTK_NOTEBOOK(win->notebook), plus, GTK_PACK_END);
    gtk_widget_show(plus);
    gtk_box_pack_start(GTK_BOX(box), win->notebook, TRUE, TRUE, 0);
    g_signal_connect(win->window, "delete-event", G_CALLBACK(delete_event), win);
    g_signal_connect(win->window, "destroy", G_CALLBACK(window_destroyed), win);
    g_signal_connect_after(win->notebook, "switch-page", G_CALLBACK(switched), win);
    g_signal_connect(win->notebook, "page-removed", G_CALLBACK(page_removed), win);
    g_signal_connect(win->search_entry, "search-changed", G_CALLBACK(search_changed), win);
    g_signal_connect(win->search_entry, "activate", G_CALLBACK(search_next), win);
    g_signal_connect(win->search_entry, "key-press-event", G_CALLBACK(search_key), win);
    g_signal_connect(win->case_button, "toggled", G_CALLBACK(search_changed), win);
    gtk_widget_show_all(win->window);
    galaxy_window_refresh(win, GALAXY_CHANGE_ALL);
    return win;
}
