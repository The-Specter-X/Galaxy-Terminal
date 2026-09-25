#include "app.h"

#include <libxapp/xapp-preferences-window.h>
#include <gdk/gdkkeysyms.h>

typedef struct {
    GalaxyApp *app;
    GtkWidget *window;
    GtkWidget *profile_combo;
    GtkWidget *font;
    GtkWidget *palette;
    GtkWidget *foreground;
    GtkWidget *background;
    GtkWidget *opacity;
    GtkWidget *shell;
    GtkWidget *cwd;
    GtkWidget *ansi_grid;
    GtkWidget *ansi[16];
    GtkWidget *default_button;
    GtkWidget *remove_button;
    gboolean loading;
} Preferences;

static GtkWidget *section(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 20);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), box);
    g_object_set_data(G_OBJECT(scroll), "content", box);
    return scroll;
}

static GtkWidget *content(GtkWidget *scroll)
{
    return g_object_get_data(G_OBJECT(scroll), "content");
}

static GtkWidget *row(GtkWidget *box, const char *label, GtkWidget *control)
{
    GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *name = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(name), 0);
    gtk_widget_set_hexpand(name, TRUE);
    gtk_box_pack_start(GTK_BOX(line), name, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(line), control, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), line, FALSE, FALSE, 0);
    return line;
}

static GtkWidget *heading(GtkWidget *box, const char *title)
{
    GtkWidget *label = gtk_label_new(NULL);
    g_autofree char *markup = g_markup_printf_escaped("<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 5);
    return label;
}

static GalaxyProfile *selected_profile(Preferences *p)
{
    g_autofree char *name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(p->profile_combo));
    return galaxy_settings_profile(p->app->settings, name);
}

static void save(Preferences *p)
{
    if (!p->loading) galaxy_settings_save(p->app->settings);
}

static void profile_populate(Preferences *p, const char *selected)
{
    GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT(p->profile_combo);
    p->loading = TRUE;
    gtk_combo_box_text_remove_all(combo);
    GalaxySettings *s = p->app->settings;
    int index = 0;
    for (guint i = 0; i < s->profiles->len; ++i) {
        GalaxyProfile *profile = g_ptr_array_index(s->profiles, i);
        gtk_combo_box_text_append_text(combo, profile->name);
        if (g_strcmp0(selected, profile->name) == 0) index = i;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
    p->loading = FALSE;
}

static void on_profile_selected(GtkComboBox *combo, gpointer user_data)
{
    Preferences *p = user_data;
    (void)combo;
    if (p->loading) return;
    GalaxyProfile *profile = selected_profile(p);
    if (!profile) return;
    p->loading = TRUE;
    gtk_font_chooser_set_font(GTK_FONT_CHOOSER(p->font), profile->font);
    gtk_entry_set_text(GTK_ENTRY(p->shell), profile->shell);
    gtk_entry_set_text(GTK_ENTRY(p->cwd), profile->cwd);
    const char *names[] = {"System", "Dark", "Light", "Custom"};
    for (guint i = 0; i < G_N_ELEMENTS(names); ++i)
        if (g_strcmp0(profile->palette, names[i]) == 0)
            gtk_combo_box_set_active(GTK_COMBO_BOX(p->palette), i);
    GdkRGBA color;
    if (gdk_rgba_parse(&color, profile->foreground))
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(p->foreground), &color);
    if (gdk_rgba_parse(&color, profile->background))
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(p->background), &color);
    for (int i = 0; i < 16; ++i)
        if (gdk_rgba_parse(&color, profile->ansi[i]))
            gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(p->ansi[i]), &color);
    gtk_range_set_value(GTK_RANGE(p->opacity), profile->opacity * 100.0);
    gboolean custom = g_strcmp0(profile->palette, "Custom") == 0;
    gtk_widget_set_sensitive(p->foreground, custom);
    gtk_widget_set_sensitive(p->background, custom);
    gtk_widget_set_sensitive(p->ansi_grid, custom);
    gtk_widget_set_sensitive(p->remove_button, p->app->settings->profiles->len > 1);
    gtk_widget_set_sensitive(p->default_button,
        g_strcmp0(profile->name, p->app->settings->default_profile) != 0);
    p->loading = FALSE;
}

