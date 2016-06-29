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
#include "xdg-desktop-portal-gtk.h"

#include <string.h>

#include <gtk/gtk.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>

#include <glib/gi18n.h>

#include "xdg-desktop-portal-dbus.h"

#include "appchooserdialog.h"

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

typedef struct {
  DialogHandle base;

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
send_response (AppDialogHandle *handle,
               const char *chosen)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  
  g_dbus_connection_emit_signal (g_dbus_interface_skeleton_get_connection (handle->base.skeleton),
                                 "org.freedesktop.portal.Desktop",
                                 "/org/freedesktop/portal/desktop",
                                 "org.freedesktop.impl.portal.AppChooser",
                                 "ChooseApplicationResponse",
                                 g_variant_new ("(sous@a{sv})",
                                                handle->base.sender,
                                                handle->base.id,
                                                handle->response,
                                                chosen,
                                                g_variant_builder_end (&opt_builder)),
                                 NULL);

  app_dialog_handle_close (handle);
}

static void
handle_app_chooser_done (GtkDialog *dialog,
                         GAppInfo *info,
                         gpointer data)
{
  AppDialogHandle *handle = data;
  const char *chosen;

  if (info != NULL)
    {
      handle->response = 0;
      chosen = g_app_info_get_id (info);
    }
  else
    {
      handle->response = 1;
      chosen = "";
    }

  send_response (handle, chosen);
}

static gboolean
handle_choose_application (XdpAppChooser *object,
                           GDBusMethodInvocation *invocation,
                           const char *arg_sender,
                           const char *arg_app_id,
                           const char *arg_parent_window,
                           const char **choices,
                           GVariant *arg_options)
{
  XdpAppChooser *chooser = XDP_APP_CHOOSER (g_dbus_method_invocation_get_user_data (invocation));
  GtkWidget *dialog;
  AppDialogHandle *handle;
  const char *cancel_label;
  const char *accept_label;
  const char *title;
  const char *heading;
  const char *latest_chosen_id;

  if (!g_variant_lookup (arg_options, "accept_label", "&s", &accept_label))
    accept_label = _("_Select");
  if (!g_variant_lookup (arg_options, "title", "&s", &title))
    title = _("Open With");
  if (!g_variant_lookup (arg_options, "heading", "&s", &heading))
    heading = _("Select application");
  if (!g_variant_lookup (arg_options, "latest-choice", "&s", &latest_chosen_id))
    latest_chosen_id = NULL;

  cancel_label = _("_Cancel");

  dialog = GTK_WIDGET (app_chooser_dialog_new (choices, latest_chosen_id, cancel_label, accept_label, title, heading));

  handle = app_dialog_handle_new (arg_app_id, arg_sender, dialog, G_DBUS_INTERFACE_SKELETON (object));

  g_signal_connect (dialog, "done", G_CALLBACK (handle_app_chooser_done), handle);

  gtk_window_present (GTK_WINDOW (dialog));

  xdp_app_chooser_complete_choose_application (chooser, invocation, handle->base.id);

  return TRUE;
}

static gboolean
handle_close (XdpAppChooser *object,
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

  g_signal_connect (helper, "handle-choose-application", G_CALLBACK (handle_choose_application), NULL);
  g_signal_connect (helper, "handle-close", G_CALLBACK (handle_close), NULL);


  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         "/org/freedesktop/portal/desktop",
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
