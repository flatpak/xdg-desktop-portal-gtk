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
#include "shell-dbus.h"

#include "screenshotdialog.h"
#include "screenshot.h"
#include "externalwindow.h"
#include "request.h"
#include "utils.h"

static OrgGnomeShellScreenshot *shell;

typedef struct {
  XdpImplScreenshot *impl;
  GDBusMethodInvocation *invocation;
  Request *request;

  GtkWidget *dialog;
  ExternalWindow *external_parent;

  int response;
  char *uri;
} ScreenshotDialogHandle;

static void
screenshot_dialog_handle_free (gpointer data)
{
  ScreenshotDialogHandle *handle = data;

  g_clear_object (&handle->external_parent);
  g_object_unref (handle->request);
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

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&opt_builder, "{sv}", "uri", g_variant_new_string (handle->uri ? handle->uri : ""));

  if (handle->request->exported)
    request_unexport (handle->request);

  xdp_impl_screenshot_complete_screenshot (handle->impl,
                                           handle->invocation,
                                           handle->response,
                                           g_variant_builder_end (&opt_builder));

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
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              ScreenshotDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  xdp_impl_screenshot_complete_screenshot (handle->impl,
                                           handle->invocation,
                                           2,
                                           g_variant_builder_end (&opt_builder));
  screenshot_dialog_handle_close (handle);
  return FALSE;
}

static gboolean
handle_screenshot (XdpImplScreenshot *object,
                   GDBusMethodInvocation *invocation,
                   const char *arg_handle,
                   const char *arg_app_id,
                   const char *arg_parent_window,
                   GVariant *arg_options)
{
  g_autoptr(Request) request = NULL;
  const char *sender;
  ScreenshotDialogHandle *handle;
  g_autoptr(GError) error = NULL;
  g_autofree char *filename = NULL;
  gboolean success;
  gboolean modal;
  GtkWidget *dialog;
  GdkDisplay *display;
  GdkScreen *screen;
  ExternalWindow *external_parent = NULL;
  GtkWidget *fake_parent;

  sender = g_dbus_method_invocation_get_sender (invocation);

  request = request_new (sender, arg_app_id, arg_handle);

  org_gnome_shell_screenshot_call_screenshot_sync (shell,
                                                   FALSE,
                                                   TRUE,
                                                   "Screenshot",
                                                   &success,
                                                   &filename,
                                                   NULL, NULL);

  if (!g_variant_lookup (arg_options, "modal", "b", &modal))
    modal = TRUE;

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

  dialog = GTK_WIDGET (screenshot_dialog_new (arg_app_id, filename));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fake_parent));
  gtk_window_set_modal (GTK_WINDOW (dialog), modal);

  handle = g_new0 (ScreenshotDialogHandle, 1);
  handle->impl = object;
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->dialog = g_object_ref (dialog);
  handle->external_parent = external_parent;
  handle->uri = g_filename_to_uri (filename, NULL, NULL);

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

  g_signal_connect (dialog, "done", G_CALLBACK (screenshot_dialog_done), handle);

  gtk_widget_realize (dialog);

  if (external_parent)
    external_window_set_parent_of (external_parent, gtk_widget_get_window (dialog));

  gtk_widget_show (dialog);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  return TRUE;
}

gboolean
screenshot_init (GDBusConnection *bus,
                 GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_screenshot_skeleton_new ());

  g_signal_connect (helper, "handle-screenshot", G_CALLBACK (handle_screenshot), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  shell = org_gnome_shell_screenshot_proxy_new_sync (bus,
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
