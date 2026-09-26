#include "settings.h"
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

static const char *const ansi_defaults[16] = {
    "#20232c", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#dcdfe4",
    "#5b616e", "#ff7b86", "#b3dd91", "#f5d492", "#80c2fb", "#dc9cf1", "#7dd3db", "#ffffff"
};

static void profile_clear(GalaxyProfile *p)
{
    g_free(p->shell); g_free(p->cwd); g_free(p->font); g_free(p->palette);
    g_free(p->foreground); g_free(p->background);
    for (int i = 0; i < 16; i++) g_free(p->ansi[i]);
}

static void profile_free(gpointer data)
{
    GalaxyProfile *p = data;
    profile_clear(p);
    g_free(p->name);
    g_free(p);
}

void galaxy_profile_reset(GalaxyProfile *p)
{
    profile_clear(p);
    p->shell = g_strdup("");
    p->cwd = g_strdup("");
    p->font = g_strdup("Monospace 11");
    p->palette = g_strdup("Dark");
    p->foreground = g_strdup("#ebedf4");
    p->background = g_strdup("#191b24");
    for (int i = 0; i < 16; i++) p->ansi[i] = g_strdup(ansi_defaults[i]);
    p->opacity = 1.0;
    p->cursor_shape = 0;
    p->cursor_blink = 0;
    p->audible_bell = FALSE;
    p->scroll_on_output = FALSE;
    p->scroll_on_keystroke = TRUE;
}

static gboolean valid_name(const char *name)
{
    return name && *name && g_utf8_validate(name, -1, NULL) &&
        !strpbrk(name, "[]\r\n") && strlen(name) <= 100;
}

GalaxyProfile *galaxy_settings_profile(GalaxySettings *s, const char *name)
{
    if (!name) return NULL;
    for (guint i = 0; i < s->profiles->len; i++) {
        GalaxyProfile *p = g_ptr_array_index(s->profiles, i);
        if (!g_strcmp0(name, p->name)) return p;
    }
    return NULL;
}

GalaxyProfile *galaxy_settings_add_profile(GalaxySettings *s, const char *name)
{
    if (!valid_name(name) || galaxy_settings_profile(s, name)) return NULL;
    GalaxyProfile *p = g_new0(GalaxyProfile, 1);
    p->name = g_strdup(name);
    galaxy_profile_reset(p);
    g_ptr_array_add(s->profiles, p);
    return p;
}

GalaxyProfile *galaxy_settings_duplicate_profile(GalaxySettings *s,
                                                const char *source, const char *name)
{
    GalaxyProfile *from = galaxy_settings_profile(s, source);
    if (!from) return NULL;
    GalaxyProfile *p = galaxy_settings_add_profile(s, name);
    if (!p) return NULL;
    profile_clear(p);
    p->shell = g_strdup(from->shell); p->cwd = g_strdup(from->cwd);
    p->font = g_strdup(from->font); p->palette = g_strdup(from->palette);
    p->foreground = g_strdup(from->foreground); p->background = g_strdup(from->background);
    for (int i = 0; i < 16; i++) p->ansi[i] = g_strdup(from->ansi[i]);
    p->opacity = from->opacity;
    p->cursor_shape = from->cursor_shape; p->cursor_blink = from->cursor_blink;
    p->audible_bell = from->audible_bell;
    p->scroll_on_output = from->scroll_on_output;
    p->scroll_on_keystroke = from->scroll_on_keystroke;
    return p;
}

gboolean galaxy_settings_rename_profile(GalaxySettings *s,
                                        const char *old_name, const char *new_name)
{
    GalaxyProfile *p = galaxy_settings_profile(s, old_name);
    if (!p || !valid_name(new_name)) return FALSE;
    if (!g_strcmp0(old_name, new_name)) return TRUE;
    if (galaxy_settings_profile(s, new_name)) return FALSE;
    /* Callers may pass strings owned by this model. Copy before changing it. */
    g_autofree char *replacement = g_strdup(new_name);
    if (!g_strcmp0(s->default_profile, old_name)) {
        g_free(s->default_profile);
        s->default_profile = g_strdup(replacement);
    }
    g_free(p->name); p->name = g_steal_pointer(&replacement);
    return TRUE;
}

