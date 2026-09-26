#include "app.h"
#include <glib/gi18n.h>
#include <libxapp/xapp-preferences-window.h>
#include <stddef.h>

typedef enum { FIELD_BOOL, FIELD_INT, FIELD_TEXT, FIELD_FONT, FIELD_COLOR,
               FIELD_OPACITY, FIELD_PALETTE, FIELD_SHAPE, FIELD_BLINK } FieldType;
typedef struct { const char *label; FieldType type; size_t offset; const char *initial; } Field;
typedef struct _Preferences Preferences;
typedef struct { Preferences *prefs; const Field *field; GtkWidget *widget; gboolean profile; } Binding;
struct _Preferences {
    GalaxyApp *app;
    GtkWidget *window, *profile_combo, *error, *validation, *preview;
    GtkWidget *remove_button, *default_button, *shortcuts[ACT_COUNT];
    GPtrArray *bindings;
    gboolean loading, editing;
};
#define GENERAL(label, type, member, initial) {label, type, offsetof(GalaxySettings, member), initial}
#define PROFILE(label, type, member, initial) {label, type, offsetof(GalaxyProfile, member), initial}
static const Field general_fields[] = {
    GENERAL(N_("Always show the tab bar"), FIELD_BOOL, show_tabs, "0"),
    GENERAL(N_("Copy mouse selection to clipboard"), FIELD_BOOL, auto_copy, "0"),
    GENERAL(N_("Confirm closing a running command"), FIELD_BOOL, confirm_close, "1"),
    GENERAL(N_("Show scrollbar"), FIELD_BOOL, show_scrollbar, "1"),
    GENERAL(N_("Hide mouse pointer while typing"), FIELD_BOOL, mouse_autohide, "0"),
    GENERAL(N_("Scrollback lines (−1 means unlimited)"), FIELD_INT, scrollback, "10000")
};
static const Field profile_fields[] = {
    PROFILE(N_("Font"), FIELD_FONT, font, "Monospace 11"),
    PROFILE(N_("Color palette"), FIELD_PALETTE, palette, "Dark"),
    PROFILE(N_("Custom text color"), FIELD_COLOR, foreground, "#ebedf4"),
    PROFILE(N_("Custom background color"), FIELD_COLOR, background, "#191b24"),
    PROFILE(N_("Background opacity (%)"), FIELD_OPACITY, opacity, "1"),
    PROFILE(N_("Cursor shape"), FIELD_SHAPE, cursor_shape, "0"),
    PROFILE(N_("Cursor blinking"), FIELD_BLINK, cursor_blink, "0"),
    PROFILE(N_("Audible bell"), FIELD_BOOL, audible_bell, "0"),
    PROFILE(N_("Scroll to bottom on output"), FIELD_BOOL, scroll_on_output, "0"),
    PROFILE(N_("Scroll to bottom on typing"), FIELD_BOOL, scroll_on_keystroke, "1"),
    PROFILE(N_("Shell executable (empty uses login shell)"), FIELD_TEXT, shell, ""),
    PROFILE(N_("Starting directory (empty inherits)"), FIELD_TEXT, cwd, "")
};
static const char *const palette_ids[] = {"Dark", "Light", "System", "Custom"};
static const char *const palette_labels[] = {N_("Dark"), N_("Light"), N_("Follow desktop"), N_("Custom")};

