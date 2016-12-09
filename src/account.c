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

#include "account.h"
#include "accountdialog.h"
#include "request.h"
#include "utils.h"
#include "externalwindow.h"

static OrgFreedesktopAccountsUser *user;

typedef struct {
  XdpImplAccount *impl;
  GDBusMethodInvocation *invocation;
  Request *request;

  GtkWidget *dialog;
  ExternalWindow *external_parent;

  int response;
  char *user_name;
  char *real_name;
  char *icon_uri;
} AccountDialogHandle;

static void
account_dialog_handle_free (gpointer data)
{
  AccountDialogHandle *handle = data;

  g_clear_object (&handle->external_parent);
  g_object_unref (handle->request);
  g_object_unref (handle->dialog);
  g_free (handle->user_name);
  g_free (handle->real_name);
  g_free (handle->icon_uri);

  g_free (handle);
}

static void
account_dialog_handle_close (AccountDialogHandle *handle)
{
  gtk_widget_destroy (handle->dialog);
  account_dialog_handle_free (handle);
}

static void
send_response (AccountDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&opt_builder, "{sv}", "id", g_variant_new_string (handle->user_name ? handle->user_name : ""));
  g_variant_builder_add (&opt_builder, "{sv}", "name", g_variant_new_string (handle->real_name ? handle->real_name : ""));
  g_variant_builder_add (&opt_builder, "{sv}", "image", g_variant_new_string (handle->icon_uri ? handle->icon_uri : ""));

  if (handle->request->exported)
    request_unexport (handle->request);

  xdp_impl_account_complete_get_user_information (handle->impl,
                                                  handle->invocation,
                                                  handle->response,
                                                  g_variant_builder_end (&opt_builder));

  account_dialog_handle_close (handle);
}

static void
account_dialog_done (GtkWidget *widget,
                     int response,
                     const char *user_name,
                     const char *real_name,
                     const char *icon_file,
                     gpointer user_data)
{
  AccountDialogHandle *handle = user_data;

  g_clear_pointer (&handle->user_name, g_free);
  g_clear_pointer (&handle->real_name, g_free);
  g_clear_pointer (&handle->icon_uri, g_free);

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
      handle->user_name = g_strdup (user_name);
      handle->real_name = g_strdup (real_name);
      if (icon_file)
        handle->icon_uri = g_filename_to_uri (icon_file, NULL, NULL);
      handle->response = 0;
      break;
    }

  send_response (handle);
}

static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              AccountDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  xdp_impl_account_complete_get_user_information (handle->impl,
                                                  handle->invocation,
                                                  2,
                                                  g_variant_builder_end (&opt_builder));
  account_dialog_handle_close (handle);

  return FALSE;
}

static gboolean
handle_get_user_information (XdpImplAccount *object,
                             GDBusMethodInvocation *invocation,
                             const char *arg_handle,
                             const char *arg_app_id,
                             const char *arg_parent_window,
                             GVariant *arg_options)
{
  g_autoptr(Request) request = NULL;
  const char *sender;
  AccountDialogHandle *handle;
  g_autoptr(GError) error = NULL;
  const char *user_name;
  const char *real_name;
  const char *icon_file;
  GtkWidget *dialog;
  GdkDisplay *display;
  GdkScreen *screen;
  ExternalWindow *external_parent = NULL;
  GtkWidget *fake_parent;
  const char *reason;

  sender = g_dbus_method_invocation_get_sender (invocation);

  request = request_new (sender, arg_app_id, arg_handle);

  user_name = org_freedesktop_accounts_user_get_user_name (user);
  real_name = org_freedesktop_accounts_user_get_real_name (user);
  icon_file = org_freedesktop_accounts_user_get_icon_file (user);

  if (!g_variant_lookup (arg_options, "reason", "&s", &reason))
    reason = NULL;

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

  dialog = GTK_WIDGET (account_dialog_new (arg_app_id, user_name, real_name, icon_file, reason));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fake_parent));
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

  handle = g_new0 (AccountDialogHandle, 1);
  handle->impl = object;
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->dialog = g_object_ref (dialog);
  handle->external_parent = external_parent;
  handle->user_name = g_strdup (user_name);
  handle->real_name = g_strdup (real_name);
  handle->icon_uri = g_filename_to_uri (icon_file, NULL, NULL);

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

  g_signal_connect (dialog, "done", G_CALLBACK (account_dialog_done), handle);

  gtk_widget_realize (dialog);

  if (external_parent)
    external_window_set_parent_of (external_parent, gtk_widget_get_window (dialog));

  gtk_widget_show (dialog);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  return TRUE;
}

static GDBusConnection *system_bus;

gboolean
account_init (GDBusConnection *bus,
              GError **error)
{
  GDBusInterfaceSkeleton *helper;
  g_autofree char *object_path = NULL;

  system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
  if (system_bus == NULL)
    return FALSE;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_account_skeleton_new ());

  g_signal_connect (helper, "handle-get-user-information", G_CALLBACK (handle_get_user_information), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  object_path = g_strdup_printf ("/org/freedesktop/Accounts/User%d", getuid ());

  user = org_freedesktop_accounts_user_proxy_new_sync (system_bus,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                       "org.freedesktop.Accounts",
                                                       object_path,
                                                       NULL,
                                                       error);
  if (user == NULL)
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
