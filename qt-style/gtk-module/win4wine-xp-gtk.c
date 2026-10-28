// SPDX-License-Identifier: LGPL-2.1-or-later
// A GTK 3 module for the XP session (GTK_MODULES): it lays XP's parts of the
// session's theme (win4wine.css, made by xpstyle-gtk) over the
// application's own CSS. Applications add their CSS at a priority above any
// theme's, and many restyle buttons with it (no border, rounded corners),
// which took XP's art off them; the user's own gtk.css stays above both.
// It follows the theme XSETTINGS names (or GTK_THEME), as GTK does.
#include <gtk/gtk.h>

static GtkCssProvider *provider;

static gchar *theme_file(const gchar *theme)
{
    const gchar *const *dirs = g_get_system_data_dirs();
    gchar *path = g_build_filename(g_get_user_data_dir(), "themes", theme, "gtk-3.0", "win4wine.css", NULL);
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
        return path;
    g_free(path);
    path = g_build_filename(g_get_home_dir(), ".themes", theme, "gtk-3.0", "win4wine.css", NULL);
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
        return path;
    g_free(path);
    for (; *dirs; dirs++) {
        path = g_build_filename(*dirs, "themes", theme, "gtk-3.0", "win4wine.css", NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
            return path;
        g_free(path);
    }
    return NULL;
}

static void update(GtkSettings *settings)
{
    GdkScreen *screen = gdk_screen_get_default();
    gchar *theme = NULL, *path;
    const gchar *forced = g_getenv("GTK_THEME");

    if (!screen)
        return;
    if (provider) {
        gtk_style_context_remove_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider));
        g_clear_object(&provider);
    }
    if (forced && *forced)
        theme = g_strndup(forced, strcspn(forced, ":"));
    else
        g_object_get(settings, "gtk-theme-name", &theme, NULL);
    if (theme && g_str_has_prefix(theme, "Win4Wine-XP") && (path = theme_file(theme))) {
        provider = gtk_css_provider_new();
        if (gtk_css_provider_load_from_path(provider, path, NULL))
            gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider),
                                                      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
        else
            g_clear_object(&provider);
        g_free(path);
    }
    g_free(theme);
}

static void theme_changed(GtkSettings *settings, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    (void)data;
    update(settings);
}

G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module)
{
    // GTK unloads nothing it calls back into: keep the module.
    g_module_make_resident(module);
    return NULL;
}

G_MODULE_EXPORT void gtk_module_init(gint *argc, gchar ***argv)
{
    GtkSettings *settings = gtk_settings_get_default();

    (void)argc;
    (void)argv;
    if (!settings)
        return;
    g_signal_connect(settings, "notify::gtk-theme-name", G_CALLBACK(theme_changed), NULL);
    update(settings);
}
