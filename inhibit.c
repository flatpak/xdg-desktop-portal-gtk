#define _GNU_SOURCE 1

#include "config.h"

#include <gtk/gtk.h>

#include <gio/gio.h>

#include "xdg-desktop-portal-dbus.h"
#include "shell-dbus.h"

#include "inhibit.h"
#include "request.h"

static OrgGnomeSessionManager *session;

static void
uninhibit_done (GObject *source,
                GAsyncResult *result,
                gpointer data)
{
  g_autoptr(GError) error = NULL;

  if (!org_gnome_session_manager_call_uninhibit_finish (ORG_GNOME_SESSION_MANAGER (source),
                                                        result,
                                                        &error))
    {
      g_warning ("Backend call failed: %s", error->message);
    }
}

static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              gpointer data)
{
  Request *request = (Request *)object;
  guint cookie;

  /* If we get a Close call before the Inhibit call returned,
   * delay the uninhibit call until we have the cookie.
   */
  cookie = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (request), "cookie"));
  if (cookie)
    org_gnome_session_manager_call_uninhibit (session,
                                              cookie,
                                              NULL,
                                              uninhibit_done,
                                              NULL);
  else
    g_object_set_data (G_OBJECT (request), "closed", GINT_TO_POINTER (1));

  if (request->exported)
    request_unexport (request);

  xdp_impl_request_complete_close (object, invocation);

  return TRUE;
}

static void
inhibit_done (GObject *source,
              GAsyncResult *result,
              gpointer data)
{
  g_autoptr(Request) request = data;
  guint cookie = 0;
  gboolean closed;
  g_autoptr(GError) error = NULL;

  if (!org_gnome_session_manager_call_inhibit_finish (ORG_GNOME_SESSION_MANAGER (source),
                                                      &cookie,
                                                      result,
                                                      &error))
    {
      g_warning ("Backend call failed: %s", error->message);
    }

  closed = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (request), "closed"));

  if (closed)
    org_gnome_session_manager_call_uninhibit (session,
                                              cookie,
                                              NULL,
                                              uninhibit_done,
                                              NULL);
  else
    g_object_set_data (G_OBJECT (request), "cookie", GUINT_TO_POINTER (cookie));
}

static gboolean
handle_inhibit (XdpImplInhibit *object,
                GDBusMethodInvocation *invocation,
                const gchar *arg_handle,
                const gchar *arg_app_id,
                const gchar *arg_window,
                guint arg_flags,
                GVariant *arg_options)
{
  g_autoptr (Request) request = NULL;
  const char *sender;
  g_autoptr(GError) error = NULL;
  const char *reason;

  sender = g_dbus_method_invocation_get_sender (invocation);

  request = request_new (sender, arg_app_id, arg_handle);

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), NULL);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  if (!g_variant_lookup (arg_options, "reason", "&s", &reason))
    reason = "";

  org_gnome_session_manager_call_inhibit (session,
                                          arg_app_id,
                                          0, /* window */
                                          reason,
                                          arg_flags,
                                          NULL,
                                          inhibit_done,
                                          g_object_ref (request));

  xdp_impl_inhibit_complete_inhibit (object, invocation);

  return TRUE;
}

gboolean
inhibit_init (GDBusConnection *bus,
              GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_inhibit_skeleton_new ());

  session = org_gnome_session_manager_proxy_new_sync (bus,
                                                      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                      "org.gnome.SessionManager",
                                                      "/org/gnome/SessionManager",
                                                      NULL,
                                                      NULL);

  g_signal_connect (helper, "handle-inhibit", G_CALLBACK (handle_inhibit), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         "/org/freedesktop/portal/desktop",
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
