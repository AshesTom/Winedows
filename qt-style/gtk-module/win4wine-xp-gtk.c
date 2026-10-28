// SPDX-License-Identifier: LGPL-2.1-or-later
// A GTK 3 module for the XP session (GTK_MODULES): it lays XP's parts of the
// session's theme (win4wine.css, made by xpstyle-gtk) over the
// application's own CSS. Applications add their CSS at a priority above any
// theme's, and many restyle buttons with it (no border, rounded corners),
// which took XP's art off them; the user's own gtk.css stays above both.
// It follows the theme XSETTINGS names (or GTK_THEME), as GTK does.
//
// With the Accurate style (Control Panel's GTK/QT Themes, a theme named
// Win4Wine-XP-Accurate-<id>) it also takes the pictures off what has none
// on XP: push buttons that have text, and menu items. GTK 3 ignores its old
// gtk-button-images and gtk-menu-images settings, and CSS cannot hide a
// widget, so the images are hidden as they are mapped, and shown again when
// the user goes back to the Standard style. Toolbars keep theirs.
#include <gtk/gtk.h>
#include <string.h>

static GtkCssProvider *provider;
static gboolean accurate;
// The images hidden for the Accurate style, shown again without it.
static GHashTable *hidden;

static gboolean has_text(GtkWidget *widget)
{
    if (GTK_IS_LABEL(widget)) {
        const gchar *text = gtk_label_get_text(GTK_LABEL(widget));
        return gtk_widget_get_visible(widget) && text && *text;
    }
    if (GTK_IS_CONTAINER(widget)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget)), *l;
        gboolean found = FALSE;
        for (l = children; l && !found; l = l->next)
            found = has_text(l->data);
        g_list_free(children);
        return found;
    }
    return FALSE;
}

// Whether XP would show no picture here: in a menu item, or in a push button
// (not a toolbar's) that has text beside it.
static gboolean pictureless(GtkWidget *image)
{
    GtkWidget *button;
    if (gtk_widget_get_ancestor(image, GTK_TYPE_MENU_ITEM))
        return TRUE;
    button = gtk_widget_get_ancestor(image, GTK_TYPE_BUTTON);
    if (!button || gtk_widget_get_ancestor(image, GTK_TYPE_TOOL_ITEM))
        return FALSE;
    return has_text(button);
}

static void forget(gpointer data, GObject *gone)
{
    (void)data;
    g_hash_table_remove(hidden, gone);
}

static gboolean hide_later(gpointer data)
{
    GtkWidget *image = data;
    if (accurate && gtk_widget_get_visible(image) && pictureless(image) && !g_hash_table_contains(hidden, image)) {
        g_hash_table_add(hidden, image);
        g_object_weak_ref(G_OBJECT(image), forget, NULL);
        gtk_widget_set_no_show_all(image, TRUE);
        gtk_widget_hide(image);
    }
    g_object_unref(image);
    return G_SOURCE_REMOVE;
}

// Every image, as it is mapped: hidden at the next idle, outside GTK's mapping.
static gboolean mapped(GSignalInvocationHint *hint, guint count, const GValue *values, gpointer data)
{
    GObject *object = count ? g_value_get_object(&values[0]) : NULL;
    (void)hint;
    (void)data;
    if (accurate && GTK_IS_IMAGE(object))
        g_idle_add(hide_later, g_object_ref(object));
    return TRUE;
}

static void visit(GtkWidget *widget, gpointer data)
{
    (void)data;
    if (GTK_IS_IMAGE(widget) && gtk_widget_get_mapped(widget))
        g_idle_add(hide_later, g_object_ref(widget));
    if (GTK_IS_CONTAINER(widget))
        gtk_container_forall(GTK_CONTAINER(widget), visit, NULL);
}

static void set_accurate(gboolean on)
{
    GList *windows, *l;
    if (on == accurate)
        return;
    accurate = on;
    if (on) {
        // The windows already open
        windows = gtk_window_list_toplevels();
        for (l = windows; l; l = l->next)
            visit(l->data, NULL);
        g_list_free(windows);
        return;
    }
    {
        GHashTableIter iter;
        gpointer image;
        g_hash_table_iter_init(&iter, hidden);
        while (g_hash_table_iter_next(&iter, &image, NULL)) {
            g_object_weak_unref(image, forget, NULL);
            gtk_widget_set_no_show_all(image, FALSE);
            gtk_widget_show(image);
            g_hash_table_iter_remove(&iter);
        }
    }
}

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
    set_accurate(theme && g_str_has_prefix(theme, "Win4Wine-XP-Accurate"));
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
    hidden = g_hash_table_new(NULL, NULL);
    g_signal_add_emission_hook(g_signal_lookup("map", GTK_TYPE_WIDGET), 0, mapped, NULL, NULL);
    g_signal_connect(settings, "notify::gtk-theme-name", G_CALLBACK(theme_changed), NULL);
    update(settings);
}
