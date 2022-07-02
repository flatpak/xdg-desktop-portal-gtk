/*
 * Copyright © 2016 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Matthias Clasen <mclasen@redhat.com>
 */

#define _GNU_SOURCE 1

#include "config.h"

#include "appchooser.h"
#include "request.h"
#include "utils.h"

#include <string.h>

#include <gtk/gtk.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>

#include <glib/gi18n.h>

#include "xdg-desktop-portal-dbus.h"

#include "appchooserdialog.h"
#include "externalwindow.h"

static GHashTable *handles;

typedef struct {
  XdpImplAppChooser *impl;
  GDBusMethodInvocation *invocation;
  Request *request;
  GtkWidget *dialog;
  ExternalWindow *external_parent;

  char *chosen;
  int response;

} AppDialogHandle;

static void
app_dialog_handle_free (gpointer data)
{
  AppDialogHandle *handle = data;

  g_hash_table_remove (handles, handle->request->id);
  g_clear_object (&handle->external_parent);
  g_object_unref (handle->request);
  g_object_unref (handle->dialog);
  g_free (handle->chosen);

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
  g_variant_builder_add (&opt_builder, "{sv}", "choice", g_variant_new_string (handle->chosen ? handle->chosen : ""));

  if (handle->request->exported)
    request_unexport (handle->request);

  xdp_impl_app_chooser_complete_choose_application (handle->impl,
                                                    handle->invocation,
                                                    handle->response,
                                                    g_variant_builder_end (&opt_builder));

  app_dialog_handle_close (handle);
}

static void
handle_app_chooser_close (AppChooserDialog *dialog,
                          gpointer data)
{
  AppDialogHandle *handle = data;
  GAppInfo *info;

  info = app_chooser_dialog_get_info (dialog);
  if (info != NULL)
    {
      const char *desktop_id = g_app_info_get_id (info);
      handle->response = 0;
      handle->chosen = g_strndup (desktop_id, strlen (desktop_id) - strlen (".desktop"));
    }
  else
    {
      handle->response = 1;
      handle->chosen = NULL;
    }

  send_response (handle);
}

static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              AppDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  if (handle->request->exported)
    request_unexport (handle->request);

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  xdp_impl_app_chooser_complete_choose_application (handle->impl,
                                                    handle->invocation,
                                                    2,
                                                    g_variant_builder_end (&opt_builder));
  app_dialog_handle_close (handle);
  xdp_impl_request_complete_close (object, invocation);

  return TRUE;
}

static gboolean
handle_choose_application (XdpImplAppChooser *object,
                           GDBusMethodInvocation *invocation,
                           const char *arg_handle,
                           const char *arg_app_id,
                           const char *arg_parent_window,
                           const char **choices,
                           GVariant *arg_options)
{
  g_autoptr(Request) request = NULL;
  GtkWidget *dialog;
  AppDialogHandle *handle;
  const char *sender;
  const char *latest_chosen_id;
  const char *content_type;
  const char *location;
  gboolean modal;
  GdkDisplay *display;
  GdkScreen *screen;
  ExternalWindow *external_parent = NULL;
  GtkWidget *fake_parent;

  sender = g_dbus_method_invocation_get_sender (invocation);

  request = request_new (sender, arg_app_id, arg_handle);

  if (!g_variant_lookup (arg_options, "last_choice", "&s", &latest_chosen_id))
    latest_chosen_id = NULL;
  if (!g_variant_lookup (arg_options, "modal", "b", &modal))
    modal = TRUE;
  if (!g_variant_lookup (arg_options, "content_type", "&s", &content_type))
    content_type = NULL;
  if (!g_variant_lookup (arg_options, "filename", "&s", &location) &&
      !g_variant_lookup (arg_options, "uri", "&s", &location))
    location = NULL;

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

  dialog = GTK_WIDGET (app_chooser_dialog_new (choices, latest_chosen_id, content_type, location));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fake_parent));

  gtk_window_set_modal (GTK_WINDOW (dialog), modal);

  handle = g_new0 (AppDialogHandle, 1);
  handle->impl = object;
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->dialog = g_object_ref (dialog);
  handle->external_parent = external_parent;

  g_hash_table_insert (handles, handle->request->id, handle);

  g_signal_connect (request, "handle-close",
                    G_CALLBACK (handle_close), handle);

  g_signal_connect (dialog, "close",
                    G_CALLBACK (handle_app_chooser_close), handle);

  gtk_widget_realize (dialog);

  if (external_parent)
    external_window_set_parent_of (external_parent, gtk_widget_get_window (dialog));

  gtk_window_present (GTK_WINDOW (dialog));

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  return TRUE;
}

static gboolean
handle_update_choices (XdpImplAppChooser *object,
                       GDBusMethodInvocation *invocation,
                       const char *arg_handle,
                       const char **choices)
{
  AppDialogHandle *handle;
  g_autofree char *a = NULL;

  handle = g_hash_table_lookup (handles, arg_handle);

  if (handle == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             XDG_DESKTOP_PORTAL_ERROR,
                                             XDG_DESKTOP_PORTAL_ERROR_NOT_FOUND,
                                             "Request not found");
      return TRUE;
    }

  g_debug ("Updating choices: %s\n", a = g_strjoinv (", ", (char **)choices));

  app_chooser_dialog_update_choices (APP_CHOOSER_DIALOG (handle->dialog), choices);

  g_dbus_method_invocation_return_value (invocation, NULL);

  return TRUE;
}

gboolean
app_chooser_init (GDBusConnection *bus,
                  GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_app_chooser_skeleton_new ());

  g_signal_connect (helper, "handle-choose-application", G_CALLBACK (handle_choose_application), NULL);
  g_signal_connect (helper, "handle-update-choices", G_CALLBACK (handle_update_choices), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  handles = g_hash_table_new (g_str_hash, g_str_equal);

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