void galaxy_settings_remove_profile(GalaxySettings *s, const char *name)
{
    if (s->profiles->len < 2) return;
    g_autofree char *owned_name = g_strdup(name);
    for (guint i = 0; i < s->profiles->len; i++) {
        GalaxyProfile *p = g_ptr_array_index(s->profiles, i);
        if (g_strcmp0(p->name, owned_name)) continue;
        g_ptr_array_remove_index(s->profiles, i);
        if (!g_strcmp0(s->default_profile, owned_name)) {
            g_free(s->default_profile);
            s->default_profile = g_strdup(((GalaxyProfile *)s->profiles->pdata[0])->name);
        }
        return;
    }
}

gboolean galaxy_profile_validate_command(const GalaxyProfile *p, char **message)
{
    if (*p->shell) {
        g_autofree char *program = g_find_program_in_path(p->shell);
        if (!program || g_file_test(program, G_FILE_TEST_IS_DIR)) {
            *message = g_strdup(_("Choose an executable shell path or leave it empty."));
            return FALSE;
        }
    }
    if (*p->cwd && (!g_path_is_absolute(p->cwd) ||
        !g_file_test(p->cwd, G_FILE_TEST_IS_DIR) || g_access(p->cwd, X_OK))) {
        *message = g_strdup(_("Choose an accessible absolute starting directory or leave it empty."));
        return FALSE;
    }
    return TRUE;
}

void galaxy_settings_reset_general(GalaxySettings *s)
{
    s->scrollback = 10000;
    s->auto_copy = FALSE; s->confirm_close = TRUE;
    s->show_tabs = FALSE; s->show_scrollbar = TRUE;
    s->mouse_autohide = FALSE;
}

void galaxy_settings_reset_shortcuts(GalaxySettings *s)
{
    for (int i = 0; i < ACT_COUNT; i++) {
        g_free(s->shortcuts[i]);
        s->shortcuts[i] = g_strdup(galaxy_shortcuts[i].accelerator);
    }
}

static void model_init(GalaxySettings *s)
{
    s->profiles = g_ptr_array_new_with_free_func(profile_free);
    s->default_profile = g_strdup("Default");
    galaxy_settings_add_profile(s, "Default");
    galaxy_settings_reset_general(s);
    galaxy_settings_reset_shortcuts(s);
}

static void model_clear(GalaxySettings *s)
{
    g_ptr_array_unref(s->profiles);
    g_free(s->default_profile);
    for (int i = 0; i < ACT_COUNT; i++) g_free(s->shortcuts[i]);
}

static void invalid(GString *warnings, const char *group, const char *field)
{
    if (warnings->len) g_string_append(warnings, ", ");
    g_string_append_printf(warnings, "%s/%s", group, field);
}

static int integer(GKeyFile *k, const char *group, const char *field,
                   int fallback, int minimum, int maximum, GString *warnings)
{
    if (!g_key_file_has_key(k, group, field, NULL)) return fallback;
    g_autoptr(GError) error = NULL;
    int result = g_key_file_get_integer(k, group, field, &error);
    if (error || result < minimum || result > maximum) {
        invalid(warnings, group, field);
        return fallback;
    }
    return result;
}

static gboolean boolean(GKeyFile *k, const char *group, const char *field,
                         gboolean fallback, GString *warnings)
{
    if (!g_key_file_has_key(k, group, field, NULL)) return fallback;
    g_autoptr(GError) error = NULL;
    gboolean result = g_key_file_get_boolean(k, group, field, &error);
    if (error) { invalid(warnings, group, field); return fallback; }
    return result;
}

static void string_field(GKeyFile *k, const char *group, const char *field,
                         char **target, gboolean empty, GString *warnings)
{
    if (!g_key_file_has_key(k, group, field, NULL)) return;
    g_autoptr(GError) error = NULL;
    char *text = g_key_file_get_string(k, group, field, &error);
    if (error || !text || (!empty && !*text) || !g_utf8_validate(text, -1, NULL)) {
        invalid(warnings, group, field); g_free(text); return;
    }
    g_free(*target); *target = text;
}

static void color_field(GKeyFile *k, const char *group, const char *field,
                        char **target, GString *warnings)
{
    if (!g_key_file_has_key(k, group, field, NULL)) return;
    g_autofree char *text = g_key_file_get_string(k, group, field, NULL);
    GdkRGBA color;
    if (!text || !gdk_rgba_parse(&color, text) || !isfinite(color.red) ||
        !isfinite(color.green) || !isfinite(color.blue) || !isfinite(color.alpha)) {
        invalid(warnings, group, field); return;
    }
    g_free(*target); *target = g_strdup(text);
}

