#define _GNU_SOURCE

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <string.h>

#define FALLBACK_URI "file:///usr/local/share/p90/p90-adwaita-portrait.png"

static GtkCssProvider *wallpaper_provider;
static GSettings *background_settings;
static GSettings *interface_settings;

static gboolean
safe_file_uri(const char *uri)
{
    return uri != NULL &&
           g_str_has_prefix(uri, "file:///") &&
           strpbrk(uri, "\"\\\r\n") == NULL;
}

static void
reload_wallpaper(void)
{
    g_autofree char *light_uri = NULL;
    g_autofree char *dark_uri = NULL;
    g_autofree char *colour_scheme = NULL;
    g_autofree char *css = NULL;
    const char *uri = FALLBACK_URI;

    light_uri = g_settings_get_string(background_settings, "picture-uri");
    dark_uri = g_settings_get_string(background_settings, "picture-uri-dark");
    colour_scheme = g_settings_get_string(interface_settings, "color-scheme");

    if (g_strcmp0(colour_scheme, "prefer-dark") == 0 && safe_file_uri(dark_uri))
        uri = dark_uri;
    else if (safe_file_uri(light_uri))
        uri = light_uri;
    else if (safe_file_uri(dark_uri))
        uri = dark_uri;

    css = g_strdup_printf(
        "phosh-home {"
        " background-color: #263b68;"
        " background-image: url(\"%s\");"
        " background-repeat: no-repeat;"
        " background-position: center;"
        " background-size: cover;"
        "}"
        ".phosh-overview, phosh-app-grid {"
        " background: transparent;"
        " background-color: transparent;"
        " background-image: none;"
        "}", uri);

    gtk_css_provider_load_from_data(wallpaper_provider, css, -1, NULL);
    if (gdk_screen_get_default() != NULL)
        gtk_style_context_reset_widgets(gdk_screen_get_default());
    g_message("P90 Phosh wallpaper updated: %s", uri);
}

static void
settings_changed(GSettings *settings, gchar *key, gpointer user_data)
{
    (void)settings;
    (void)key;
    (void)user_data;
    reload_wallpaper();
}

static gboolean
install_wallpaper_provider(gpointer user_data)
{
    GdkScreen *screen = gdk_screen_get_default();

    (void)user_data;
    if (screen == NULL)
        return G_SOURCE_CONTINUE;

    wallpaper_provider = gtk_css_provider_new();
    background_settings = g_settings_new("org.gnome.desktop.background");
    interface_settings = g_settings_new("org.gnome.desktop.interface");

    gtk_style_context_add_provider_for_screen(
        screen,
        GTK_STYLE_PROVIDER(wallpaper_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER + 1);

    g_signal_connect(background_settings, "changed::picture-uri",
                     G_CALLBACK(settings_changed), NULL);
    g_signal_connect(background_settings, "changed::picture-uri-dark",
                     G_CALLBACK(settings_changed), NULL);
    g_signal_connect(interface_settings, "changed::color-scheme",
                     G_CALLBACK(settings_changed), NULL);
    reload_wallpaper();
    return G_SOURCE_REMOVE;
}

__attribute__((constructor)) static void
queue_wallpaper_provider(void)
{
    /* The helper belongs only in Phosh.  Desktop applications launched by
     * Phosh inherit its environment; leaving this GTK 3 library in LD_PRELOAD
     * makes GTK 4 applications (notably GNOME Settings) abort immediately.
     * Keep the old-kernel compatibility shim for children, but remove this
     * library from their future preload list after the loader has mapped it. */
    g_setenv("LD_PRELOAD", "/run/p90-getrandom-compat.so", TRUE);
    g_idle_add(install_wallpaper_provider, NULL);
}