static void margin(GtkWidget *widget, int size)
{
    gtk_widget_set_margin_start(widget, size); gtk_widget_set_margin_end(widget, size);
    gtk_widget_set_margin_top(widget, size); gtk_widget_set_margin_bottom(widget, size);
}
static GtkWidget *section(GtkWidget **box)
{
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    margin(*box, 20);
    gtk_container_add(GTK_CONTAINER(scroll), *box);
    return scroll;
}
static GtkWidget *label(GtkWidget *box, const char *text)
{
    GtkWidget *widget = gtk_label_new(text);
    gtk_label_set_line_wrap(GTK_LABEL(widget), TRUE);
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 0);
    return widget;
}
static GtkWidget *row(GtkWidget *box, const char *text, GtkWidget *control)
{
    GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *name = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(name), 0);
    gtk_label_set_line_wrap(GTK_LABEL(name), TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(name), control);
    gtk_box_pack_start(GTK_BOX(line), name, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(line), control, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), line, FALSE, FALSE, 0);
    return line;
}
static GalaxyProfile *selected(Preferences *p)
{
    return galaxy_settings_profile(p->app->settings,
        gtk_combo_box_get_active_id(GTK_COMBO_BOX(p->profile_combo)));
}
static gpointer field_address(Binding *binding)
{
    gpointer base = binding->profile ? (gpointer)selected(binding->prefs) : (gpointer)binding->prefs->app->settings;
    return base ? (char *)base + binding->field->offset : NULL;
}
static void preview(Preferences *p)
{
    GalaxyProfile *profile = selected(p);
    if (profile) galaxy_terminal_apply_profile(VTE_TERMINAL(p->preview), profile);
}
static void save(Preferences *p, guint changes)
{
    p->editing = TRUE;
    galaxy_settings_changed(p->app->settings, changes);
    p->editing = FALSE;
    preview(p);
}
static void populate(Preferences *p, const char *name)
{
    p->loading = TRUE;
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(p->profile_combo));
    GalaxySettings *s = p->app->settings;
    for (guint i = 0; i < s->profiles->len; i++) {
        GalaxyProfile *profile = s->profiles->pdata[i];
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(p->profile_combo), profile->name, profile->name);
    }
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(p->profile_combo), name))
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(p->profile_combo), s->default_profile);
    p->loading = FALSE;
}
static void binding_refresh(Binding *binding)
{
    gpointer address = field_address(binding);
    if (!address) return;
    GtkWidget *widget = binding->widget;
    switch (binding->field->type) {
    case FIELD_BOOL: gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), *(gboolean *)address); break;
    case FIELD_INT: gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), *(int *)address); break;
    case FIELD_TEXT: {
        gtk_entry_set_text(GTK_ENTRY(widget), *(char **)address);
        GalaxyProfile trial = *selected(binding->prefs);
        trial.shell = binding->field->offset == offsetof(GalaxyProfile, shell) ? *(char **)address : "";
        trial.cwd = binding->field->offset == offsetof(GalaxyProfile, cwd) ? *(char **)address : "";
        g_autofree char *problem = NULL;
        if (galaxy_profile_validate_command(&trial, &problem))
            gtk_style_context_remove_class(gtk_widget_get_style_context(widget), "error");
        else gtk_style_context_add_class(gtk_widget_get_style_context(widget), "error");
        gtk_widget_set_tooltip_text(widget, problem);
        break;
    }
    case FIELD_FONT: gtk_font_chooser_set_font(GTK_FONT_CHOOSER(widget), *(char **)address); break;
    case FIELD_COLOR: {
        GdkRGBA color;
        if (gdk_rgba_parse(&color, *(char **)address)) gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(widget), &color);
        break;
    }
    case FIELD_OPACITY: gtk_range_set_value(GTK_RANGE(widget), *(double *)address * 100); break;
    case FIELD_PALETTE: gtk_combo_box_set_active_id(GTK_COMBO_BOX(widget), *(char **)address); break;
    case FIELD_SHAPE: case FIELD_BLINK: gtk_combo_box_set_active(GTK_COMBO_BOX(widget), *(int *)address); break;
    }
}
static void refresh_controls(Preferences *p)
{
    p->loading = TRUE;
    for (guint i = 0; i < p->bindings->len; i++) binding_refresh(p->bindings->pdata[i]);
    for (int i = 0; i < ACT_COUNT; i++) {
        guint key = 0; GdkModifierType mods = 0;
        galaxy_shortcut_parse(p->app->settings->shortcuts[i], &key, &mods);
        g_autofree char *name = key ? gtk_accelerator_get_label(key, mods) : g_strdup(_("Disabled"));
        gtk_button_set_label(GTK_BUTTON(p->shortcuts[i]), name);
    }
    GalaxyProfile *profile = selected(p);
    gtk_widget_set_sensitive(p->remove_button, p->app->settings->profiles->len > 1);
    gtk_widget_set_sensitive(p->default_button, profile && g_strcmp0(profile->name, p->app->settings->default_profile));
    g_autofree char *problem = NULL;
    if (profile) galaxy_profile_validate_command(profile, &problem);
    gtk_label_set_text(GTK_LABEL(p->validation), problem ? problem : "");
    p->loading = FALSE;
    preview(p);
}
void galaxy_preferences_refresh(GalaxyApp *app, guint changes)
{
    if (!app->preferences) return;
    Preferences *p = g_object_get_data(G_OBJECT(app->preferences), "preferences");
    if (!p) return;
    if (changes & GALAXY_CHANGE_STATUS)
        gtk_label_set_text(GTK_LABEL(p->error), app->settings->error ? app->settings->error : "");
    if (p->editing) return;
    if (changes & (GALAXY_CHANGE_PROFILES | GALAXY_CHANGE_RELOAD)) {
        g_autofree char *name = g_strdup(gtk_combo_box_get_active_id(GTK_COMBO_BOX(p->profile_combo)));
        populate(p, name);
    }
    if (changes & ~GALAXY_CHANGE_STATUS) refresh_controls(p);
}
static void profile_selected(GtkComboBox *combo, gpointer data)
{
    (void)combo;
    Preferences *p = data;
    if (!p->loading) refresh_controls(p);
}
static void field_changed(GtkWidget *widget, gpointer data)
{
    Binding *binding = data;
    Preferences *p = binding->prefs;
    if (p->loading) return;
    gpointer address = field_address(binding);
    if (!address) return;
    char *value = NULL;
    switch (binding->field->type) {
    case FIELD_BOOL: *(gboolean *)address = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)); break;
    case FIELD_INT: *(int *)address = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget)); break;
    case FIELD_TEXT: {
        value = g_strdup(gtk_entry_get_text(GTK_ENTRY(widget)));
        GalaxyProfile trial = *selected(p);
        /* Validate this field separately so either of two broken fields can be repaired. */
        trial.shell = binding->field->offset == offsetof(GalaxyProfile, shell) ? value : "";
        trial.cwd = binding->field->offset == offsetof(GalaxyProfile, cwd) ? value : "";
        g_autofree char *problem = NULL;
        if (!galaxy_profile_validate_command(&trial, &problem)) {
            gtk_label_set_text(GTK_LABEL(p->validation), problem);
            gtk_style_context_add_class(gtk_widget_get_style_context(widget), "error");
            gtk_widget_set_tooltip_text(widget, problem);
            g_free(value);
            return;
        }
        gtk_label_set_text(GTK_LABEL(p->validation), "");
        gtk_style_context_remove_class(gtk_widget_get_style_context(widget), "error");
        gtk_widget_set_tooltip_text(widget, NULL);
        break;
    }
    case FIELD_FONT: {
        value = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(widget));
        g_autoptr(PangoFontDescription) font = pango_font_description_from_string(value);
        int size = pango_font_description_get_size(font);
        if (size < 0 || size > 256 * PANGO_SCALE) {
            gtk_label_set_text(GTK_LABEL(p->validation), _("Choose a font size no larger than 256."));
            g_free(value);
            return;
        }
        gtk_label_set_text(GTK_LABEL(p->validation), "");
        break;
    }
    case FIELD_COLOR: {
        GdkRGBA color; gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(widget), &color);
        value = gdk_rgba_to_string(&color); break;
    }
    case FIELD_OPACITY: *(double *)address = gtk_range_get_value(GTK_RANGE(widget)) / 100; break;
    case FIELD_PALETTE: value = g_strdup(gtk_combo_box_get_active_id(GTK_COMBO_BOX(widget))); break;
    case FIELD_SHAPE: case FIELD_BLINK: *(int *)address = gtk_combo_box_get_active(GTK_COMBO_BOX(widget)); break;
    }
    if (value) { g_free(*(char **)address); *(char **)address = value; }
    save(p, binding->profile ? GALAXY_CHANGE_COLORS : GALAXY_CHANGE_BEHAVIOR);
}
static void reset_field(GtkButton *button, gpointer data)
{
    (void)button;
    Binding *binding = data;
    gpointer address = field_address(binding);
    if (!address) return;
    const char *value = binding->field->initial;
    switch (binding->field->type) {
    case FIELD_BOOL: case FIELD_INT: case FIELD_SHAPE: case FIELD_BLINK: *(int *)address = atoi(value); break;
    case FIELD_OPACITY: *(double *)address = g_ascii_strtod(value, NULL); break;
    default: g_free(*(char **)address); *(char **)address = g_strdup(value);
    }
    save(binding->prefs, binding->profile ? GALAXY_CHANGE_COLORS : GALAXY_CHANGE_BEHAVIOR);
    refresh_controls(binding->prefs);
}
static void add_field(Preferences *p, GtkWidget *box, const Field *field, gboolean profile)
{
    Binding *binding = g_new0(Binding, 1);
    binding->prefs = p; binding->field = field; binding->profile = profile;
    const char *signal = "changed";
    GtkWidget *widget = NULL;
    switch (field->type) {
    case FIELD_BOOL: widget = gtk_check_button_new(); signal = "toggled"; break;
    case FIELD_INT: widget = gtk_spin_button_new_with_range(-1, 1000000, 1); signal = "value-changed"; break;
    case FIELD_TEXT: widget = gtk_entry_new(); break;
    case FIELD_FONT: widget = gtk_font_button_new(); signal = "font-set"; break;
    case FIELD_COLOR: widget = gtk_color_button_new(); signal = "color-set"; break;
    case FIELD_OPACITY:
        widget = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
        gtk_widget_set_size_request(widget, 180, -1);
        gtk_scale_set_digits(GTK_SCALE(widget), 0);
        signal = "value-changed"; break;
    case FIELD_PALETTE:
        widget = gtk_combo_box_text_new();
        for (guint i = 0; i < G_N_ELEMENTS(palette_ids); i++)
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget), palette_ids[i], _(palette_labels[i]));
        break;
    case FIELD_SHAPE: case FIELD_BLINK: {
        const char *shapes[] = {N_("Block"), N_("I-beam"), N_("Underline")};
        const char *blink[] = {N_("Follow desktop"), N_("On"), N_("Off")};
        widget = gtk_combo_box_text_new();
        for (int i = 0; i < 3; i++)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widget), _(field->type == FIELD_SHAPE ? shapes[i] : blink[i]));
        break;
    }
    }
    binding->widget = widget;
    g_ptr_array_add(p->bindings, binding);
    GtkWidget *line = row(box, _(field->label), widget);
    GtkWidget *reset = gtk_button_new_from_icon_name("edit-undo-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_tooltip_text(reset, _("Restore this setting’s default"));
    gtk_box_pack_end(GTK_BOX(line), reset, FALSE, FALSE, 0);
    g_signal_connect(reset, "clicked", G_CALLBACK(reset_field), binding);
    g_signal_connect(widget, signal, G_CALLBACK(field_changed), binding);
}
static void reset_general(GtkButton *button, gpointer data)
{
    (void)button; Preferences *p = data;
    galaxy_settings_reset_general(p->app->settings);
    save(p, GALAXY_CHANGE_BEHAVIOR); refresh_controls(p);
}
static void retry_save(GtkButton *button, gpointer data)
{
    (void)button; Preferences *p = data;
    galaxy_settings_flush(p->app->settings);
}
static GtkWidget *build_general(Preferences *p)
{
    GtkWidget *box, *page = section(&box);
    label(box, _("Settings apply immediately. Changes are saved after a short pause."));
    p->error = label(box, "");
    gtk_label_set_selectable(GTK_LABEL(p->error), TRUE);
    GtkWidget *retry = gtk_button_new_with_label(_("Retry saving settings"));
    gtk_box_pack_start(GTK_BOX(box), retry, FALSE, FALSE, 0);
    g_signal_connect(retry, "clicked", G_CALLBACK(retry_save), p);
    for (guint i = 0; i < G_N_ELEMENTS(general_fields); i++) add_field(p, box, &general_fields[i], FALSE);
    label(box, _("Search covers retained terminal output, not the shell history file. Unlimited scrollback can consume disk space and memory."));
    label(box, _("Application appearance follows the desktop. Terminal colors are configured per profile."));
    GtkWidget *reset = gtk_button_new_with_label(_("Restore general defaults"));
    gtk_box_pack_start(GTK_BOX(box), reset, FALSE, FALSE, 0);
    g_signal_connect(reset, "clicked", G_CALLBACK(reset_general), p);
    return page;
}