static gboolean parse_model(GalaxySettings *s, const char *text, gsize length)
{
    g_autoptr(GKeyFile) key = g_key_file_new();
    g_autoptr(GError) error = NULL;
    if (!g_key_file_load_from_data(key, text, length, G_KEY_FILE_NONE, &error)) {
        g_free(s->error);
        s->error = g_strdup_printf(_("Configuration could not be loaded; keeping valid settings: %s"),
                                   error->message);
        return FALSE;
    }
    GalaxySettings next = {0};
    model_init(&next);
    g_autoptr(GString) warnings = g_string_new(NULL);
    next.scrollback = integer(key, "General", "ScrollbackLines", 10000, -1, 1000000, warnings);
    next.auto_copy = boolean(key, "General", "CopyOnSelect", FALSE, warnings);
    next.confirm_close = boolean(key, "General", "ConfirmClose", TRUE, warnings);
    next.show_tabs = boolean(key, "General", "AlwaysShowTabs", FALSE, warnings);
    next.show_scrollbar = boolean(key, "General", "ShowScrollbar", TRUE, warnings);
    next.mouse_autohide = boolean(key, "General", "HidePointerWhileTyping", FALSE, warnings);
    string_field(key, "General", "DefaultProfile", &next.default_profile, FALSE, warnings);
    for (int i = 0; i < ACT_COUNT; i++) {
        g_autofree char *value = g_key_file_get_string(key, "Shortcuts", galaxy_shortcuts[i].name, NULL);
        if (!value && !g_key_file_has_key(key, "Shortcuts", galaxy_shortcuts[i].name, NULL)) continue;
        guint keyval; GdkModifierType mods;
        if (!galaxy_shortcut_parse(value, &keyval, &mods)) {
            invalid(warnings, "Shortcuts", galaxy_shortcuts[i].name); continue;
        }
        g_free(next.shortcuts[i]); next.shortcuts[i] = g_strdup(value);
    }
    /* A duplicate binding is disabled deterministically, never silently shadowed. */
    for (int i = 0; i < ACT_COUNT; i++) {
        guint keyval; GdkModifierType mods;
        galaxy_shortcut_parse(next.shortcuts[i], &keyval, &mods);
        if (!keyval) continue;
        for (int j = 0; j < i; j++) {
            guint previous; GdkModifierType previous_mods;
            galaxy_shortcut_parse(next.shortcuts[j], &previous, &previous_mods);
            if (previous == keyval && previous_mods == mods) {
                invalid(warnings, "Shortcuts", galaxy_shortcuts[i].name);
                g_free(next.shortcuts[i]); next.shortcuts[i] = g_strdup("");
                break;
            }
        }
    }
    g_auto(GStrv) groups = g_key_file_get_groups(key, NULL);
    g_ptr_array_set_size(next.profiles, 0);
    for (int i = 0; groups[i]; i++) {
        if (!g_str_has_prefix(groups[i], "Profile ")) continue;
        GalaxyProfile *p = galaxy_settings_add_profile(&next, groups[i] + 8);
        if (!p) { invalid(warnings, groups[i], "name"); continue; }
        string_field(key, groups[i], "Shell", &p->shell, TRUE, warnings);
        string_field(key, groups[i], "WorkingDirectory", &p->cwd, TRUE, warnings);
        string_field(key, groups[i], "Font", &p->font, FALSE, warnings);
        string_field(key, groups[i], "Palette", &p->palette, FALSE, warnings);
        if (g_strcmp0(p->palette, "Dark") && g_strcmp0(p->palette, "Light") &&
            g_strcmp0(p->palette, "System") && g_strcmp0(p->palette, "Custom")) {
            invalid(warnings, groups[i], "Palette");
            g_free(p->palette); p->palette = g_strdup("Dark");
        }
        color_field(key, groups[i], "Foreground", &p->foreground, warnings);
        color_field(key, groups[i], "Background", &p->background, warnings);
        for (int j = 0; j < 16; j++) {
            g_autofree char *field = g_strdup_printf("AnsiColor%d", j);
            color_field(key, groups[i], field, &p->ansi[j], warnings);
        }
        if (g_key_file_has_key(key, groups[i], "Opacity", NULL)) {
            double value = g_key_file_get_double(key, groups[i], "Opacity", &error);
            if (error || !isfinite(value) || value < 0.0 || value > 1.0)
                invalid(warnings, groups[i], "Opacity");
            else p->opacity = value;
            g_clear_error(&error);
        }
        p->cursor_shape = integer(key, groups[i], "CursorShape", 0, 0, 2, warnings);
        p->cursor_blink = integer(key, groups[i], "CursorBlink", 0, 0, 2, warnings);
        p->audible_bell = boolean(key, groups[i], "AudibleBell", FALSE, warnings);
        p->scroll_on_output = boolean(key, groups[i], "ScrollOnOutput", FALSE, warnings);
        p->scroll_on_keystroke = boolean(key, groups[i], "ScrollOnKeystroke", TRUE, warnings);
    }
    if (!next.profiles->len) galaxy_settings_add_profile(&next, "Default");
    if (!galaxy_settings_profile(&next, next.default_profile)) {
        invalid(warnings, "General", "DefaultProfile");
        g_free(next.default_profile);
        next.default_profile = g_strdup(((GalaxyProfile *)next.profiles->pdata[0])->name);
    }
    model_clear(s);
    s->profiles = next.profiles; s->default_profile = next.default_profile;
    for (int i = 0; i < ACT_COUNT; i++) s->shortcuts[i] = next.shortcuts[i];
    s->scrollback = next.scrollback; s->auto_copy = next.auto_copy;
    s->confirm_close = next.confirm_close; s->show_tabs = next.show_tabs;
    s->show_scrollbar = next.show_scrollbar; s->mouse_autohide = next.mouse_autohide;
    g_free(s->contents); s->contents = g_strndup(text, length);
    g_free(s->error);
    s->error = warnings->len
        ? g_strdup_printf(_("Invalid values were replaced with safe defaults: %s"), warnings->str) : NULL;
    return TRUE;
}