static void on_profile_add(GtkButton *button, gpointer user_data)
{
    Preferences *p = user_data;
    (void)button;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("New profile", GTK_WINDOW(p->window),
        GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Add", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Profile name");
    gtk_container_set_border_width(GTK_CONTAINER(entry), 12);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry);
    GtkWidget *error = gtk_label_new("");
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), error);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(entry);
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        g_autofree char *name = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))));
        GalaxyProfile *profile = galaxy_settings_add_profile(p->app->settings, name);
        if (profile) {
            profile_populate(p, name);
            on_profile_selected(NULL, p);
            save(p);
            break;
        }
        gtk_label_set_text(GTK_LABEL(error), "Enter a unique name without [ or ].");
    }
    gtk_widget_destroy(dialog);
}

static void on_profile_remove(GtkButton *button, gpointer user_data)
{
    Preferences *p = user_data;
    (void)button;
    GalaxyProfile *profile = selected_profile(p);
    if (!profile) return;
    g_autofree char *name = g_strdup(profile->name);
    galaxy_settings_remove_profile(p->app->settings, name);
    profile_populate(p, p->app->settings->default_profile);
    on_profile_selected(NULL, p);
    save(p);
}

static void on_profile_rename(GtkButton *button, gpointer user_data)
{
    Preferences *p = user_data;
    (void)button;
    GalaxyProfile *profile = selected_profile(p);
    if (!profile) return;
    g_autofree char *old_name = g_strdup(profile->name);
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Rename profile", GTK_WINDOW(p->window),
        GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Rename", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), old_name);
    gtk_container_set_border_width(GTK_CONTAINER(entry), 12);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry);
    GtkWidget *error = gtk_label_new("");
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), error);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(entry);
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        g_autofree char *name = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))));
        if (galaxy_settings_rename_profile(p->app->settings, old_name, name)) {
            for (GList *w = p->app->windows; w; w = w->next) {
                GalaxyWindow *win = w->data;
                GtkNotebook *book = GTK_NOTEBOOK(win->notebook);
                for (int i = 0; i < gtk_notebook_get_n_pages(book); ++i) {
                    GtkWidget *page = gtk_notebook_get_nth_page(book, i);
                    GalaxyTab *tab = g_object_get_data(G_OBJECT(page), "galaxy-tab");
                    if (g_strcmp0(tab->profile_name, old_name) != 0) continue;
                    g_free(tab->profile_name);
                    tab->profile_name = g_strdup(name);
                }
            }
            profile_populate(p, name);
            on_profile_selected(NULL, p);
            save(p);
            break;
        }
        gtk_label_set_text(GTK_LABEL(error), "Enter a unique name without [ or ].");
    }
    gtk_widget_destroy(dialog);
}

static void on_profile_default(GtkButton *button, gpointer user_data)
{
    Preferences *p = user_data;
    (void)button;
    GalaxyProfile *profile = selected_profile(p);
    if (!profile) return;
    g_free(p->app->settings->default_profile);
    p->app->settings->default_profile = g_strdup(profile->name);
    on_profile_selected(NULL, p);
    save(p);
}

static void on_font_changed(GtkFontButton *button, gpointer user_data)
{
    Preferences *p = user_data;
    if (p->loading) return;
    GalaxyProfile *profile = selected_profile(p);
    if (!profile) return;
    g_free(profile->font);
    profile->font = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(button));
    save(p);
}

static void on_palette_changed(GtkComboBox *combo, gpointer user_data)
{
    Preferences *p = user_data;
    if (p->loading) return;
    GalaxyProfile *profile = selected_profile(p);
    if (!profile) return;
    g_free(profile->palette);
    profile->palette = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    gboolean custom = g_strcmp0(profile->palette, "Custom") == 0;
    gtk_widget_set_sensitive(p->foreground, custom);
    gtk_widget_set_sensitive(p->background, custom);
    gtk_widget_set_sensitive(p->ansi_grid, custom);
    save(p);
}

static void on_color_changed(GtkColorButton *button, gpointer user_data)
{
    Preferences *p = user_data;
    if (p->loading) return;
    GalaxyProfile *profile = selected_profile(p);
    if (!profile) return;
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
    char **target = GTK_WIDGET(button) == p->foreground
        ? &profile->foreground : &profile->background;
    g_free(*target);
    *target = gdk_rgba_to_string(&rgba);
    save(p);
}

static void on_opacity_changed(GtkRange *range, gpointer user_data)
{
    Preferences *p = user_data;
    if (p->loading) return;
    GalaxyProfile *profile = selected_profile(p);
    if (profile) {
        profile->opacity = gtk_range_get_value(range) / 100.0;
        save(p);
    }
}

