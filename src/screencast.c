/*
 * Copyright © 2017 Red Hat, Inc
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
 */

#include "config.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <stdint.h>

#include "xdg-desktop-portal-dbus.h"

#include "screencast.h"
#include "screencastwidget.h"
#include "screencastdialog.h"
#include "gnomescreencast.h"
#include "remotedesktop.h"
#include "displaystatetracker.h"
#include "externalwindow.h"
#include "request.h"
#include "session.h"
#include "utils.h"

typedef struct _ScreenCastDialogHandle ScreenCastDialogHandle;

typedef struct _ScreenCastSession
{
  Session parent;

  GnomeScreenCastSession *gnome_screen_cast_session;
  gulong session_ready_handler_id;
  gulong session_closed_handler_id;

  char *parent_window;

  ScreenCastSelection select;

  GDBusMethodInvocation *start_invocation;
  ScreenCastDialogHandle *dialog_handle;
} ScreenCastSession;

typedef struct _ScreenCastSessionClass
{
  SessionClass parent_class;
} ScreenCastSessionClass;

typedef struct _ScreenCastDialogHandle
{
  Request *request;
  ScreenCastSession *session;

  GtkWidget *dialog;
  ExternalWindow *external_parent;

  int response;
} ScreenCastDialogHandle;

static GDBusConnection *impl_connection;
static GDBusInterfaceSkeleton *impl;

static GnomeScreenCast *gnome_screen_cast;

GType screen_cast_session_get_type (void);
G_DEFINE_TYPE (ScreenCastSession, screen_cast_session, session_get_type ())

static void
start_done (ScreenCastSession *session);

static gboolean
start_session (ScreenCastSession *session,
               GVariant *selections,
               GError **error);

static void
cancel_start_session (ScreenCastSession *session,
                      int response);

static gboolean
is_screen_cast_session (Session *session)
{
  return G_TYPE_CHECK_INSTANCE_TYPE (session, screen_cast_session_get_type ());
}

static void
screen_cast_dialog_handle_free (ScreenCastDialogHandle *dialog_handle)
{
  g_clear_pointer (&dialog_handle->dialog, gtk_widget_destroy);
  g_clear_object (&dialog_handle->external_parent);
  g_object_unref (dialog_handle->request);

  g_free (dialog_handle);
}

static void
screen_cast_dialog_handle_close (ScreenCastDialogHandle *dialog_handle)
{
  screen_cast_dialog_handle_free (dialog_handle);
}

static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              ScreenCastDialogHandle *dialog_handle)
{
  cancel_start_session (dialog_handle->session, 2);

  screen_cast_dialog_handle_close (dialog_handle);

  return FALSE;
}

static void
screen_cast_dialog_done (GtkWidget *widget,
                         int dialog_response,
                         GVariant *selections,
                         ScreenCastDialogHandle *dialog_handle)
{
  int response;

  switch (dialog_response)
    {
    default:
      g_warning ("Unexpected response: %d", dialog_response);
      /* Fall through */
    case GTK_RESPONSE_DELETE_EVENT:
      response = 2;
      break;

    case GTK_RESPONSE_CANCEL:
      response = 1;
      break;

    case GTK_RESPONSE_OK:
      response = 0;
      break;
    }

  if (response == 0)
    {
      g_autoptr(GError) error = NULL;

      if (!start_session (dialog_handle->session, selections, &error))
        {
          g_warning ("Failed to start session: %s", error->message);
          response = 2;
        }
    }

  if (response != 0)
    cancel_start_session (dialog_handle->session, response);

  if (dialog_handle->request->exported)
    request_unexport (dialog_handle->request);
}

static ScreenCastDialogHandle *
create_screen_cast_dialog (ScreenCastSession *session,
                           GDBusMethodInvocation *invocation,
                           Request *request,
                           const char *parent_window)
{
  ScreenCastDialogHandle *dialog_handle;
  ExternalWindow *external_parent;
  GdkScreen *screen;
  GdkDisplay *display;
  GtkWidget *fake_parent;
  GtkWidget *dialog;

  if (parent_window)
    {
      external_parent = create_external_window_from_handle (parent_window);
      if (!external_parent)
        g_warning ("Failed to associate portal window with parent window %s",
                   parent_window);
    }
  else
    {
      external_parent = NULL;
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

  dialog = GTK_WIDGET (screen_cast_dialog_new (request->app_id,
                                               &session->select));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fake_parent));
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

  dialog_handle = g_new0 (ScreenCastDialogHandle, 1);
  dialog_handle->session = session;
  dialog_handle->request = g_object_ref (request);
  dialog_handle->external_parent = external_parent;
  dialog_handle->dialog = dialog;

  g_signal_connect (request, "handle-close",
                    G_CALLBACK (handle_close), dialog_handle);

  g_signal_connect (dialog, "done",
                    G_CALLBACK (screen_cast_dialog_done), dialog_handle);

  gtk_widget_realize (dialog);

  if (external_parent)
    external_window_set_parent_of (external_parent, gtk_widget_get_window (dialog));

  gtk_widget_show (dialog);

  return dialog_handle;
}

