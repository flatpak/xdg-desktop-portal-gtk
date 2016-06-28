#define _GNU_SOURCE 1

#include "config.h"

#include <errno.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gtk/gtk.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>

#include "xdg-desktop-portal-dbus.h"

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include "screenshotdialog.h"
#include "screenshot.h"
#include "xdg-desktop-portal-gtk.h"

static XdpOrgGnomeShellScreenshot *shell;

typedef struct {
  DialogHandle base;

  GtkWidget *dialog;

  int response;
  char *uri;
} ScreenshotDialogHandle;

static ScreenshotDialogHandle *
screenshot_dialog_handle_new (const char *app_id,
                              const char *sender,
                              const char *uri,
                              GtkWidget *dialog,
                              GDBusInterfaceSkeleton *skeleton)
{
  ScreenshotDialogHandle *handle = g_new0 (ScreenshotDialogHandle, 1);

  handle->base.app_id = g_strdup (app_id);
  handle->base.sender = g_strdup (sender);
  handle->base.skeleton = g_object_ref (skeleton);

  handle->dialog = g_object_ref (dialog);
  handle->uri = g_strdup (uri);

  dialog_handle_register (&handle->base);

  /* TODO: Track lifetime of sender and close handle */

  return handle;
}

static void
screenshot_dialog_handle_free (ScreenshotDialogHandle *handle)
{
  dialog_handle_unregister (&handle->base);
  g_free (handle->base.id);
  g_free (handle->base.app_id);
  g_free (handle->base.sender);
  g_object_unref (handle->base.skeleton);
  g_object_unref (handle->dialog);

  g_free (handle->uri);
  g_free (handle);
}

static void
screenshot_dialog_handle_close (ScreenshotDialogHandle *handle)
{
  gtk_widget_destroy (handle->dialog);
  screenshot_dialog_handle_free (handle);
}

static void
send_response (ScreenshotDialogHandle *handle)
{
  GVariantBuilder opt_builder;
  GVariant *options;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  options = g_variant_builder_end (&opt_builder);

  g_dbus_connection_emit_signal (g_dbus_interface_skeleton_get_connection (handle->base.skeleton),
                                 "org.freedesktop.portal.Desktop",
                                 "/org/freedesktop/portal/desktop",
                                 "org.freedesktop.impl.portal.Screenshot",
                                 "ScreenshotResponse",
                                 g_variant_new ("(sous@a{sv})",
                                                handle->base.sender,
                                                handle->base.id,
                                                handle->response,
                                                handle->uri ? handle->uri : "",
                                                options),
                                 NULL);

  screenshot_dialog_handle_close (handle);
}

static void
screenshot_dialog_done (GtkWidget *widget,
                        int response,
                        gpointer user_data)
{
  ScreenshotDialogHandle *handle = user_data;

  switch (response)
    {
    default:
      g_warning ("Unexpected response: %d", response);
      /* Fall through */
    case GTK_RESPONSE_DELETE_EVENT:
      handle->response = 2;
      g_free (handle->uri);
      handle->uri = NULL;
      break;

    case GTK_RESPONSE_CANCEL:
      handle->response = 1;
      g_free (handle->uri);
      handle->uri = NULL;
      break;

    case GTK_RESPONSE_OK:
      handle->response = 0;
      break;
    }

  send_response (handle);
}

static gboolean
handle_screenshot (XdpScreenshot *object,
                   GDBusMethodInvocation *invocation,
                   const gchar *arg_sender,
                   const gchar *arg_app_id,
                   const gchar *arg_parent_window,
                   GVariant *arg_options)
{
  XdpScreenshot *chooser = XDP_SCREENSHOT (g_dbus_method_invocation_get_user_data (invocation));
  ScreenshotDialogHandle *handle;
  g_autoptr(GError) error = NULL;
  g_autofree char *filename = NULL;
  g_autofree char *uri = NULL;
  gboolean success;
  GtkWidget *dialog;

  xdp_org_gnome_shell_screenshot_call_screenshot_sync (shell,
                                                       FALSE,
                                                       TRUE,
                                                       "Screenshot",
                                                       &success,
                                                       &filename,
                                                       NULL,
                                                       NULL);

  dialog = GTK_WIDGET (screenshot_dialog_new (arg_app_id, filename));

  uri = g_strconcat ("file://", filename, NULL);
  handle = screenshot_dialog_handle_new (arg_app_id, arg_sender, uri, dialog, G_DBUS_INTERFACE_SKELETON (chooser));

  g_signal_connect (dialog, "done", G_CALLBACK (screenshot_dialog_done), handle);

  gtk_window_present (GTK_WINDOW (dialog));

  xdp_screenshot_complete_screenshot (chooser, invocation, handle->base.id);

  return TRUE;
}

static gboolean
handle_screenshot_close (XdpScreenshot *object,
                         GDBusMethodInvocation *invocation,
                         const gchar *arg_sender,
                         const gchar *arg_app_id,
                         const gchar *arg_handle)
{
  ScreenshotDialogHandle *handle;

  handle = (ScreenshotDialogHandle *)dialog_handle_find (arg_sender, arg_app_id, arg_handle,
                                                         XDP_TYPE_SCREENSHOT_SKELETON);

  if (handle != NULL)
    {
      screenshot_dialog_handle_close (handle);
      xdp_screenshot_complete_screenshot (object, invocation, handle->base.id);
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
screenshot_init (GDBusConnection *bus,
                 GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_screenshot_skeleton_new ());

  g_signal_connect (helper, "handle-screenshot", G_CALLBACK (handle_screenshot), NULL);
  g_signal_connect (helper, "handle-close", G_CALLBACK (handle_screenshot_close), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         "/org/freedesktop/portal/desktop",
                                         error))
    return FALSE;

  shell = xdp_org_gnome_shell_screenshot_proxy_new_sync (bus,
                                                         G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                         "org.gnome.Shell.Screenshot",
                                                         "/org/gnome/Shell/Screenshot",
                                                         NULL,
                                                         error);
  if (shell == NULL)
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
