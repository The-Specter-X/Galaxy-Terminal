#include "settings.h"

#include <glib/gstdio.h>

const char *const galaxy_action_names[ACT_COUNT] = {
    "NewTab", "NewWindow", "CloseTab", "Copy", "Paste", "Find",
    "NextTab", "PreviousTab", "ZoomIn", "ZoomOut", "ZoomReset",
    "Preferences", "Fullscreen", "Tab1", "Tab2", "Tab3", "Tab4",
    "Tab5", "Tab6", "Tab7", "Tab8", "Tab9"
};

const char *const galaxy_action_labels[ACT_COUNT] = {
    "New tab", "New window", "Close tab", "Copy", "Paste", "Find",
    "Next tab", "Previous tab", "Zoom in", "Zoom out", "Reset zoom",
    "Preferences", "Fullscreen", "Switch to tab 1", "Switch to tab 2",
    "Switch to tab 3", "Switch to tab 4", "Switch to tab 5",
    "Switch to tab 6", "Switch to tab 7", "Switch to tab 8", "Switch to tab 9"
};

static const char *const default_shortcuts[ACT_COUNT] = {
    "<Control><Shift>t", "<Control><Shift>n", "<Control><Shift>w",
    "<Control><Shift>c", "<Control><Shift>v", "<Control><Shift>f",
    "<Control>Page_Down", "<Control>Page_Up", "<Control>plus",
    "<Control>minus", "<Control>0", "<Control>comma", "F11",
    "<Alt>1", "<Alt>2", "<Alt>3", "<Alt>4", "<Alt>5",
    "<Alt>6", "<Alt>7", "<Alt>8", "<Alt>9"
};

static const char *const default_ansi[16] = {
    "#20232c", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#dcdfe4",
    "#5b616e", "#ff7b86", "#b3dd91", "#f5d492", "#80c2fb", "#dc9cf1", "#7dd3db", "#ffffff"
};

static void profile_free(gpointer data)
{
    GalaxyProfile *p = data;
    g_free(p->name);
    g_free(p->shell);
    g_free(p->cwd);
    g_free(p->font);
    g_free(p->palette);
    g_free(p->foreground);
    g_free(p->background);
    for (int i = 0; i < 16; ++i) g_free(p->ansi[i]);
    g_free(p);
}

static char *get_string(GKeyFile *key, const char *group, const char *field,
                        const char *fallback)
{
    char *value = g_key_file_get_string(key, group, field, NULL);
    if (!value || !*value) {
        g_free(value);
        return g_strdup(fallback);
    }
    return value;
}

static gboolean get_boolean(GKeyFile *key, const char *group, const char *field,
                            gboolean fallback)
{
    return g_key_file_has_key(key, group, field, NULL)
        ? g_key_file_get_boolean(key, group, field, NULL) : fallback;
}

static int get_integer(GKeyFile *key, const char *group, const char *field,
                       int fallback)
{
    return g_key_file_has_key(key, group, field, NULL)
        ? g_key_file_get_integer(key, group, field, NULL) : fallback;
}