static void on_ansi_changed(GtkColorButton *button, gpointer user_data)
{
    Preferences *p = user_data;
    if (p->loading) return;
    GalaxyProfile *profile = selected_profile(p);
    if (!profile) return;
    int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "ansi-index"));
    GdkRGBA color;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &color);
    g_free(profile->ansi[index]);
    profile->ansi[index] = gdk_rgba_to_string(&color);
    save(p);
}

static void on_shell_changed(GtkEditable *editable, gpointer user_data)
{
    Preferences *p = user_data;
    if (p->loading) return;
    GalaxyProfile *profile = selected_profile(p);
    if (!profile) return;
    g_free(profile->shell);
    profile->shell = g_strdup(gtk_entry_get_text(GTK_ENTRY(editable)));
    save(p);
}

static void on_cwd_changed(GtkEditable *editable, gpointer user_data)
{
    Preferences *p = user_data;
    if (p->loading) return;
    GalaxyProfile *profile = selected_profile(p);
    if (!profile) return;
    g_free(profile->cwd);
    profile->cwd = g_strdup(gtk_entry_get_text(GTK_ENTRY(editable)));
    save(p);
}

static void on_bool_changed(GtkSwitch *button, GParamSpec *pspec, gpointer user_data)
{
    Preferences *p = g_object_get_data(G_OBJECT(button), "preferences");
    gboolean *value = user_data;
    (void)pspec;
    if (p->loading) return;
    *value = gtk_switch_get_active(button);
    save(p);
}

static void boolean_row(Preferences *p, GtkWidget *box, const char *name,
                        gboolean *value)
{
    GtkWidget *button = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(button), *value);
    g_object_set_data(G_OBJECT(button), "preferences", p);
    g_signal_connect(button, "notify::active", G_CALLBACK(on_bool_changed), value);
    row(box, name, button);
}

static void on_scrollback_changed(GtkSpinButton *spin, gpointer user_data)
{
    Preferences *p = user_data;
    if (p->loading) return;
    p->app->settings->scrollback = gtk_spin_button_get_value_as_int(spin);
    save(p);
}

static void on_unlimited_toggled(GtkToggleButton *button, gpointer data)
{
    GtkWidget *spin = g_object_get_data(G_OBJECT(button), "spin");
    Preferences *p = data;
    if (p->loading) return;
    gboolean unlimited = gtk_toggle_button_get_active(button);
    gtk_widget_set_sensitive(spin, !unlimited);
    p->app->settings->scrollback = unlimited ? -1 : gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
    save(p);
}

static GtkWidget *build_general(Preferences *p)
{
    GtkWidget *page = section();
    GtkWidget *box = content(page);
    GalaxySettings *s = p->app->settings;
    heading(box, "Tabs and interaction");
    boolean_row(p, box, "Always show the tab bar", &s->show_tabs);
    boolean_row(p, box, "Copy mouse selection to clipboard", &s->auto_copy);
    boolean_row(p, box, "Confirm closing a running command", &s->confirm_close);
    boolean_row(p, box, "Show scrollbar", &s->show_scrollbar);
    boolean_row(p, box, "Hide mouse pointer while typing", &s->mouse_autohide);
    heading(box, "Appearance");
    boolean_row(p, box, "Follow desktop dark mode", &s->follow_dark);
    heading(box, "Scrollback");
    GtkWidget *spin = gtk_spin_button_new_with_range(0, 1000000, 1000);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), s->scrollback < 0 ? 10000 : s->scrollback);
    row(box, "Lines retained in each tab", spin);
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_scrollback_changed), p);
    GtkWidget *unlimited = gtk_check_button_new_with_label("Unlimited scrollback (uses more resources)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(unlimited), s->scrollback == -1);
    gtk_widget_set_sensitive(spin, s->scrollback != -1);
    g_object_set_data(G_OBJECT(unlimited), "spin", spin);
    g_signal_connect(unlimited, "toggled", G_CALLBACK(on_unlimited_toggled), p);
    gtk_box_pack_start(GTK_BOX(box), unlimited, FALSE, FALSE, 0);
    return page;
}