static void notify(GalaxySettings *s, guint changes)
{
    if (s->changed) s->changed(s, changes, s->user_data);
}

static char *serialize(GalaxySettings *s, gsize *length)
{
    g_autoptr(GKeyFile) k = g_key_file_new();
    g_key_file_set_string(k, "General", "DefaultProfile", s->default_profile);
    g_key_file_set_integer(k, "General", "ScrollbackLines", s->scrollback);
    g_key_file_set_boolean(k, "General", "CopyOnSelect", s->auto_copy);
    g_key_file_set_boolean(k, "General", "ConfirmClose", s->confirm_close);
    g_key_file_set_boolean(k, "General", "AlwaysShowTabs", s->show_tabs);
    g_key_file_set_boolean(k, "General", "ShowScrollbar", s->show_scrollbar);
    g_key_file_set_boolean(k, "General", "HidePointerWhileTyping", s->mouse_autohide);
    for (int i = 0; i < ACT_COUNT; i++)
        g_key_file_set_string(k, "Shortcuts", galaxy_shortcuts[i].name, s->shortcuts[i]);
    for (guint i = 0; i < s->profiles->len; i++) {
        GalaxyProfile *p = s->profiles->pdata[i];
        g_autofree char *group = g_strconcat("Profile ", p->name, NULL);
        g_key_file_set_string(k, group, "Shell", p->shell);
        g_key_file_set_string(k, group, "WorkingDirectory", p->cwd);
        g_key_file_set_string(k, group, "Font", p->font);
        g_key_file_set_string(k, group, "Palette", p->palette);
        g_key_file_set_string(k, group, "Foreground", p->foreground);
        g_key_file_set_string(k, group, "Background", p->background);
        for (int j = 0; j < 16; j++) {
            g_autofree char *field = g_strdup_printf("AnsiColor%d", j);
            g_key_file_set_string(k, group, field, p->ansi[j]);
        }
        g_key_file_set_double(k, group, "Opacity", p->opacity);
        g_key_file_set_integer(k, group, "CursorShape", p->cursor_shape);
        g_key_file_set_integer(k, group, "CursorBlink", p->cursor_blink);
        g_key_file_set_boolean(k, group, "AudibleBell", p->audible_bell);
        g_key_file_set_boolean(k, group, "ScrollOnOutput", p->scroll_on_output);
        g_key_file_set_boolean(k, group, "ScrollOnKeystroke", p->scroll_on_keystroke);
    }
    return g_key_file_to_data(k, length, NULL);
}