static void load_settings(GalaxySettings *s, const char *data, gsize length)
{
    g_autoptr(GKeyFile) key = g_key_file_new();
    if (data && !g_key_file_load_from_data(key, data, length, G_KEY_FILE_NONE, NULL)) {
        g_warning("Invalid Galaxy Terminal configuration; using defaults");
        return;
    }

    g_ptr_array_set_size(s->profiles, 0);
    g_clear_pointer(&s->default_profile, g_free);
    s->default_profile = get_string(key, "General", "DefaultProfile", "Default");
    s->scrollback = CLAMP(get_integer(key, "General", "ScrollbackLines", 10000), -1, 1000000);
    s->auto_copy = get_boolean(key, "General", "CopyOnSelect", FALSE);
    s->confirm_close = get_boolean(key, "General", "ConfirmClose", TRUE);
    s->show_tabs = get_boolean(key, "General", "AlwaysShowTabs", TRUE);
    s->show_scrollbar = get_boolean(key, "General", "ShowScrollbar", TRUE);
    s->mouse_autohide = get_boolean(key, "General", "HidePointerWhileTyping", FALSE);
    s->follow_dark = get_boolean(key, "General", "FollowDarkMode", TRUE);

    for (int i = 0; i < ACT_COUNT; ++i) {
        g_free(s->shortcuts[i]);
        s->shortcuts[i] = g_key_file_has_key(key, "Shortcuts", galaxy_action_names[i], NULL)
            ? g_key_file_get_string(key, "Shortcuts", galaxy_action_names[i], NULL)
            : g_strdup(default_shortcuts[i]);
    }

    gsize count = 0;
    g_auto(GStrv) groups = g_key_file_get_groups(key, &count);
    for (gsize i = 0; i < count; ++i) {
        if (!g_str_has_prefix(groups[i], "Profile ")) continue;
        const char *name = groups[i] + strlen("Profile ");
        if (!*name) continue;
        GalaxyProfile *p = g_new0(GalaxyProfile, 1);
        p->name = g_strdup(name);
        p->shell = get_string(key, groups[i], "Shell", "");
        p->cwd = get_string(key, groups[i], "WorkingDirectory", "");
        p->font = get_string(key, groups[i], "Font", "Monospace 11");
        p->palette = get_string(key, groups[i], "Palette", "System");
        p->foreground = get_string(key, groups[i], "Foreground", "#ebedf4");
        p->background = get_string(key, groups[i], "Background", "#191b24");
        for (int j = 0; j < 16; ++j) {
            g_autofree char *field = g_strdup_printf("AnsiColor%d", j);
            p->ansi[j] = get_string(key, groups[i], field, default_ansi[j]);
        }
        p->opacity = g_key_file_has_key(key, groups[i], "Opacity", NULL)
            ? g_key_file_get_double(key, groups[i], "Opacity", NULL) : 1.0;
        p->opacity = CLAMP(p->opacity, 0.25, 1.0);
        g_ptr_array_add(s->profiles, p);
    }
    if (!s->profiles->len) galaxy_settings_add_profile(s, "Default");
    if (!galaxy_settings_profile(s, s->default_profile)) {
        g_free(s->default_profile);
        s->default_profile = g_strdup(((GalaxyProfile *)s->profiles->pdata[0])->name);
    }
    g_free(s->contents);
    s->contents = g_strndup(data ? data : "", data ? length : 0);
}

GalaxyProfile *galaxy_settings_profile(GalaxySettings *s, const char *name)
{
    if (!name) return NULL;
    for (guint i = 0; i < s->profiles->len; ++i) {
        GalaxyProfile *p = g_ptr_array_index(s->profiles, i);
        if (g_strcmp0(p->name, name) == 0) return p;
    }
    return NULL;
}

GalaxyProfile *galaxy_settings_add_profile(GalaxySettings *s, const char *name)
{
    if (!name || !*name || strchr(name, '[') || strchr(name, ']') ||
        strchr(name, '\n') || galaxy_settings_profile(s, name)) return NULL;
    GalaxyProfile *p = g_new0(GalaxyProfile, 1);
    p->name = g_strdup(name);
    p->shell = g_strdup("");
    p->cwd = g_strdup("");
    p->font = g_strdup("Monospace 11");
    p->palette = g_strdup("System");
    p->foreground = g_strdup("#ebedf4");
    p->background = g_strdup("#191b24");
    for (int i = 0; i < 16; ++i) p->ansi[i] = g_strdup(default_ansi[i]);
    p->opacity = 1.0;
    g_ptr_array_add(s->profiles, p);
    return p;
}

gboolean galaxy_settings_rename_profile(GalaxySettings *s, const char *old_name,
                                        const char *new_name)
{
    GalaxyProfile *profile = galaxy_settings_profile(s, old_name);
    if (!profile || !new_name || !*new_name || strchr(new_name, '[') ||
        strchr(new_name, ']') || strchr(new_name, '\n') ||
        (g_strcmp0(old_name, new_name) != 0 && galaxy_settings_profile(s, new_name)))
        return FALSE;
    if (g_strcmp0(old_name, new_name) == 0) return TRUE;
    if (g_strcmp0(s->default_profile, old_name) == 0) {
        g_free(s->default_profile);
        s->default_profile = g_strdup(new_name);
    }
    g_free(profile->name);
    profile->name = g_strdup(new_name);
    return TRUE;
}

void galaxy_settings_remove_profile(GalaxySettings *s, const char *name)
{
    if (s->profiles->len < 2) return;
    for (guint i = 0; i < s->profiles->len; ++i) {
        GalaxyProfile *p = g_ptr_array_index(s->profiles, i);
        if (g_strcmp0(p->name, name) != 0) continue;
        g_ptr_array_remove_index(s->profiles, i);
        if (g_strcmp0(s->default_profile, name) == 0) {
            g_free(s->default_profile);
            s->default_profile = g_strdup(((GalaxyProfile *)s->profiles->pdata[0])->name);
        }
        return;
    }
}

static gboolean reload_later(gpointer data)
{
    GalaxySettings *s = data;
    s->reload_source = 0;
    g_autofree char *text = NULL;
    gsize length = 0;
    if (!g_file_get_contents(s->path, &text, &length, NULL) ||
        g_strcmp0(text, s->contents) == 0) return G_SOURCE_REMOVE;
    load_settings(s, text, length);
    if (s->changed) s->changed(s->user_data);
    return G_SOURCE_REMOVE;
}

