#include "appchooser.h"
#include "xdg-desktop-portal-gtk.h"

#define _GNU_SOURCE 1

#include "config.h"

#include <string.h>

#include <gtk/gtk.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>

#include "xdg-desktop-portal-dbus.h"

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

typedef struct {
  DialogHandle base;

  char *uri;
  char *content_type;
  GtkWidget *dialog;

  int response;

} AppDialogHandle;

static AppDialogHandle *
app_dialog_handle_new (const char *app_id,
                       const char *sender,
                       GtkWidget *dialog,
                       GDBusInterfaceSkeleton *skeleton)
{
  AppDialogHandle *handle = g_new0 (AppDialogHandle, 1);

  handle->base.app_id = g_strdup (app_id);
  handle->base.sender = g_strdup (sender);
  handle->base.skeleton = g_object_ref (skeleton);
  handle->dialog = g_object_ref (dialog);

  dialog_handle_register (&handle->base);

  /* TODO: Track lifetime of sender and close handle */

  return handle;
}

static void
app_dialog_handle_free (AppDialogHandle *handle)
{
  dialog_handle_unregister (&handle->base);
  g_free (handle->base.app_id);
  g_free (handle->base.sender);
  g_object_unref (handle->base.skeleton);
  g_free (handle->uri);
  g_free (handle->content_type);
  g_object_unref (handle->dialog);
  g_free (handle);
}

static void
app_dialog_handle_close (AppDialogHandle *handle)
{
  gtk_widget_destroy (handle->dialog);
  app_dialog_handle_free (handle);
}

static void
send_response (AppDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  
  g_dbus_connection_emit_signal (g_dbus_interface_skeleton_get_connection (handle->base.skeleton),
                                 "org.freedesktop.portal.Desktop",
                                 "/org/freedesktop/portal/desktop",
                                 "org.freedesktop.portal.AppChooser",
                                 "OpenURIResponse",
                                 g_variant_new ("(sou@a{sv})",
                                                handle->base.sender,
                                                handle->base.id,
                                                handle->response,
                                                g_variant_builder_end (&opt_builder)),
                                 NULL);

  app_dialog_handle_close (handle);
}

static void
handle_app_chooser_response (GtkDialog *dialog,
                             gint response,
                             gpointer data)
{
  AppDialogHandle *handle = data;

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

    case GTK_RESPONSE_OK:
      {
        GList uris;

        uris.data = handle->uri;
        uris.next = NULL;

        g_autoptr(GAppInfo) app_info = gtk_app_chooser_get_app_info (GTK_APP_CHOOSER (dialog));
        g_app_info_launch_uris (app_info, &uris, NULL, NULL);

        handle->response = 0;
      }
      break;
    }

  send_response (handle);
}

static gboolean
handle_app_chooser_open_uri (XdpAppChooser *object,
                             GDBusMethodInvocation *invocation,
                             const gchar *arg_sender,
                             const gchar *arg_app_id,
                             const gchar *arg_parent_window,
                             const gchar *arg_uri,
                             GVariant *arg_options)
{
  XdpAppChooser *chooser = XDP_APP_CHOOSER (g_dbus_method_invocation_get_user_data (invocation));
  GtkWidget *dialog;
  char *str;
  char *uri_scheme;
  char *content_type;
  g_autoptr(GAppInfo) app_info = NULL;
  AppDialogHandle *handle;

  g_print ("OpenUri: %s\n", arg_uri);

  uri_scheme = g_uri_parse_scheme (arg_uri);
  if (uri_scheme && uri_scheme[0] != '\0' && strcmp (uri_scheme, "file") != 0)
    {
      g_autofree char *scheme_down = g_ascii_strdown (uri_scheme, -1);
      content_type = g_strconcat ("x-scheme-handler/", scheme_down, NULL);
      app_info = g_app_info_get_default_for_uri_scheme (uri_scheme);
    }
  else
    {
      g_autoptr(GFile) file = g_file_new_for_uri (arg_uri);
      g_autoptr(GFileInfo) info = g_file_query_info (file,
                                                     G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                                     0,
                                                     NULL,
                                                     NULL);
      content_type = g_strdup (g_file_info_get_content_type (info));
    }
  g_free (uri_scheme);

  app_info = g_app_info_get_default_for_type (content_type, FALSE);

  dialog = gtk_app_chooser_dialog_new_for_content_type (NULL,
                                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        content_type);
  if (strcmp (arg_app_id, "") == 0)
    {
      str = g_strdup_printf ("An application wants to open %s", arg_uri);
    }
  else
    {
      g_autofree char *desktop_id = g_strconcat (arg_app_id, ".desktop", NULL);
      g_autoptr(GAppInfo) app_info = (GAppInfo *)g_desktop_app_info_new (desktop_id);
      str = g_strdup_printf ("%s wants to open %s", g_app_info_get_display_name (app_info), arg_uri);
    }
  gtk_app_chooser_dialog_set_heading (GTK_APP_CHOOSER_DIALOG (dialog), str);

  handle = app_dialog_handle_new (arg_app_id, arg_sender, dialog, G_DBUS_INTERFACE_SKELETON (object));

  g_signal_connect (dialog, "response", G_CALLBACK (handle_app_chooser_response), handle);

  handle->uri = g_strdup (arg_uri);
  handle->content_type = content_type;

  gtk_window_present (GTK_WINDOW (dialog));

  xdp_app_chooser_complete_open_uri (chooser, invocation, handle->base.id);

  return TRUE;
}

static gboolean
handle_app_chooser_close (XdpAppChooser *object,
                           GDBusMethodInvocation *invocation,
                           const gchar *arg_sender,
                           const gchar *arg_app_id,
                           const gchar *arg_handle)
{
  AppDialogHandle *handle;


  handle = (AppDialogHandle *)dialog_handle_find (arg_sender, arg_app_id, arg_handle,
                                                  XDP_TYPE_APP_CHOOSER_SKELETON);

  if (handle != NULL)
    {
      app_dialog_handle_close (handle);
      xdp_app_chooser_complete_close (object, invocation);
    }
  else
    {
      g_dbus_method_invocation_return_dbus_error (invocation,
                                                  "org.freedesktop.Flatpak.Error.NotFound",
                                                  "No such handle");
    }

  return TRUE;
}

gboolean
app_chooser_init (GDBusConnection *bus,
                  GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_app_chooser_skeleton_new ());

  g_signal_connect (helper, "handle-open-uri", G_CALLBACK (handle_app_chooser_open_uri), NULL);
  g_signal_connect (helper, "handle-close", G_CALLBACK (handle_app_chooser_close), NULL);


  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         "/org/freedesktop/portal/desktop",
                                         error))
    return FALSE;

  return TRUE;
}