static void name_response(GtkDialog *dialog, int response, GtkWidget *window)
{
    Preferences *p = g_object_get_data(G_OBJECT(window), "preferences");
    if (response != GTK_RESPONSE_ACCEPT) { gtk_widget_destroy(GTK_WIDGET(dialog)); return; }
    GtkWidget *entry = g_object_get_data(G_OBJECT(dialog), "entry");
    GtkWidget *error = g_object_get_data(G_OBJECT(dialog), "error");
    const char *source = g_object_get_data(G_OBJECT(dialog), "source");
    int mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dialog), "mode"));
    g_autofree char *name = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))));
    GalaxySettings *s = p->app->settings;
    gboolean success = mode == 1 ? galaxy_settings_rename_profile(s, source, name) :
        (mode == 2 ? galaxy_settings_duplicate_profile(s, source, name) != NULL :
                     galaxy_settings_add_profile(s, name) != NULL);
    if (!success) {
        gtk_label_set_text(GTK_LABEL(error), _("Use a unique name of at most 100 bytes, without brackets or line breaks. The source profile must still exist."));
        return;
    }
    if (mode == 1) {
        for (GList *node = p->app->windows; node; node = node->next) {
            GalaxyWindow *win = node->data;
            int count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(win->notebook));
            for (int i = 0; i < count; i++) {
                GalaxyTab *tab = g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(GTK_NOTEBOOK(win->notebook), i)), "galaxy-tab");
                if (!g_strcmp0(tab->profile_name, source)) {
                    g_free(tab->profile_name); tab->profile_name = g_strdup(name);
                }
            }
        }
    }
    populate(p, name); save(p, GALAXY_CHANGE_PROFILES); refresh_controls(p);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}