static void on_directory_changed(GFileMonitor *monitor, GFile *file, GFile *other,
                                 GFileMonitorEvent event, gpointer data)
{
    GalaxySettings *s = data;
    (void)monitor; (void)other;
    if (event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
        event != G_FILE_MONITOR_EVENT_CREATED &&
        event != G_FILE_MONITOR_EVENT_MOVED_IN) return;
    g_autofree char *path = g_file_get_path(file);
    if (g_strcmp0(path, s->path) != 0) return;
    if (s->reload_source) g_source_remove(s->reload_source);
    s->reload_source = g_timeout_add(150, reload_later, s);
}

GalaxySettings *galaxy_settings_new(void)
{
    GalaxySettings *s = g_new0(GalaxySettings, 1);
    s->profiles = g_ptr_array_new_with_free_func(profile_free);
    s->path = g_build_filename(g_get_user_config_dir(), "galaxy-terminal", "settings.ini", NULL);
    g_autofree char *dir = g_path_get_dirname(s->path);
    if (g_mkdir_with_parents(dir, 0700) != 0)
        g_warning("Could not create settings directory %s", dir);
    g_autofree char *text = NULL;
    gsize length = 0;
    g_file_get_contents(s->path, &text, &length, NULL);
    load_settings(s, text, length);
    g_autoptr(GFile) folder = g_file_new_for_path(dir);
    s->monitor = g_file_monitor_directory(folder, G_FILE_MONITOR_NONE, NULL, NULL);
    if (s->monitor)
        g_signal_connect(s->monitor, "changed", G_CALLBACK(on_directory_changed), s);
    return s;
}

void galaxy_settings_set_changed(GalaxySettings *s, void (*changed)(gpointer), gpointer data)
{
    s->changed = changed;
    s->user_data = data;
}

void galaxy_settings_save(GalaxySettings *s)
{
    g_autoptr(GKeyFile) key = g_key_file_new();
    g_key_file_set_string(key, "General", "DefaultProfile", s->default_profile);
    g_key_file_set_integer(key, "General", "ScrollbackLines", s->scrollback);
    g_key_file_set_boolean(key, "General", "CopyOnSelect", s->auto_copy);
    g_key_file_set_boolean(key, "General", "ConfirmClose", s->confirm_close);
    g_key_file_set_boolean(key, "General", "AlwaysShowTabs", s->show_tabs);
    g_key_file_set_boolean(key, "General", "ShowScrollbar", s->show_scrollbar);
    g_key_file_set_boolean(key, "General", "HidePointerWhileTyping", s->mouse_autohide);
    g_key_file_set_boolean(key, "General", "FollowDarkMode", s->follow_dark);
    for (int i = 0; i < ACT_COUNT; ++i)
        g_key_file_set_string(key, "Shortcuts", galaxy_action_names[i], s->shortcuts[i]);
    for (guint i = 0; i < s->profiles->len; ++i) {
        GalaxyProfile *p = g_ptr_array_index(s->profiles, i);
        g_autofree char *group = g_strdup_printf("Profile %s", p->name);
        g_key_file_set_string(key, group, "Shell", p->shell);
        g_key_file_set_string(key, group, "WorkingDirectory", p->cwd);
        g_key_file_set_string(key, group, "Font", p->font);
        g_key_file_set_string(key, group, "Palette", p->palette);
        g_key_file_set_string(key, group, "Foreground", p->foreground);
        g_key_file_set_string(key, group, "Background", p->background);
        for (int j = 0; j < 16; ++j) {
            g_autofree char *field = g_strdup_printf("AnsiColor%d", j);
            g_key_file_set_string(key, group, field, p->ansi[j]);
        }
        g_key_file_set_double(key, group, "Opacity", p->opacity);
    }
    gsize length = 0;
    g_autofree char *text = g_key_file_to_data(key, &length, NULL);
    g_autoptr(GError) error = NULL;
    if (!g_file_set_contents(s->path, text, length, &error)) {
        g_warning("Could not save Galaxy Terminal settings: %s", error->message);
        return;
    }
    g_free(s->contents);
    s->contents = g_strdup(text);
    if (s->changed) s->changed(s->user_data);
}

void galaxy_settings_free(GalaxySettings *s)
{
    if (!s) return;
    if (s->reload_source) g_source_remove(s->reload_source);
    g_clear_object(&s->monitor);
    g_ptr_array_free(s->profiles, TRUE);
    for (int i = 0; i < ACT_COUNT; ++i) g_free(s->shortcuts[i]);
    g_free(s->default_profile);
    g_free(s->contents);
    g_free(s->path);
    g_free(s);
}