static GtkWidget *build_profiles(Preferences *p)
{
    GtkWidget *page = section();
    GtkWidget *box = content(page);
    heading(box, "Terminal profiles");
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(box), controls, FALSE, FALSE, 0);
    p->profile_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(p->profile_combo, TRUE);
    gtk_box_pack_start(GTK_BOX(controls), p->profile_combo, TRUE, TRUE, 0);
    GtkWidget *add = gtk_button_new_with_label("Add");
    GtkWidget *rename = gtk_button_new_with_label("Rename");
    p->remove_button = gtk_button_new_with_label("Remove");
    gtk_box_pack_start(GTK_BOX(controls), add, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), rename, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), p->remove_button, FALSE, FALSE, 0);
    p->default_button = gtk_button_new_with_label("Make default");
    gtk_box_pack_start(GTK_BOX(box), p->default_button, FALSE, FALSE, 0);
    heading(box, "Text and colors");
    p->font = gtk_font_button_new();
    row(box, "Font", p->font);
    p->palette = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->palette), "System");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->palette), "Dark");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->palette), "Light");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->palette), "Custom");
    row(box, "Color palette", p->palette);
    p->foreground = gtk_color_button_new();
    p->background = gtk_color_button_new();
    row(box, "Custom text color", p->foreground);
    row(box, "Custom background color", p->background);
    p->ansi_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(p->ansi_grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(p->ansi_grid), 4);
    for (int i = 0; i < 16; ++i) {
        p->ansi[i] = gtk_color_button_new();
        gtk_widget_set_size_request(p->ansi[i], 48, 30);
        g_autofree char *tip = g_strdup_printf("ANSI color %d", i);
        gtk_widget_set_tooltip_text(p->ansi[i], tip);
        g_object_set_data(G_OBJECT(p->ansi[i]), "ansi-index", GINT_TO_POINTER(i));
        gtk_grid_attach(GTK_GRID(p->ansi_grid), p->ansi[i], i % 4, i / 4, 1, 1);
        g_signal_connect(p->ansi[i], "color-set", G_CALLBACK(on_ansi_changed), p);
    }
    row(box, "Custom ANSI colors", p->ansi_grid);
    p->opacity = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 25, 100, 1);
    gtk_widget_set_size_request(p->opacity, 210, -1);
    gtk_scale_set_digits(GTK_SCALE(p->opacity), 0);
    row(box, "Background opacity (%)", p->opacity);
    heading(box, "Shell");
    p->shell = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(p->shell), "System login shell");
    gtk_widget_set_tooltip_text(p->shell, "Executable path for new tabs; empty uses the account's shell");
    row(box, "Shell executable", p->shell);
    p->cwd = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(p->cwd), "Inherit launch directory");
    gtk_widget_set_tooltip_text(p->cwd, "Absolute directory for new windows; tabs opened with + inherit the active tab's directory");
    row(box, "Starting directory", p->cwd);
    g_signal_connect(p->profile_combo, "changed", G_CALLBACK(on_profile_selected), p);
    g_signal_connect(add, "clicked", G_CALLBACK(on_profile_add), p);
    g_signal_connect(rename, "clicked", G_CALLBACK(on_profile_rename), p);
    g_signal_connect(p->remove_button, "clicked", G_CALLBACK(on_profile_remove), p);
    g_signal_connect(p->default_button, "clicked", G_CALLBACK(on_profile_default), p);
    g_signal_connect(p->font, "font-set", G_CALLBACK(on_font_changed), p);
    g_signal_connect(p->palette, "changed", G_CALLBACK(on_palette_changed), p);
    g_signal_connect(p->foreground, "color-set", G_CALLBACK(on_color_changed), p);
    g_signal_connect(p->background, "color-set", G_CALLBACK(on_color_changed), p);
    g_signal_connect(p->opacity, "value-changed", G_CALLBACK(on_opacity_changed), p);
    g_signal_connect(p->shell, "changed", G_CALLBACK(on_shell_changed), p);
    g_signal_connect(p->cwd, "changed", G_CALLBACK(on_cwd_changed), p);
    profile_populate(p, p->app->settings->default_profile);
    on_profile_selected(NULL, p);
    return page;
}

typedef struct {
    Preferences *preferences;
    GtkWidget *dialog;
    GtkWidget *message;
    GalaxyAction action;
    char *result;
} ShortcutCapture;

