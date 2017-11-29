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
#include "shell-dbus.h"

#include "screencast.h"
#include "screencastdialog.h"
#include "displaystatetracker.h"
#include "externalwindow.h"
#include "request.h"
#include "session.h"
#include "utils.h"

typedef struct _ScreenCastDialogHandle ScreenCastDialogHandle;

typedef enum _ScreenCastSourceType
{
  SCREEN_CAST_SOURCE_TYPE_ANY,
  SCREEN_CAST_SOURCE_TYPE_MONITOR,
  SCREEN_CAST_SOURCE_TYPE_WINDOW,
} ScreenCastSourceType;

typedef struct _ScreenCastSession
{
  Session parent;

  char *parent_window;
  char *mutter_session_path;
  OrgGnomeMutterScreenCastSession *mutter_session_proxy;
  gulong closed_handler_id;

  GList *streams;
  int n_needed_stream_node_ids;

  struct {
    gboolean multiple;
  } select;

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

typedef struct _ScreenCastStream
{
  ScreenCastSession *session;
  char *stream_path;
  uint32_t pipewire_node_id;
  OrgGnomeMutterScreenCastStream *stream_proxy;
} ScreenCastStream;

static GDBusConnection *impl_connection;
static guint screen_cast_name_watch;
static GDBusInterfaceSkeleton *impl;
static OrgGnomeMutterScreenCast *screen_cast;

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
                                               session->select.multiple));
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

static ScreenCastStream *
screen_cast_stream_new (ScreenCastSession *screen_cast_session,
                        const char *stream_path,
                        OrgGnomeMutterScreenCastStream *stream_proxy)
{
  ScreenCastStream *stream;

  stream = g_new0 (ScreenCastStream, 1);
  stream->session = screen_cast_session;
  stream->stream_path = g_strdup (stream_path);
  stream->stream_proxy = stream_proxy;

  return stream;
}

static void
screen_cast_stream_free (ScreenCastStream *stream)
{
  g_clear_object (&stream->stream_proxy);
  g_free (stream->stream_path);
  g_free (stream);
}

static void
on_mutter_session_closed (OrgGnomeMutterScreenCastSession *mutter_session_proxy,
                          ScreenCastSession *screen_cast_session)
{
  session_close ((Session *)screen_cast_session);
}

static ScreenCastSession *
screen_cast_session_new (const char *app_id,
                         const char *session_handle,
                         const char *mutter_session_path,
                         OrgGnomeMutterScreenCastSession *mutter_session_proxy)
{
  ScreenCastSession *screen_cast_session;

  screen_cast_session = g_object_new (screen_cast_session_get_type (),
                                      "id", session_handle,
                                      NULL);
  screen_cast_session->mutter_session_path =
    g_strdup (mutter_session_path);
  screen_cast_session->mutter_session_proxy =
    g_object_ref (mutter_session_proxy);

  screen_cast_session->closed_handler_id =
    g_signal_connect (screen_cast_session->mutter_session_proxy,
                      "closed", G_CALLBACK (on_mutter_session_closed),
                      screen_cast_session);

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
  const char *sender;
  g_autoptr(Request) request = NULL;
  g_autofree char *mutter_session_path = NULL;
  g_autoptr(GError) error = NULL;
  int response;
  GVariantBuilder properties_builder;
  GVariant *properties;
  OrgGnomeMutterScreenCastSession *mutter_session_proxy;
  Session *session;
  GVariantBuilder results_builder;

  sender = g_dbus_method_invocation_get_sender (invocation);

  request = request_new (sender, arg_app_id, arg_handle);

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE_VARDICT);
  properties = g_variant_builder_end (&properties_builder);
  if (!org_gnome_mutter_screen_cast_call_create_session_sync (screen_cast,
                                                              properties,
                                                              &mutter_session_path,
                                                              NULL,
                                                              &error))
    {
      g_warning ("Failed to create screen cast session: %s", error->message);
      response = 2;
      goto out;
    }

  mutter_session_proxy =
    org_gnome_mutter_screen_cast_session_proxy_new_sync (impl_connection,
                                                         G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                         "org.gnome.Mutter.ScreenCast",
                                                         mutter_session_path,
                                                         NULL,
                                                         &error);
  if (!mutter_session_proxy)
    {
      g_warning ("Failed to get screen cast session proxy: %s", error->message);
      response = 2;
      goto out;
    }

  session = (Session *)screen_cast_session_new (arg_app_id,
                                                arg_session_handle,
                                                mutter_session_path,
                                                mutter_session_proxy);

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
  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  g_variant_builder_init (&results_builder, G_VARIANT_TYPE_VARDICT);
  xdp_impl_screen_cast_complete_create_session (object,
                                                invocation,
                                                response,
                                                g_variant_builder_end (&results_builder));

  g_clear_object (&mutter_session_proxy);

  return TRUE;
}

