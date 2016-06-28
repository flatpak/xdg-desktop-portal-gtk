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

#include <string.h>

#include <gtk/gtk.h>
#include <gtk/gtkunixprint.h>

#include <gio/gio.h>

#include "xdg-desktop-portal-dbus.h"

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include "print.h"
#include "xdg-desktop-portal-gtk.h"

typedef struct {
  DialogHandle base;

  GtkWidget *dialog;

  char *filename;
  int response;

} PrintDialogHandle;

static PrintDialogHandle *
print_dialog_handle_new (const char *app_id,
                         const char *sender,
                         const char *filename,
                         GtkWidget *dialog,
                         GDBusInterfaceSkeleton *skeleton)
{
  PrintDialogHandle *handle = g_new0 (PrintDialogHandle, 1);

  handle->base.app_id = g_strdup (app_id);
  handle->base.sender = g_strdup (sender);
  handle->base.skeleton = g_object_ref (skeleton);
  handle->filename = g_strdup (filename);
  handle->dialog = g_object_ref (dialog);
  dialog_handle_register (&handle->base);

  /* TODO: Track lifetime of sender and close handle */

  return handle;
}

static void
print_dialog_handle_free (PrintDialogHandle *handle)
{
  dialog_handle_unregister (&handle->base);
  g_free (handle->base.app_id);
  g_free (handle->base.sender);
  g_free (handle->filename);
  g_object_unref (handle->base.skeleton);
  g_object_unref (handle->dialog);
  g_free (handle);
}

static void
print_dialog_handle_close (PrintDialogHandle *handle)
{
  gtk_widget_destroy (handle->dialog);
  print_dialog_handle_free (handle);
}

static void
send_response (PrintDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  
  g_dbus_connection_emit_signal (g_dbus_interface_skeleton_get_connection (handle->base.skeleton),
                                 "org.freedesktop.portal.Desktop",
                                 "/org/freedesktop/portal/desktop",
                                 "org.freedesktop.impl.portal.Print",
                                 "PrintFileResponse",
                                 g_variant_new ("(sou@a{sv})",
                                                handle->base.sender,
                                                handle->base.id,
                                                handle->response,
                                                g_variant_builder_end (&opt_builder)),
                                 NULL);

  print_dialog_handle_close (handle);
}

static void
handle_print_dialog_response (GtkDialog *dialog,
                              gint response,
                              gpointer data)
{
  PrintDialogHandle *handle = data;

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
        GtkPageSetup *setup;
        GtkPrintSettings *settings;
        GtkPrinter *printer;
        GtkPrintJob *job;
        GError *error = NULL;

        setup = gtk_print_unix_dialog_get_page_setup (GTK_PRINT_UNIX_DIALOG (handle->dialog));
        settings = gtk_print_unix_dialog_get_settings (GTK_PRINT_UNIX_DIALOG (handle->dialog));
        printer = gtk_print_unix_dialog_get_selected_printer (GTK_PRINT_UNIX_DIALOG (handle->dialog))
;
        job = gtk_print_job_new ("", //TODO send title along
                                 printer, settings, setup);
        g_clear_object (&settings);

        if (!gtk_print_job_set_source_file (job, handle->filename, &error))
          {
            // TODO report error;
            g_warning ("printing failed: %s\n", error->message);
            g_error_free (error);
          }
        else
          {
            g_print ("send %s to printer\n", handle->filename);
            gtk_print_job_send (job, NULL, NULL, NULL); //TODO: wait ?
          }
        g_object_unref (job);

        handle->response = 0;
      }
      break;
    }

  send_response (handle);
}

static gboolean
handle_print_file (XdpPrint *object,
                   GDBusMethodInvocation *invocation,
                   const gchar *arg_sender,
                   const gchar *arg_app_id,
                   const gchar *arg_parent_window,
                   const gchar *arg_title,
                   const gchar *arg_filename,
                   GVariant *arg_options)
{
  XdpPrint *print = XDP_PRINT (g_dbus_method_invocation_get_user_data (invocation));
  GtkWidget *dialog;
  GdkWindow *foreign_parent = NULL;
  PrintDialogHandle *handle;

 #ifdef GDK_WINDOWING_X11
  if (g_str_has_prefix (arg_parent_window, "x11:"))
    {
      int xid;

      if (sscanf (arg_parent_window, "x11:%x", &xid) != 1)
        g_warning ("invalid xid");
      else
        foreign_parent = gdk_x11_window_foreign_new_for_display (gdk_display_get_default (), xid);
    }
#endif
  else
    g_warning ("Unhandled parent window type %s\n", arg_parent_window);

  dialog = gtk_print_unix_dialog_new (arg_title, NULL);
  gtk_print_unix_dialog_set_manual_capabilities (GTK_PRINT_UNIX_DIALOG (dialog), 0);

  handle = print_dialog_handle_new (arg_app_id, arg_sender, arg_filename, dialog, G_DBUS_INTERFACE_SKELETON (object));

  g_signal_connect (dialog, "response", G_CALLBACK (handle_print_dialog_response), handle);

  gtk_widget_realize (dialog);

  if (foreign_parent)
    gdk_window_set_transient_for (gtk_widget_get_window (dialog), foreign_parent);

  gtk_widget_show (dialog);

  xdp_print_complete_print_file (print, invocation, handle->base.id);

  return TRUE;
}

static gboolean
handle_print_close (XdpPrint *object,
                    GDBusMethodInvocation *invocation,
                    const gchar *arg_sender,
                    const gchar *arg_app_id,
                    const gchar *arg_handle)
{
  PrintDialogHandle *handle;

  handle = (PrintDialogHandle *)dialog_handle_find (arg_sender, arg_app_id, arg_handle,
                                                    XDP_TYPE_PRINT_SKELETON);

  if (handle != NULL)
    {
      print_dialog_handle_close (handle);
      xdp_print_complete_close (object, invocation);
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
print_init (GDBusConnection *bus,
            GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_print_skeleton_new ());

  g_signal_connect (helper, "handle-print-file", G_CALLBACK (handle_print_file), NULL);
  g_signal_connect (helper, "handle-close", G_CALLBACK (handle_print_close), NULL);


  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         "/org/freedesktop/portal/desktop",
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