gboolean galaxy_settings_flush(GalaxySettings *s)
{
    if (s->save_source) { g_source_remove(s->save_source); s->save_source = 0; }
    if (!s->dirty) return TRUE;
    gsize length;
    g_autofree char *text = serialize(s, &length);
    g_autoptr(GError) error = NULL;
    if (!g_file_set_contents_full(s->path, text, length,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, &error)) {
        g_free(s->error);
        s->error = g_strdup_printf(_("Settings could not be saved: %s"), error->message);
        notify(s, GALAXY_CHANGE_STATUS);
        return FALSE;
    }
    s->dirty = FALSE;
    g_free(s->contents); s->contents = g_strdup(text);
    g_clear_pointer(&s->error, g_free);
    notify(s, GALAXY_CHANGE_STATUS);
    return TRUE;
}

static gboolean save_later(gpointer data)
{
    GalaxySettings *s = data;
    s->save_source = 0;
    galaxy_settings_flush(s);
    return G_SOURCE_REMOVE;
}

void galaxy_settings_changed(GalaxySettings *s, guint changes)
{
    s->dirty = TRUE;
    if (s->save_source) g_source_remove(s->save_source);
    s->save_source = g_timeout_add(350, save_later, s);
    notify(s, changes);
}

gboolean galaxy_settings_reload(GalaxySettings *s)
{
    g_autofree char *text = NULL;
    gsize length;
    g_autoptr(GError) error = NULL;
    if (!g_file_get_contents(s->path, &text, &length, &error)) {
        if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_free(s->error); s->error = g_strdup(error->message);
            notify(s, GALAXY_CHANGE_STATUS);
        }
        return FALSE;
    }
    if (!g_strcmp0(text, s->contents)) return TRUE;
    /* Explicit external edits win over pending GUI writes. */
    if (s->save_source) { g_source_remove(s->save_source); s->save_source = 0; }
    s->dirty = FALSE;
    gboolean success = parse_model(s, text, length);
    notify(s, success ? GALAXY_CHANGE_ALL : GALAXY_CHANGE_STATUS);
    return success;
}

static gboolean reload_later(gpointer data)
{
    GalaxySettings *s = data;
    s->reload_source = 0;
    galaxy_settings_reload(s);
    return G_SOURCE_REMOVE;
}

static void on_directory_changed(GFileMonitor *monitor, GFile *file, GFile *other,
                                 GFileMonitorEvent event, gpointer data)
{
    GalaxySettings *s = data;
    (void)monitor; (void)other;
    if (event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
        event != G_FILE_MONITOR_EVENT_CREATED && event != G_FILE_MONITOR_EVENT_MOVED_IN &&
        event != G_FILE_MONITOR_EVENT_RENAMED) return;
    g_autofree char *path = g_file_get_path(file);
    g_autofree char *other_path = other ? g_file_get_path(other) : NULL;
    if (g_strcmp0(path, s->path) && g_strcmp0(other_path, s->path)) return;
    if (s->reload_source) g_source_remove(s->reload_source);
    s->reload_source = g_timeout_add(150, reload_later, s);
}

GalaxySettings *galaxy_settings_new(void)
{
    GalaxySettings *s = g_new0(GalaxySettings, 1);
    model_init(s);
    s->path = g_build_filename(g_get_user_config_dir(), "galaxy-terminal", "settings.ini", NULL);
    g_autofree char *dir = g_path_get_dirname(s->path);
    if (g_mkdir_with_parents(dir, 0700))
        s->error = g_strdup(_("The settings directory could not be created."));
    galaxy_settings_reload(s);
    g_autoptr(GFile) folder = g_file_new_for_path(dir);
    s->monitor = g_file_monitor_directory(folder, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
    if (s->monitor) g_signal_connect(s->monitor, "changed", G_CALLBACK(on_directory_changed), s);
    return s;
}

void galaxy_settings_set_changed(GalaxySettings *s,
    void (*changed)(GalaxySettings *, guint, gpointer), gpointer data)
{
    s->changed = changed; s->user_data = data;
}

void galaxy_settings_free(GalaxySettings *s)
{
    if (!s) return;
    s->changed = NULL;
    if (!galaxy_settings_flush(s) && s->error) g_printerr("%s\n", s->error);
    if (s->reload_source) g_source_remove(s->reload_source);
    g_clear_object(&s->monitor);
    model_clear(s);
    g_free(s->path); g_free(s->contents); g_free(s->error); g_free(s);
}