static void profile_name_dialog(GtkButton *button, gpointer data)
{
    Preferences *p = data;
    int mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "mode"));
    GalaxyProfile *profile = selected(p);
    if (mode && !profile) return;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Profile name"), GTK_WINDOW(p->window),
        GTK_DIALOG_DESTROY_WITH_PARENT, _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Apply"), GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    margin(box, 16);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 100);
    if (mode == 1) gtk_entry_set_text(GTK_ENTRY(entry), profile->name);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_container_add(GTK_CONTAINER(box), entry);
    GtkWidget *error = label(box, "");
    g_object_set_data(G_OBJECT(dialog), "entry", entry);
    g_object_set_data(G_OBJECT(dialog), "error", error);
    g_object_set_data(G_OBJECT(dialog), "mode", GINT_TO_POINTER(mode));
    g_object_set_data_full(G_OBJECT(dialog), "source", g_strdup(profile ? profile->name : NULL), g_free);
    g_signal_connect_object(dialog, "response", G_CALLBACK(name_response), p->window, 0);
    gtk_widget_show_all(dialog); gtk_widget_grab_focus(entry);
}
static void profile_remove(GtkButton *button, gpointer data)
{
    (void)button; Preferences *p = data;
    GalaxyProfile *profile = selected(p);
    if (profile) galaxy_settings_remove_profile(p->app->settings, profile->name);
    populate(p, p->app->settings->default_profile);
    save(p, GALAXY_CHANGE_PROFILES); refresh_controls(p);
}
static void profile_default(GtkButton *button, gpointer data)
{
    (void)button; Preferences *p = data;
    GalaxyProfile *profile = selected(p);
    if (!profile) return;
    g_free(p->app->settings->default_profile);
    p->app->settings->default_profile = g_strdup(profile->name);
    save(p, GALAXY_CHANGE_PROFILES); refresh_controls(p);
}
static void profile_reset(GtkButton *button, gpointer data)
{
    (void)button; Preferences *p = data;
    GalaxyProfile *profile = selected(p);
    if (profile) galaxy_profile_reset(profile);
    save(p, GALAXY_CHANGE_COLORS); refresh_controls(p);
}
static GtkWidget *build_profiles(Preferences *p)
{
    GtkWidget *box, *page = section(&box);
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    p->profile_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(box), p->profile_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
    const char *names[] = {N_("Add"), N_("Rename"), N_("Duplicate")};
    for (int i = 0; i < 3; i++) {
        GtkWidget *button = gtk_button_new_with_label(_(names[i]));
        g_object_set_data(G_OBJECT(button), "mode", GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(buttons), button, FALSE, FALSE, 0);
        g_signal_connect(button, "clicked", G_CALLBACK(profile_name_dialog), p);
    }
    p->remove_button = gtk_button_new_with_label(_("Remove"));
    p->default_button = gtk_button_new_with_label(_("Make default"));
    gtk_box_pack_start(GTK_BOX(buttons), p->remove_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), p->default_button, FALSE, FALSE, 0);
    label(box, _("Removing a profile moves its open tabs to the default profile. Shell and directory changes affect future launches."));
    p->validation = label(box, "");
    gtk_style_context_add_class(gtk_widget_get_style_context(p->validation), "galaxy-error");
    p->preview = vte_terminal_new();
    vte_terminal_set_input_enabled(VTE_TERMINAL(p->preview), FALSE);
    vte_terminal_set_size(VTE_TERMINAL(p->preview), 48, 4);
    gtk_widget_set_size_request(p->preview, -1, 110);
    gtk_widget_set_can_focus(p->preview, FALSE);
    gtk_box_pack_start(GTK_BOX(box), p->preview, FALSE, FALSE, 0);
    vte_terminal_feed(VTE_TERMINAL(p->preview), "Galaxy Terminal  λ  café  日本語  🌌\r\n\033[32muser@galaxy\033[0m:\033[34m~/Projects\033[0m $ echo Hello\r\n\033[31mRed \033[33mYellow \033[36mCyan \033[1mBold\033[0m", -1);
    for (guint i = 0; i < G_N_ELEMENTS(profile_fields); i++) add_field(p, box, &profile_fields[i], TRUE);
    label(box, _("Custom colors apply when the Custom palette is selected. Opacity changes only the terminal background; blur is controlled by the compositor."));
    static const char *const colors[] = {"#20232c", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#dcdfe4", "#5b616e", "#ff7b86", "#b3dd91", "#f5d492", "#80c2fb", "#dc9cf1", "#7dd3db", "#ffffff"};
    label(box, _("Custom ANSI colors"));
    GtkWidget *color_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(color_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(color_grid), 12);
    gtk_box_pack_start(GTK_BOX(box), color_grid, FALSE, FALSE, 0);
    for (int i = 0; i < 16; i++) {
        GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_grid_attach(GTK_GRID(color_grid), cell, i % 4, i / 4, 1, 1);
        Field *field = g_new0(Field, 1);
        field->label = g_strdup_printf("%d", i);
        field->type = FIELD_COLOR; field->offset = offsetof(GalaxyProfile, ansi) + i * sizeof(char *);
        field->initial = colors[i];
        add_field(p, cell, field, TRUE);
        g_autofree char *tip = g_strdup_printf(_("Custom ANSI color %d"), i);
        gtk_widget_set_tooltip_text(cell, tip);
        /* Dynamic field metadata lives as long as the binding's widget. */
        Binding *binding = g_ptr_array_index(p->bindings, p->bindings->len - 1);
        g_object_set_data_full(G_OBJECT(binding->widget), "field-label", (gpointer)field->label, g_free);
        g_object_set_data_full(G_OBJECT(binding->widget), "field", field, g_free);
    }
    GtkWidget *reset = gtk_button_new_with_label(_("Restore this profile’s defaults"));
    gtk_box_pack_start(GTK_BOX(box), reset, FALSE, FALSE, 0);
    g_signal_connect(reset, "clicked", G_CALLBACK(profile_reset), p);
    g_signal_connect(p->remove_button, "clicked", G_CALLBACK(profile_remove), p);
    g_signal_connect(p->default_button, "clicked", G_CALLBACK(profile_default), p);
    g_signal_connect(p->profile_combo, "changed", G_CALLBACK(profile_selected), p);
    return page;
}

