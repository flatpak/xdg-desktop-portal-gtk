/*
 * usb.c
 *
 * Copyright 2023 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 * Copyright © 2024 GNOME Foundation Inc.
 * Copyright © 2026 Nuhiat Arefin <nuhiatarefin@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *       Hubert Figuière <hub@figuiere.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* NOTE: This file is adapted from xdg-desktop-portal-gnome. */

#define _GNU_SOURCE 1

#include "config.h"

#include <glib/gi18n.h>

#include <gtk/gtk.h>

#include <gio/gio.h>

#include "xdg-desktop-portal-dbus.h"

#include "externalwindow.h"
#include "request.h"
#include "usb.h"
#include "usbdialog.h"
#include "utils.h"

typedef struct
{
  XdpImplUsb *impl;
  GDBusMethodInvocation *invocation;
  Request *request;

  GtkWidget *dialog;
  ExternalWindow *external_parent;
  GVariant *results;

  int response;
} UsbDialogHandle;

static GVariant *
empty_results (void)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);

  return g_variant_builder_end (&builder);
}

static GVariant *
build_devices_results (GVariant *devices)
{
  GVariantBuilder results_builder;
  GVariantBuilder devices_builder;
  GVariantIter iter;
  const char *id;
  GVariant *device_info;
  GVariant *access_options;

  g_variant_builder_init (&results_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_init (&devices_builder, G_VARIANT_TYPE ("a(sa{sv})"));

  g_variant_iter_init (&iter, devices);
  while (g_variant_iter_next (&iter,
                              "(&s@a{sv}@a{sv})",
                              &id,
                              &device_info,
                              &access_options))
    {
      g_variant_builder_add (&devices_builder,
                             "(s@a{sv})",
                             id,
                             access_options);
      g_variant_unref (device_info);
      g_variant_unref (access_options);
    }

  g_variant_builder_add (&results_builder,
                         "{sv}",
                         "devices",
                         g_variant_builder_end (&devices_builder));

  return g_variant_builder_end (&results_builder);
}

static void
usb_dialog_handle_free (gpointer data)
{
  UsbDialogHandle *handle = data;

  g_clear_object (&handle->external_parent);
  g_clear_object (&handle->request);
  g_clear_object (&handle->dialog);
  g_clear_pointer (&handle->results, g_variant_unref);

  g_free (handle);
}

static void
usb_dialog_handle_close (UsbDialogHandle *handle)
{
  gtk_widget_destroy (handle->dialog);
  usb_dialog_handle_free (handle);
}

static void
complete_acquire_devices (UsbDialogHandle *handle,
                          guint            response,
                          GVariant        *results)
{
  if (handle->request->exported)
    request_unexport (handle->request);

  xdp_impl_usb_complete_acquire_devices (handle->impl,
                                         handle->invocation,
                                         response,
                                         results);
}

static void
on_usb_dialog_response_cb (UsbDialog       *dialog,
                           int              response,
                           UsbDialogHandle *handle)
{
  g_autoptr(GVariant) results = NULL;

  (void) dialog;

  switch (response)
    {
    default:
      g_warning ("Unexpected response: %d", response);
      handle->response = 2;
      results = g_variant_ref_sink (empty_results ());
      break;

    case GTK_RESPONSE_CANCEL:
      handle->response = 1;
      results = g_variant_ref_sink (empty_results ());
      break;

    case GTK_RESPONSE_APPLY:
      handle->response = 0;
      results = g_variant_ref (handle->results);
      break;
    }

  complete_acquire_devices (handle, handle->response, results);
  usb_dialog_handle_close (handle);
}

static gboolean
handle_close (XdpImplRequest        *object,
              GDBusMethodInvocation *invocation,
              UsbDialogHandle       *handle)
{
  g_autoptr(GVariant) results = NULL;

  (void) object;
  (void) invocation;

  results = g_variant_ref_sink (empty_results ());
  complete_acquire_devices (handle, 2, results);
  usb_dialog_handle_close (handle);

  return FALSE;
}

static gboolean
handle_acquire_devices (XdpImplUsb            *object,
                        GDBusMethodInvocation *invocation,
                        const char            *arg_handle,
                        const char            *arg_parent_window,
                        const char            *arg_app_id,
                        GVariant              *arg_devices,
                        GVariant              *arg_options)
{
  g_autoptr(Request) request = NULL;
  const char *sender;
  UsbDialogHandle *handle;
  GtkWidget *dialog;
  ExternalWindow *external_parent = NULL;
  GdkDisplay *display;
  GdkScreen *screen;
  GtkWidget *fake_parent;

  (void) arg_options;

  sender = g_dbus_method_invocation_get_sender (invocation);
  request = request_new (sender, arg_app_id, arg_handle);

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

  dialog = GTK_WIDGET (usb_dialog_new (arg_app_id, arg_devices));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fake_parent));

  handle = g_new0 (UsbDialogHandle, 1);
  handle->impl = object;
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->dialog = g_object_ref_sink (dialog);
  handle->external_parent = external_parent;
  handle->results = g_variant_ref_sink (build_devices_results (arg_devices));
  handle->response = 2;

  g_signal_connect (request,
                    "handle-close",
                    G_CALLBACK (handle_close),
                    handle);

  g_signal_connect (dialog,
                    "response",
                    G_CALLBACK (on_usb_dialog_response_cb),
                    handle);

  gtk_widget_realize (dialog);

  if (external_parent)
    external_window_set_parent_of (external_parent, gtk_widget_get_window (dialog));

  gtk_widget_show (dialog);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  return TRUE;
}

gboolean
usb_init (GDBusConnection *bus,
          GError         **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_usb_skeleton_new ());

  g_signal_connect (helper,
                    "handle-acquire-devices",
                    G_CALLBACK (handle_acquire_devices),
                    NULL);
  xdp_impl_usb_set_version (XDP_IMPL_USB (helper), 1);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
