#pragma once
#include <gtk/gtk.h>

typedef enum {
    ACT_NEW_TAB, ACT_NEW_WINDOW, ACT_CLOSE_TAB, ACT_COPY, ACT_PASTE,
    ACT_FIND, ACT_NEXT_TAB, ACT_PREV_TAB, ACT_ZOOM_IN, ACT_ZOOM_OUT,
    ACT_ZOOM_RESET, ACT_PREFERENCES, ACT_FULLSCREEN, ACT_TAB_1,
    ACT_TAB_2, ACT_TAB_3, ACT_TAB_4, ACT_TAB_5, ACT_TAB_6,
    ACT_TAB_7, ACT_TAB_8, ACT_TAB_9, ACT_COUNT
} GalaxyAction;

typedef struct { const char *name; const char *label; const char *accelerator; } GalaxyShortcut;
extern const GalaxyShortcut galaxy_shortcuts[ACT_COUNT];
gboolean galaxy_shortcut_parse(const char *text, guint *key, GdkModifierType *mods);
char *galaxy_shortcut_capture(GdkEventKey *event);