static ScreenCastSession *
screen_cast_session_new (const char *app_id,
                         const char *session_handle)
{
  ScreenCastSession *screen_cast_session;

  screen_cast_session = g_object_new (screen_cast_session_get_type (),
                                      "id", session_handle,
                                      NULL);

  return screen_cast_session;
}

static gboolean
handle_create_session (XdpImplScreenCast *object,
                       GDBusMethodInvocation *invocation,
                       const char *arg_handle,
                       const char *arg_session_handle,
                       const char *arg_app_id,
                       GVariant *arg_options)
{
  g_autoptr(GError) error = NULL;
  int response;
  Session *session;
  GVariantBuilder results_builder;

  session = (Session *)screen_cast_session_new (arg_app_id,
                                                arg_session_handle);

  if (!session_export (session,
                       g_dbus_method_invocation_get_connection (invocation),
                       &error))
    {
      g_clear_object (&session);
      g_warning ("Failed to create screen cast session: %s", error->message);
      response = 2;
      goto out;
    }

  response = 0;

out:
  g_variant_builder_init (&results_builder, G_VARIANT_TYPE_VARDICT);
  xdp_impl_screen_cast_complete_create_session (object,
                                                invocation,
                                                response,
                                                g_variant_builder_end (&results_builder));

  return TRUE;
}

static gboolean
handle_select_sources (XdpImplScreenCast *object,
                       GDBusMethodInvocation *invocation,
                       const char *arg_handle,
                       const char *arg_session_handle,
                       const char *arg_app_id,
                       GVariant *arg_options)
{
  Session *session;
  int response;
  uint32_t types;
  gboolean multiple;
  ScreenCastCursorMode cursor_mode;
  ScreenCastSelection select;
  GVariantBuilder results_builder;
  GVariant *results;

  session = lookup_session (arg_session_handle);
  if (!session)
    {
      g_warning ("Tried to select sources on non-existing %s", arg_session_handle);
      response = 2;
      goto out;
    }

  if (!g_variant_lookup (arg_options, "multiple", "b", &multiple))
    multiple = FALSE;

  if (!g_variant_lookup (arg_options, "types", "u", &types))
    types = SCREEN_CAST_SOURCE_TYPE_MONITOR;

  if (!(types & (SCREEN_CAST_SOURCE_TYPE_MONITOR |
                 SCREEN_CAST_SOURCE_TYPE_WINDOW)))
    {
      g_warning ("Unknown screen cast source type");
      response = 2;
      goto out;
    }

  if (!g_variant_lookup (arg_options, "cursor_mode", "u", &cursor_mode))
    cursor_mode = SCREEN_CAST_CURSOR_MODE_HIDDEN;

  switch (cursor_mode)
    {
    case SCREEN_CAST_CURSOR_MODE_HIDDEN:
    case SCREEN_CAST_CURSOR_MODE_EMBEDDED:
    case SCREEN_CAST_CURSOR_MODE_METADATA:
      break;
    default:
      g_warning ("Unknown screen cast cursor mode");
      response = 2;
      goto out;
    }

  select.multiple = multiple;
  select.source_types = types;
  select.cursor_mode = cursor_mode;

  if (is_screen_cast_session (session))
    {
      ScreenCastSession *screen_cast_session = (ScreenCastSession *)session;

      screen_cast_session->select = select;
      response = 0;
    }
  else if (is_remote_desktop_session (session))
    {
      RemoteDesktopSession *remote_desktop_session =
        (RemoteDesktopSession *)session;

      remote_desktop_session_sources_selected (remote_desktop_session, &select);
      response = 0;
    }
  else
    {
      g_warning ("Tried to select sources on invalid session type");
      response = 2;
    }

out:
  g_variant_builder_init (&results_builder, G_VARIANT_TYPE_VARDICT);
  results = g_variant_builder_end (&results_builder);
  xdp_impl_screen_cast_complete_select_sources (object, invocation,
                                                response, results);

  return TRUE;
}

