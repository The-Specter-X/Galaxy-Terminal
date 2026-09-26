#include "app.h"

gboolean galaxy_desktop_dark(void)
{
    gboolean dark = FALSE;
    GtkSettings *settings = gtk_settings_get_default();
    if (settings) g_object_get(settings, "gtk-application-prefer-dark-theme", &dark, NULL);
    return dark;
}

void galaxy_terminal_apply_profile(VteTerminal *terminal, GalaxyProfile *p)
{
    gboolean dark = !g_strcmp0(p->palette, "Dark") ||
        (!g_strcmp0(p->palette, "System") && galaxy_desktop_dark());
    const char *fg = dark ? "#ebedf4" : "#242424";
    const char *bg = dark ? "#191b24" : "#fafafa";
    static const char *const dark_colors[16] = {
        "#20232c", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#dcdfe4",
        "#5b616e", "#ff7b86", "#b3dd91", "#f5d492", "#80c2fb", "#dc9cf1", "#7dd3db", "#ffffff"
    };
    static const char *const light_colors[16] = {
        "#343a45", "#b42134", "#25792e", "#8b6200", "#215fbd", "#90469b", "#007c87", "#d5d8dc",
        "#666d77", "#d32e42", "#3a973e", "#a37b08", "#397be0", "#ac63b2", "#159aa5", "#ffffff"
    };
    gboolean custom = !g_strcmp0(p->palette, "Custom");
    if (custom) { fg = p->foreground; bg = p->background; }
    GdkRGBA foreground, background, palette[16];
    if (!gdk_rgba_parse(&foreground, fg)) gdk_rgba_parse(&foreground, "#ebedf4");
    if (!gdk_rgba_parse(&background, bg)) gdk_rgba_parse(&background, "#191b24");
    foreground.alpha = 1.0;
    background.alpha = p->opacity;
    for (int i = 0; i < 16; i++) {
        if (!gdk_rgba_parse(&palette[i], custom ? p->ansi[i] :
                            (dark ? dark_colors[i] : light_colors[i])))
            gdk_rgba_parse(&palette[i], dark_colors[i]);
        palette[i].alpha = 1.0;
    }
    vte_terminal_set_colors(terminal, &foreground, &background, palette, 16);
    vte_terminal_set_color_cursor(terminal, NULL);
    g_autoptr(PangoFontDescription) font = pango_font_description_from_string(p->font);
    vte_terminal_set_font(terminal, font);
    vte_terminal_set_cursor_shape(terminal, p->cursor_shape);
    vte_terminal_set_cursor_blink_mode(terminal, p->cursor_blink);
    vte_terminal_set_audible_bell(terminal, p->audible_bell);
    vte_terminal_set_scroll_on_output(terminal, p->scroll_on_output);
    vte_terminal_set_scroll_on_keystroke(terminal, p->scroll_on_keystroke);
}

void galaxy_install_css(void)
{
    const char *css =
        "window.galaxy-window, notebook.galaxy-notebook > stack,"
        "scrolledwindow.galaxy-terminal, scrolledwindow.galaxy-terminal > viewport {"
        "background-color: transparent; background-image: none; }"
        "notebook.galaxy-notebook > header > tabs > tab { padding: 5px 8px; }"
        ".galaxy-status { padding: 6px 12px; }"
        ".galaxy-error { color: #c01c28; }"
        ".galaxy-preview { padding: 8px; }";
    g_autoptr(GtkCssProvider) provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}
