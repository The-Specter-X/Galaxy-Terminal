#include "shortcuts.h"
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>

const GalaxyShortcut galaxy_shortcuts[ACT_COUNT] = {
    {"NewTab", N_("New tab"), "<Control><Shift>t"},
    {"NewWindow", N_("New window"), "<Control><Shift>n"},
    {"CloseTab", N_("Close tab"), "<Control><Shift>w"},
    {"Copy", N_("Copy"), "<Control><Shift>c"},
    {"Paste", N_("Paste"), "<Control><Shift>v"},
    {"Find", N_("Find"), "<Control><Shift>f"},
    {"NextTab", N_("Next tab"), "<Control>Page_Down"},
    {"PreviousTab", N_("Previous tab"), "<Control>Page_Up"},
    {"ZoomIn", N_("Zoom in"), "<Control>plus"},
    {"ZoomOut", N_("Zoom out"), "<Control>minus"},
    {"ZoomReset", N_("Reset zoom"), "<Control>0"},
    {"Preferences", N_("Preferences"), "<Control>comma"},
    {"Fullscreen", N_("Fullscreen"), "F11"},
    {"Tab1", N_("Switch to tab 1"), "<Alt>1"},
    {"Tab2", N_("Switch to tab 2"), "<Alt>2"},
    {"Tab3", N_("Switch to tab 3"), "<Alt>3"},
    {"Tab4", N_("Switch to tab 4"), "<Alt>4"},
    {"Tab5", N_("Switch to tab 5"), "<Alt>5"},
    {"Tab6", N_("Switch to tab 6"), "<Alt>6"},
    {"Tab7", N_("Switch to tab 7"), "<Alt>7"},
    {"Tab8", N_("Switch to tab 8"), "<Alt>8"},
    {"Tab9", N_("Switch to tab 9"), "<Alt>9"}
};

/* Validate configuration without a display. GtkAccelGroup handles key events. */
gboolean galaxy_shortcut_parse(const char *text, guint *key, GdkModifierType *mods)
{
    *key = 0; *mods = 0;
    if (!text) return FALSE;
    if (!*text) return TRUE;
    const char *at = text;
    while (*at == '<') {
        const char *end = strchr(at, '>');
        if (!end) return FALSE;
        g_autofree char *part = g_strndup(at + 1, end - at - 1);
        if (!g_ascii_strcasecmp(part, "Control") || !g_ascii_strcasecmp(part, "Ctrl") ||
            !g_ascii_strcasecmp(part, "Primary")) *mods |= GDK_CONTROL_MASK;
        else if (!g_ascii_strcasecmp(part, "Shift")) *mods |= GDK_SHIFT_MASK;
        else if (!g_ascii_strcasecmp(part, "Alt")) *mods |= GDK_MOD1_MASK;
        else if (!g_ascii_strcasecmp(part, "Super")) *mods |= GDK_SUPER_MASK;
        else return FALSE;
        at = end + 1;
    }
    *key = gdk_keyval_to_lower(gdk_keyval_from_name(at));
    if (*key == GDK_KEY_VoidSymbol || !*key) return FALSE;
    if (*key == GDK_KEY_plus) *mods &= ~GDK_SHIFT_MASK;
    return gtk_accelerator_valid(*key, *mods) &&
        ((*mods & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK)) ||
         (*key >= GDK_KEY_F1 && *key <= GDK_KEY_F35));
}

char *galaxy_shortcut_capture(GdkEventKey *event)
{
    guint key = gdk_keyval_to_lower(event->keyval);
    GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask();
    if (key == GDK_KEY_plus) mods &= ~GDK_SHIFT_MASK;
    g_autofree char *name = gtk_accelerator_name(key, mods);
    guint parsed; GdkModifierType parsed_mods;
    return galaxy_shortcut_parse(name, &parsed, &parsed_mods) ? g_strdup(name) : NULL;
}