static gboolean on_shortcut_key(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    ShortcutCapture *capture = user_data;
    (void)widget;
    if (event->keyval == GDK_KEY_Escape) {
        gtk_dialog_response(GTK_DIALOG(capture->dialog), GTK_RESPONSE_CANCEL);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_BackSpace) {
        capture->result = g_strdup("");
        gtk_dialog_response(GTK_DIALOG(capture->dialog), GTK_RESPONSE_ACCEPT);
        return TRUE;
    }
    GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask();
    if (!gtk_accelerator_valid(event->keyval, mods)) {
        gtk_label_set_text(GTK_LABEL(capture->message), "Use a key with Ctrl, Alt, Shift, or F1–F12.");
        return TRUE;
    }
    for (int i = 0; i < ACT_COUNT; ++i) {
        if (i == (int)capture->action) continue;
        guint key;
        GdkModifierType existing;
        gtk_accelerator_parse(capture->preferences->app->settings->shortcuts[i], &key, &existing);
        if (key == event->keyval && existing == mods) {
            gtk_label_set_text(GTK_LABEL(capture->message), "That shortcut is already assigned.");
            return TRUE;
        }
    }
    capture->result = gtk_accelerator_name(event->keyval, mods);
    gtk_dialog_response(GTK_DIALOG(capture->dialog), GTK_RESPONSE_ACCEPT);
    return TRUE;
}

static void on_shortcut_clicked(GtkButton *button, gpointer user_data)
{
    Preferences *p = user_data;
    GalaxyAction action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action"));
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Set shortcut", GTK_WINDOW(p->window),
        GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 350, 130);
    GtkWidget *instructions = gtk_label_new("Press a new shortcut. Backspace clears it; Escape cancels.");
    gtk_container_set_border_width(GTK_CONTAINER(instructions), 16);
    GtkWidget *message = gtk_label_new("");
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), instructions);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), message);
    ShortcutCapture capture = {p, dialog, message, action, NULL};
    g_signal_connect(dialog, "key-press-event", G_CALLBACK(on_shortcut_key), &capture);
    gtk_widget_show_all(dialog);
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response == GTK_RESPONSE_ACCEPT && capture.result) {
        GalaxySettings *s = p->app->settings;
        g_free(s->shortcuts[action]);
        s->shortcuts[action] = capture.result;
        guint key = 0;
        GdkModifierType modifiers = 0;
        gtk_accelerator_parse(capture.result, &key, &modifiers);
        g_autofree char *label = key ? gtk_accelerator_get_label(key, modifiers) : g_strdup("Disabled");
        gtk_button_set_label(button, label);
        save(p);
    } else g_free(capture.result);
}

static GtkWidget *build_shortcuts(Preferences *p)
{
    GtkWidget *page = section();
    GtkWidget *box = content(page);
    heading(box, "Keyboard shortcuts");
    for (int i = 0; i < ACT_COUNT; ++i) {
        guint key = 0;
        GdkModifierType modifiers = 0;
        gtk_accelerator_parse(p->app->settings->shortcuts[i], &key, &modifiers);
        g_autofree char *label = key ? gtk_accelerator_get_label(key, modifiers) : g_strdup("Disabled");
        GtkWidget *button = gtk_button_new_with_label(label);
        gtk_widget_set_size_request(button, 145, -1);
        g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(i));
        g_signal_connect(button, "clicked", G_CALLBACK(on_shortcut_clicked), p);
        row(box, galaxy_action_labels[i], button);
    }
    return page;
}

static void on_preferences_destroy(GtkWidget *widget, gpointer data)
{
    Preferences *p = data;
    (void)widget;
    p->app->preferences = NULL;
    g_free(p);
}

void galaxy_preferences_show(GalaxyApp *app, GtkWindow *parent)
{
    if (app->preferences) {
        gtk_window_present(GTK_WINDOW(app->preferences));
        return;
    }
    Preferences *p = g_new0(Preferences, 1);
    p->app = app;
    p->window = GTK_WIDGET(xapp_preferences_window_new());
    app->preferences = p->window;
    gtk_window_set_title(GTK_WINDOW(p->window), "Galaxy Terminal Preferences");
    gtk_window_set_default_size(GTK_WINDOW(p->window), 700, 540);
    gtk_window_set_transient_for(GTK_WINDOW(p->window), parent);
    gtk_application_add_window(app->application, GTK_WINDOW(p->window));
    XAppPreferencesWindow *prefs = XAPP_PREFERENCES_WINDOW(p->window);
    xapp_preferences_window_add_page(prefs, build_general(p), "general", "General");
    xapp_preferences_window_add_page(prefs, build_profiles(p), "profiles", "Profiles");
    xapp_preferences_window_add_page(prefs, build_shortcuts(p), "shortcuts", "Shortcuts");
    g_signal_connect(p->window, "destroy", G_CALLBACK(on_preferences_destroy), p);
    gtk_widget_show_all(p->window);
}