static void
on_pipewire_stream_added (OrgGnomeMutterScreenCastStream *stream_proxy,
                          unsigned int arg_node_id,
                          ScreenCastStream *stream)
{
  stream->pipewire_node_id = arg_node_id;
  g_return_if_fail (stream->session->n_needed_stream_node_ids > 0);
  stream->session->n_needed_stream_node_ids--;
  if (stream->session->n_needed_stream_node_ids == 0)
    start_done (stream->session);
}

static gboolean
handle_select_sources (XdpImplScreenCast *object,
                       GDBusMethodInvocation *invocation,
                       const char *arg_handle,
                       const char *arg_session_handle,
                       const char *arg_app_id,
                       GVariant *arg_options)
{
  const char *sender;
  g_autoptr(Request) request = NULL;
  ScreenCastSession *screen_cast_session;
  int response;
  uint32_t types;
  gboolean multiple;
  GVariantBuilder results_builder;
  GVariant *results;

  sender = g_dbus_method_invocation_get_sender (invocation);
  request = request_new (sender, arg_app_id, arg_handle);

  screen_cast_session = (ScreenCastSession *)lookup_session (arg_session_handle);
  if (!screen_cast_session)
    {
      g_warning ("Tried to select sources on non-existing %s", arg_session_handle);
      response = 2;
      goto out;
    }

  if (!g_variant_lookup (arg_options, "multiple", "b", &multiple))
    multiple = FALSE;

  if (!g_variant_lookup (arg_options, "types", "u", &types))
    types = SCREEN_CAST_SOURCE_TYPE_MONITOR;

  if (!(types & SCREEN_CAST_SOURCE_TYPE_MONITOR))
    {
      g_warning ("Screen cast of a window not implemented");
      response = 2;
      goto out;
    }

  screen_cast_session->select.multiple = multiple;
  response = 0;

out:
  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  g_variant_builder_init (&results_builder, G_VARIANT_TYPE_VARDICT);
  results = g_variant_builder_end (&results_builder);
  xdp_impl_screen_cast_complete_select_sources (object, invocation,
                                                response, results);

  return TRUE;
}

static void
start_done (ScreenCastSession *screen_cast_session)
{
  GVariantBuilder streams_builder;
  GVariantBuilder results_builder;
  GList *l;

  g_variant_builder_init (&results_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_init (&streams_builder, G_VARIANT_TYPE ("a(ua{sv})"));

  for (l = screen_cast_session->streams; l; l = l->next)
    {
      ScreenCastStream *stream = l->data;
      GVariantBuilder stream_properties_builder;
      GVariant *parameters;
      int x, y;
      int width, height;

      g_variant_builder_init (&stream_properties_builder, G_VARIANT_TYPE_VARDICT);

      parameters =
        org_gnome_mutter_screen_cast_stream_get_parameters (stream->stream_proxy);
      if (parameters)
        {
          if (g_variant_lookup (parameters, "position", "(ii)", &x, &y))
            g_variant_builder_add (&stream_properties_builder, "{sv}",
                                   "position",
                                   g_variant_new ("(ii)", x, y));
          if (g_variant_lookup (parameters, "size", "(ii)", &width, &height))
            g_variant_builder_add (&stream_properties_builder, "{sv}",
                                   "size",
                                   g_variant_new ("(ii)", width, height));
        }
      else
        {
          g_warning ("Screen cast stream %s missing parameters",
                     stream->stream_path);
        }

      g_variant_builder_add (&streams_builder, "(ua{sv})",
                             stream->pipewire_node_id,
                             &stream_properties_builder);
    }

  g_variant_builder_add (&results_builder, "{sv}",
                         "streams",
                         g_variant_builder_end (&streams_builder));

  xdp_impl_screen_cast_complete_start (XDP_IMPL_SCREEN_CAST (impl),
                                       screen_cast_session->start_invocation, 0,
                                       g_variant_builder_end (&results_builder));
  screen_cast_session->start_invocation = NULL;
}

static gboolean
record_monitor (ScreenCastSession *screen_cast_session,
                const char *connector,
                GError **error)
{
  GVariantBuilder properties_builder;
  GVariant *properties;
  OrgGnomeMutterScreenCastSession *session_proxy;
  g_autofree char *stream_path = NULL;
  OrgGnomeMutterScreenCastStream *stream_proxy;
  ScreenCastStream *stream;

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE_VARDICT);
  properties = g_variant_builder_end (&properties_builder);
  session_proxy = screen_cast_session->mutter_session_proxy;
  if (!org_gnome_mutter_screen_cast_session_call_record_monitor_sync (session_proxy,
                                                                      connector,
                                                                      properties,
                                                                      &stream_path,
                                                                      NULL,
                                                                      error))
    return FALSE;

  stream_proxy =
    org_gnome_mutter_screen_cast_stream_proxy_new_sync (impl_connection,
                                                        G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                        "org.gnome.Mutter.ScreenCast",
                                                        stream_path,
                                                        NULL,
                                                        error);
  if (!stream_proxy)
    return FALSE;

  stream = screen_cast_stream_new (screen_cast_session,
                                   stream_path,
                                   stream_proxy);
  screen_cast_session->streams = g_list_append (screen_cast_session->streams,
                                                stream);
  screen_cast_session->n_needed_stream_node_ids++;

  g_signal_connect (stream_proxy, "pipewire-stream-added",
                    G_CALLBACK (on_pipewire_stream_added),
                    stream);

  return TRUE;
}