static void
start_done (ScreenCastSession *screen_cast_session)
{
  GnomeScreenCastSession *gnome_screen_cast_session;
  GVariantBuilder streams_builder;
  GVariantBuilder results_builder;

  g_variant_builder_init (&results_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_init (&streams_builder, G_VARIANT_TYPE ("a(ua{sv})"));

  gnome_screen_cast_session = screen_cast_session->gnome_screen_cast_session;
  gnome_screen_cast_session_add_stream_properties (gnome_screen_cast_session,
                                                   &streams_builder);

  g_variant_builder_add (&results_builder, "{sv}",
                         "streams",
                         g_variant_builder_end (&streams_builder));

  xdp_impl_screen_cast_complete_start (XDP_IMPL_SCREEN_CAST (impl),
                                       screen_cast_session->start_invocation, 0,
                                       g_variant_builder_end (&results_builder));
  screen_cast_session->start_invocation = NULL;
}

static void
on_gnome_screen_cast_session_ready (GnomeScreenCastSession *gnome_screen_cast_session,
                                    ScreenCastSession *screen_cast_session)
{
  start_done (screen_cast_session);
}

static void
on_gnome_screen_cast_session_closed (GnomeScreenCastSession *gnome_screen_cast_session,
                                     ScreenCastSession *screen_cast_session)
{
  session_close ((Session *)screen_cast_session);
}

static gboolean
start_session (ScreenCastSession *screen_cast_session,
               GVariant *selections,
               GError **error)
{
  GnomeScreenCastSession *gnome_screen_cast_session;
  g_autoptr(GVariant) source_selections = NULL;

  gnome_screen_cast_session =
    gnome_screen_cast_create_session (gnome_screen_cast, NULL, error);
  if (!gnome_screen_cast_session)
    return FALSE;

  screen_cast_session->gnome_screen_cast_session = gnome_screen_cast_session;

  screen_cast_session->session_ready_handler_id =
    g_signal_connect (gnome_screen_cast_session, "ready",
                      G_CALLBACK (on_gnome_screen_cast_session_ready),
                      screen_cast_session);
  screen_cast_session->session_closed_handler_id =
    g_signal_connect (gnome_screen_cast_session, "closed",
                      G_CALLBACK (on_gnome_screen_cast_session_closed),
                      screen_cast_session);

  g_variant_lookup (selections, "selected_screen_cast_sources", "@a(u?)",
                    &source_selections);
  if (!gnome_screen_cast_session_record_selections (gnome_screen_cast_session,
                                                    source_selections,
                                                    &screen_cast_session->select,
                                                    error))
    return FALSE;

  if (!gnome_screen_cast_session_start (gnome_screen_cast_session, error))
    return FALSE;

  return TRUE;
}

static void
cancel_start_session (ScreenCastSession *screen_cast_session,
                      int response)
{
  GVariantBuilder results_builder;

  g_variant_builder_init (&results_builder, G_VARIANT_TYPE_VARDICT);
  xdp_impl_screen_cast_complete_start (XDP_IMPL_SCREEN_CAST (impl),
                                       screen_cast_session->start_invocation,
                                       response,
                                       g_variant_builder_end (&results_builder));
}

static gboolean
handle_start (XdpImplScreenCast *object,
              GDBusMethodInvocation *invocation,
              const char *arg_handle,
              const char *arg_session_handle,
              const char *arg_app_id,
              const char *arg_parent_window,
              GVariant *arg_options)
{
  const char *sender;
  g_autoptr(Request) request = NULL;
  ScreenCastSession *screen_cast_session;
  ScreenCastDialogHandle *dialog_handle;
  GVariantBuilder results_builder;

  sender = g_dbus_method_invocation_get_sender (invocation);
  request = request_new (sender, arg_app_id, arg_handle);
  request_export (request,
                  g_dbus_method_invocation_get_connection (invocation));

  screen_cast_session =
    (ScreenCastSession *)lookup_session (arg_session_handle);
  if (!screen_cast_session)
    {
      g_warning ("Attempted to start non existing screen cast session");
      goto err;
    }

  if (screen_cast_session->dialog_handle)
    {
      g_warning ("Screen cast dialog already open");
      goto err;
    }

  dialog_handle = create_screen_cast_dialog (screen_cast_session,
                                             invocation,
                                             request,
                                             arg_parent_window);


  screen_cast_session->start_invocation = invocation;
  screen_cast_session->dialog_handle = dialog_handle;

  return TRUE;

err:
  g_variant_builder_init (&results_builder, G_VARIANT_TYPE ("a(ua{sv}"));
  xdp_impl_screen_cast_complete_start (object, invocation, 2,
                                       g_variant_builder_end (&results_builder));

  return TRUE;
}

static void
on_gnome_screen_cast_enabled (GnomeScreenCast *gnome_screen_cast)
{
  int gnome_api_version;
  ScreenCastSourceType available_source_types;
  ScreenCastCursorMode available_cursor_modes;
  g_autoptr(GError) error = NULL;

  impl = G_DBUS_INTERFACE_SKELETON (xdp_impl_screen_cast_skeleton_new ());

  g_signal_connect (impl, "handle-create-session",
                    G_CALLBACK (handle_create_session), NULL);
  g_signal_connect (impl, "handle-select-sources",
                    G_CALLBACK (handle_select_sources), NULL);
  g_signal_connect (impl, "handle-start",
                    G_CALLBACK (handle_start), NULL);

  gnome_api_version = gnome_screen_cast_get_api_version (gnome_screen_cast);

  available_source_types = SCREEN_CAST_SOURCE_TYPE_MONITOR;
  if (gnome_api_version >= 2)
    available_source_types |= SCREEN_CAST_SOURCE_TYPE_WINDOW;
  g_object_set (G_OBJECT (impl),
                "available-source-types", available_source_types,
                NULL);

  available_cursor_modes = SCREEN_CAST_CURSOR_MODE_NONE;
  if (gnome_api_version >= 2)
    available_cursor_modes |= (SCREEN_CAST_CURSOR_MODE_HIDDEN |
                               SCREEN_CAST_CURSOR_MODE_EMBEDDED |
                               SCREEN_CAST_CURSOR_MODE_METADATA);
  g_object_set (G_OBJECT (impl),
                "available-cursor-modes", available_cursor_modes,
                NULL);

  if (!g_dbus_interface_skeleton_export (impl,
                                         impl_connection,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         &error))
    {
      g_warning ("Failed to export screen cast portal implementation object: %s",
                 error->message);
      return;
    }

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (impl)->name);
}

