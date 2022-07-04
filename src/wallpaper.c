#define _GNU_SOURCE 1

#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gtk/gtk.h>

#include <glib/gi18n.h>
#include <gio/gdesktopappinfo.h>
#include <gdesktop-enums.h>

#include "xdg-desktop-portal-dbus.h"
#include "shell-dbus.h"

#include "wallpaper.h"
#include "wallpaperdialog.h"
#include "externalwindow.h"
#include "request.h"
#include "utils.h"

#define BACKGROUND_SCHEMA "org.gnome.desktop.background"

typedef struct {
  XdpImplWallpaper *impl;
  GDBusMethodInvocation *invocation;
  Request *request;
  GtkWidget *dialog;
  ExternalWindow *external_parent;

  guint response;
  gchar *picture_uri;
} WallpaperDialogHandle;

static void
wallpaper_dialog_handle_free (gpointer data)
{
  WallpaperDialogHandle *handle = data;

  g_clear_object (&handle->external_parent);
  g_clear_object (&handle->request);
  g_clear_pointer (&handle->picture_uri, g_free);

  if (handle->dialog != NULL)
      gtk_widget_destroy (GTK_WIDGET (handle->dialog));
  g_clear_object (&handle->dialog);


  g_free (handle);
}

static void
send_response (WallpaperDialogHandle *handle)
{
  if (handle->request->exported)
    request_unexport (handle->request);

  xdp_impl_wallpaper_complete_set_wallpaper_uri (handle->impl,
                                                 handle->invocation,
                                                 handle->response);

  wallpaper_dialog_handle_free (handle);
}

static gboolean
set_gsettings (gchar *schema,
               gchar *uri)
{
  g_autoptr(GSettings) settings = NULL;

  settings = g_settings_new (schema);

  return (g_settings_set_string (settings, "picture-uri", uri) &&
          g_settings_set_enum (settings, "picture-options", G_DESKTOP_BACKGROUND_STYLE_ZOOM));
}

static void
on_file_copy_cb (GObject *source_object,
                 GAsyncResult *result,
                 gpointer data)
{
  WallpaperDialogHandle *handle = data;
  g_autoptr(GFile) destination = NULL;
  GFile *picture_file = G_FILE (source_object);
  g_autoptr(GError) error = NULL;
  g_autofree gchar *uri = NULL;
  gchar *contents = NULL;
  gsize length = 0;

  handle->response = 2;

  uri = g_file_get_uri (picture_file);
  if (!g_file_load_contents_finish (picture_file, result, &contents, &length, NULL, &error))
    {
      g_warning ("Failed to copy '%s': %s", uri, error->message);

      goto out;
    }

  destination = g_file_new_for_path (handle->picture_uri);
  if (!g_file_replace_contents (destination,
                                contents,
                                length,
                                NULL, FALSE,
                                G_FILE_CREATE_REPLACE_DESTINATION,
                                NULL, NULL,
                                &error))
    {
      g_warning ("Failed to store image as '%s': %s", handle->picture_uri, error->message);
      goto out;
    }

  if (set_gsettings (BACKGROUND_SCHEMA, handle->picture_uri))
    handle->response = 0;
  else
    handle->response = 1;

out:
  send_response (handle);
}

static void
set_wallpaper (WallpaperDialogHandle *handle,
               const gchar *uri)
{
  g_autoptr(GFile) source = NULL;

  handle->picture_uri = g_build_filename (g_get_user_config_dir (), "background", NULL);

  source = g_file_new_for_uri (uri);
  g_file_load_contents_async (source,
                              NULL,
                              on_file_copy_cb,
                              handle);
}

static void
handle_wallpaper_dialog_response (WallpaperDialog *dialog,
                                  gint response,
                                  gpointer data)
{
  WallpaperDialogHandle *handle = data;

  switch (response)
    {
      default:
        g_warning ("Unexpected response: %d", response);
        /* Fall through */
      case GTK_RESPONSE_DELETE_EVENT:
        handle->response = 2;
        break;
      case GTK_RESPONSE_CANCEL:
        handle->response = 1;
        break;
      case GTK_RESPONSE_APPLY:
        handle->response = 0;
        set_wallpaper (handle, wallpaper_dialog_get_uri (dialog));

        return;
    }

  send_response (handle);
}

static gboolean
handle_set_wallpaper_uri (XdpImplWallpaper *object,
                          GDBusMethodInvocation *invocation,
                          const char *arg_handle,
                          const char *arg_app_id,
                          const char *arg_parent_window,
                          const char *arg_uri,
                          GVariant *arg_options)
{
  g_autoptr(Request) request = NULL;
  WallpaperDialogHandle *handle;
  const char *sender;
  gboolean show_preview = FALSE;
  GdkDisplay *display;
  GdkScreen *screen;
  ExternalWindow *external_parent = NULL;
  GtkWidget *fake_parent;
  GtkWidget *dialog;

  sender = g_dbus_method_invocation_get_sender (invocation);
  request = request_new (sender, arg_app_id, arg_handle);

  g_variant_lookup (arg_options, "show-preview", "b", &show_preview);

  handle = g_new0 (WallpaperDialogHandle, 1);
  handle->impl = object;
  handle->invocation = invocation;
  handle->request = g_object_ref (request);

  if (!show_preview)
    {
      set_wallpaper (handle, arg_uri);
      goto out;
    }

  if (arg_parent_window)
    {
      external_parent = create_external_window_from_handle (arg_parent_window);
      if (!external_parent)
        g_warning ("Failed to associate portal window with parent window %s",
                   arg_parent_window);
    }

  if (external_parent)
    display = external_window_get_display (external_parent);
  else
    display = gdk_display_get_default ();
  screen = gdk_display_get_default_screen (display);

  fake_parent = g_object_new (GTK_TYPE_WINDOW,
                              "type", GTK_WINDOW_TOPLEVEL,
                              "screen", screen,
                              NULL);
  g_object_ref_sink (fake_parent);

  dialog = (GtkWidget *)wallpaper_dialog_new (arg_uri, arg_app_id);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fake_parent));
  handle->dialog = g_object_ref (dialog);

  g_signal_connect (dialog, "response",
                    G_CALLBACK (handle_wallpaper_dialog_response), handle);
  gtk_widget_realize (dialog);

  if (external_parent)
    external_window_set_parent_of (external_parent, gtk_widget_get_window (dialog));

  gtk_window_present (GTK_WINDOW (dialog));

out:
  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  return TRUE;
}

gboolean
wallpaper_init (GDBusConnection *bus,
                GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_wallpaper_skeleton_new ());

  g_signal_connect (helper, "handle-set-wallpaper-uri", G_CALLBACK (handle_set_wallpaper_uri), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