static gboolean
record_streams (ScreenCastSession *screen_cast_session,
                GVariant *selections,
                GError **error)
{
  GVariantIter selections_iter;
  GVariant *selection;

  g_variant_iter_init (&selections_iter, selections);
  while ((selection = g_variant_iter_next_value (&selections_iter)))
    {
      ScreenCastSelection selection_type;
      g_autofree char *key = NULL;

      g_variant_get (selection, "(us)",
                     &selection_type,
                     &key);

      switch (selection_type)
        {
        case SCREEN_CAST_SELECTION_MONITOR:
          if (!record_monitor (screen_cast_session, key, error))
            return FALSE;
          break;
        }
    }

  return TRUE;
}

static gboolean
start_session (ScreenCastSession *screen_cast_session,
               GVariant *selections,
               GError **error)
{
  OrgGnomeMutterScreenCastSession *session_proxy;

  if (!record_streams (screen_cast_session, selections, error))
    return FALSE;

  session_proxy = screen_cast_session->mutter_session_proxy;
  if (!org_gnome_mutter_screen_cast_session_call_start_sync (session_proxy,
                                                             NULL,
                                                             error))
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
  g_autoptr(GError) error = NULL;
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
screen_cast_name_appeared (GDBusConnection *connection,
                           const char *name,
                           const char *name_owner,
                           gpointer user_data)
{
  g_autoptr(GError) error = NULL;

  screen_cast = org_gnome_mutter_screen_cast_proxy_new_sync (impl_connection,
                                                             G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                             "org.gnome.Mutter.ScreenCast",
                                                             "/org/gnome/Mutter/ScreenCast",
                                                             NULL,
                                                             &error);
  if (!screen_cast)
    {
      g_warning ("Failed to acquire org.gnome.Mutter.ScreenCast proxy: %s",
                 error->message);
      return;
    }

  impl = G_DBUS_INTERFACE_SKELETON (xdp_impl_screen_cast_skeleton_new ());

  g_signal_connect (impl, "handle-create-session",
                    G_CALLBACK (handle_create_session), NULL);
  g_signal_connect (impl, "handle-select-sources",
                    G_CALLBACK (handle_select_sources), NULL);
  g_signal_connect (impl, "handle-start",
                    G_CALLBACK (handle_start), NULL);

  g_object_set (G_OBJECT (impl),
                "available-source-types", SCREEN_CAST_SOURCE_TYPE_MONITOR,
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
screen_cast_name_vanished (GDBusConnection *connection,
                           const char *name,
                           gpointer user_data)
{
  if (impl)
    {
      g_dbus_interface_skeleton_unexport (impl);
      g_clear_object (&impl);
    }

  g_clear_object (&screen_cast);
}

gboolean
screen_cast_init (GDBusConnection *connection,
                  GError **error)
{
  impl_connection = connection;
  screen_cast_name_watch = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                             "org.gnome.Mutter.ScreenCast",
                                             G_BUS_NAME_WATCHER_FLAGS_NONE,
                                             screen_cast_name_appeared,
                                             screen_cast_name_vanished,
                                             NULL,
                                             NULL);

  return TRUE;
}

static void
screen_cast_session_close (Session *session)
{
  ScreenCastSession *screen_cast_session = (ScreenCastSession *)session;
  OrgGnomeMutterScreenCastSession *session_proxy;
  g_autoptr(GError) error = NULL;

  session_proxy = screen_cast_session->mutter_session_proxy;
  g_signal_handler_disconnect (session_proxy,
                               screen_cast_session->closed_handler_id);

  if (!org_gnome_mutter_screen_cast_session_call_stop_sync (session_proxy,
                                                            NULL,
                                                            &error))
    {
      g_warning ("Failed to stop screen cast session: %s", error->message);
      return;
    }
}

static void
screen_cast_session_finalize (GObject *object)
{
  ScreenCastSession *screen_cast_session = (ScreenCastSession *)object;

  g_list_free_full (screen_cast_session->streams,
                    (GDestroyNotify) screen_cast_stream_free);
  g_free (screen_cast_session->mutter_session_path);

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