static void
on_gnome_screen_cast_disabled (GDBusConnection *connection,
                               const char *name,
                               gpointer user_data)
{
  if (impl)
    {
      g_debug ("unproviding %s", g_dbus_interface_skeleton_get_info (impl)->name);

      g_dbus_interface_skeleton_unexport (impl);
      g_clear_object (&impl);
    }
}

gboolean
screen_cast_init (GDBusConnection *connection,
                  GError **error)
{
  impl_connection = connection;
  gnome_screen_cast = gnome_screen_cast_new (connection);

  g_signal_connect (gnome_screen_cast, "enabled",
                    G_CALLBACK (on_gnome_screen_cast_enabled), NULL);
  g_signal_connect (gnome_screen_cast, "disabled",
                    G_CALLBACK (on_gnome_screen_cast_disabled), NULL);

  return TRUE;
}

static void
screen_cast_session_close (Session *session)
{
  ScreenCastSession *screen_cast_session = (ScreenCastSession *)session;
  GnomeScreenCastSession *gnome_screen_cast_session;
  g_autoptr(GError) error = NULL;

  gnome_screen_cast_session = screen_cast_session->gnome_screen_cast_session;
  if (gnome_screen_cast_session)
    {
      g_signal_handler_disconnect (gnome_screen_cast_session,
                                   screen_cast_session->session_ready_handler_id);
      g_signal_handler_disconnect (gnome_screen_cast_session,
                                   screen_cast_session->session_closed_handler_id);
      if (!gnome_screen_cast_session_stop (gnome_screen_cast_session,
                                           &error))
        g_warning ("Failed to close GNOME screen cast session: %s",
                   error->message);
      g_clear_object (&screen_cast_session->gnome_screen_cast_session);
    }
}

static void
screen_cast_session_finalize (GObject *object)
{
  ScreenCastSession *screen_cast_session = (ScreenCastSession *)object;

  g_clear_object (&screen_cast_session->gnome_screen_cast_session);

  G_OBJECT_CLASS (screen_cast_session_parent_class)->finalize (object);
}

static void
screen_cast_session_init (ScreenCastSession *screen_cast_session)
{
}

static void
screen_cast_session_class_init (ScreenCastSessionClass *klass)
{
  GObjectClass *gobject_class;
  SessionClass *session_class;

  gobject_class = (GObjectClass *)klass;
  gobject_class->finalize = screen_cast_session_finalize;

  session_class = (SessionClass *)klass;
  session_class->close = screen_cast_session_close;
}