static gboolean shortcut_available(Preferences *p, int action, const char *value)
{
    guint key; GdkModifierType mods;
    if (!galaxy_shortcut_parse(value, &key, &mods)) return FALSE;
    if (!key) return TRUE;
    for (int i = 0; i < ACT_COUNT; i++) {
        guint existing_key; GdkModifierType existing_mods;
        if (i != action && galaxy_shortcut_parse(p->app->settings->shortcuts[i], &existing_key, &existing_mods) &&
            key == existing_key && mods == existing_mods) return FALSE;
    }
    return TRUE;
}
static gboolean shortcut_key(GtkWidget *dialog, GdkEventKey *event, GtkWidget *window)
{
    Preferences *p = g_object_get_data(G_OBJECT(window), "preferences");
    if (event->keyval == GDK_KEY_Escape) { gtk_widget_destroy(dialog); return TRUE; }
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dialog), "action"));
    g_autofree char *value = event->keyval == GDK_KEY_BackSpace ? g_strdup("") : galaxy_shortcut_capture(event);
    GtkWidget *message = g_object_get_data(G_OBJECT(dialog), "message");
    if (!value || !shortcut_available(p, action, value)) {
        gtk_label_set_text(GTK_LABEL(message), _("Use an unassigned Ctrl, Alt, Super or function-key shortcut."));
        return TRUE;
    }
    g_free(p->app->settings->shortcuts[action]);
    p->app->settings->shortcuts[action] = g_steal_pointer(&value);
    save(p, GALAXY_CHANGE_SHORTCUTS); refresh_controls(p);
    gtk_widget_destroy(dialog);
    return TRUE;
}
static void shortcut_clicked(GtkButton *button, gpointer data)
{
    Preferences *p = data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Set shortcut"), GTK_WINDOW(p->window),
        GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL, _("Cancel"), GTK_RESPONSE_CANCEL, NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog)); margin(box, 16);
    label(box, _("Press a shortcut. Backspace disables it; Escape cancels."));
    GtkWidget *message = label(box, "");
    g_object_set_data(G_OBJECT(dialog), "action", g_object_get_data(G_OBJECT(button), "action"));
    g_object_set_data(G_OBJECT(dialog), "message", message);
    g_signal_connect_object(dialog, "key-press-event", G_CALLBACK(shortcut_key), p->window, 0);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
    gtk_widget_show_all(dialog);
}
static void shortcut_reset(GtkButton *button, gpointer data)
{
    Preferences *p = data;
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action"));
    const char *value = galaxy_shortcuts[action].accelerator;
    if (!shortcut_available(p, action, value)) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(p->window), GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", _("The default shortcut is assigned elsewhere. Clear that assignment or restore all shortcut defaults."));
        g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
        gtk_widget_show(dialog);
        return;
    }
    g_free(p->app->settings->shortcuts[action]); p->app->settings->shortcuts[action] = g_strdup(value);
    save(p, GALAXY_CHANGE_SHORTCUTS); refresh_controls(p);
}
static void shortcuts_reset(GtkButton *button, gpointer data)
{
    (void)button; Preferences *p = data;
    galaxy_settings_reset_shortcuts(p->app->settings);
    save(p, GALAXY_CHANGE_SHORTCUTS); refresh_controls(p);
}
static GtkWidget *build_shortcuts(Preferences *p)
{
    GtkWidget *box, *page = section(&box);
    label(box, _("Ctrl + uses the plus key, including Shift when your keyboard layout needs it. Ctrl+Left/Right can be assigned here, but normally belong to shell word navigation."));
    for (int i = 0; i < ACT_COUNT; i++) {
        GtkWidget *button = gtk_button_new(); p->shortcuts[i] = button;
        gtk_widget_set_name(button, galaxy_shortcuts[i].name);
        gtk_widget_set_size_request(button, 150, -1);
        g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(i));
        GtkWidget *line = row(box, _(galaxy_shortcuts[i].label), button);
        GtkWidget *reset = gtk_button_new_from_icon_name("edit-undo-symbolic", GTK_ICON_SIZE_MENU);
        gtk_widget_set_tooltip_text(reset, _("Restore this shortcut’s default"));
        g_object_set_data(G_OBJECT(reset), "action", GINT_TO_POINTER(i));
        gtk_box_pack_end(GTK_BOX(line), reset, FALSE, FALSE, 0);
        g_signal_connect(button, "clicked", G_CALLBACK(shortcut_clicked), p);
        g_signal_connect(reset, "clicked", G_CALLBACK(shortcut_reset), p);
    }
    GtkWidget *reset = gtk_button_new_with_label(_("Restore all shortcut defaults"));
    gtk_box_pack_start(GTK_BOX(box), reset, FALSE, FALSE, 0);
    g_signal_connect(reset, "clicked", G_CALLBACK(shortcuts_reset), p);
    return page;
}
static void preferences_destroyed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    Preferences *p = data;
    p->loading = TRUE;
    p->app->preferences = NULL;
}
static void preferences_free(gpointer data)
{
    Preferences *p = data;
    g_ptr_array_unref(p->bindings); g_free(p);
}
void galaxy_preferences_show(GalaxyApp *app, GtkWindow *parent)
{
    if (app->preferences) { gtk_window_present(GTK_WINDOW(app->preferences)); return; }
    Preferences *p = g_new0(Preferences, 1);
    p->app = app; p->bindings = g_ptr_array_new_with_free_func(g_free);
    p->window = GTK_WIDGET(xapp_preferences_window_new());
    gtk_window_set_title(GTK_WINDOW(p->window), _("Galaxy Terminal Preferences"));
    gtk_window_set_default_size(GTK_WINDOW(p->window), 780, 640);
    gtk_window_set_transient_for(GTK_WINDOW(p->window), parent);
    gtk_window_set_application(GTK_WINDOW(p->window), app->application);
    XAppPreferencesWindow *window = XAPP_PREFERENCES_WINDOW(p->window);
    xapp_preferences_window_add_page(window, build_general(p), "general", _("General"));
    xapp_preferences_window_add_page(window, build_profiles(p), "profiles", _("Profiles"));
    xapp_preferences_window_add_page(window, build_shortcuts(p), "shortcuts", _("Shortcuts"));
    g_object_set_data_full(G_OBJECT(p->window), "preferences", p, preferences_free);
    g_signal_connect(p->window, "destroy", G_CALLBACK(preferences_destroyed), p);
    app->preferences = p->window;
    populate(p, app->settings->default_profile);
    gtk_widget_show_all(p->window);
    galaxy_preferences_refresh(app, GALAXY_CHANGE_ALL);
}
