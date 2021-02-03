#define _GNU_SOURCE 1

#include "config.h"

#include <gtk/gtk.h>

#include <gio/gio.h>

#include "xdg-desktop-portal-dbus.h"
#include "shell-dbus.h"
#include "session.h"
#include "utils.h"

#include "inhibit.h"
#include "request.h"

enum {
  INHIBIT_LOGOUT  = 1,
  INHIBIT_SWITCH  = 2,
  INHIBIT_SUSPEND = 4,
  INHIBIT_IDLE    = 8
};

static GDBusInterfaceSkeleton *inhibit;
static OrgGnomeSessionManager *sessionmanager;
static OrgGnomeScreenSaver *screensaver;
static GDBusProxy *client;

typedef enum {
  UNKNOWN   = 0,
  RUNNING   = 1,
  QUERY_END = 2,
  ENDING    = 3
} SessionState;

static SessionState session_state = RUNNING;
static gboolean screensaver_active = FALSE;
static guint query_end_timeout;

static void
uninhibit_done_gnome (GObject *source,
                      GAsyncResult *result,
                      gpointer data)
{
  g_autoptr(GError) error = NULL;

  if (!org_gnome_session_manager_call_uninhibit_finish (sessionmanager, result, &error))
    g_warning ("Backend call failed: %s", error->message);
}

static gboolean
handle_close_gnome (XdpImplRequest *object,
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
    org_gnome_session_manager_call_uninhibit (sessionmanager, cookie, NULL, uninhibit_done_gnome, NULL);
  else
    g_object_set_data (G_OBJECT (request), "closed", GINT_TO_POINTER (1));

  if (request->exported)
    request_unexport (request);

  xdp_impl_request_complete_close (object, invocation);

  return TRUE;
}

static void
inhibit_done_gnome (GObject *source,
                    GAsyncResult *result,
                    gpointer data)
{
  g_autoptr(Request) request = data;
  guint cookie = 0;
  gboolean closed;
  g_autoptr(GError) error = NULL;

  if (!org_gnome_session_manager_call_inhibit_finish (sessionmanager, &cookie, result, &error))
    g_warning ("Backend call failed: %s", error->message);

  closed = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (request), "closed"));

  if (closed)
    org_gnome_session_manager_call_uninhibit (sessionmanager, cookie, NULL, uninhibit_done_gnome, NULL);
  else
    g_object_set_data (G_OBJECT (request), "cookie", GUINT_TO_POINTER (cookie));
}

static gboolean
handle_inhibit_gnome (XdpImplInhibit *object,
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

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close_gnome), NULL);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  if (!g_variant_lookup (arg_options, "reason", "&s", &reason))
    reason = "";

  org_gnome_session_manager_call_inhibit (sessionmanager,
                                          arg_app_id,
                                          0, /* window */
                                          reason,
                                          arg_flags,
                                          NULL,
                                          inhibit_done_gnome,
                                          g_object_ref (request));

  xdp_impl_inhibit_complete_inhibit (object, invocation);

  return TRUE;
}

static void
send_quit_response (GDBusProxy  *client,
                    gboolean     will_quit,
                    const gchar *reason)
{
  g_debug ("Calling EndSessionResponse %d '%s'", will_quit, reason ? reason : "");

  g_dbus_proxy_call (client,
                     "EndSessionResponse",
                     g_variant_new ("(bs)", will_quit, reason ? reason : ""),
                     G_DBUS_CALL_FLAGS_NONE,
                     G_MAXINT,
                     NULL, NULL, NULL);
}

static void global_emit_state_changed (void);

static void
set_session_state (SessionState state)
{
  const char *names[] = {
    "Unknown", "Running", "Query-end", "Ending"
  };

  g_debug ("Session state now: %s", names[state]);

  session_state = state;

  global_emit_state_changed ();
}

static void global_set_pending_query_end_response (gboolean pending);
static gboolean global_get_pending_query_end_response (void);

static void
stop_waiting_for_query_end_response (gboolean send_response)
{
  g_debug ("Stop waiting for QueryEndResponse calls");

  if (query_end_timeout != 0)
    {
      g_source_remove (query_end_timeout);
      query_end_timeout = 0;
    }

  global_set_pending_query_end_response (FALSE);

  if (send_response && client)
    send_quit_response (client, TRUE, NULL);
}

static gboolean
query_end_response (gpointer data)
{
  g_debug ("1 second wait is over");

  stop_waiting_for_query_end_response (TRUE);

  return G_SOURCE_REMOVE;
}

static void
wait_for_query_end_response (GDBusProxy *proxy)
{
  if (query_end_timeout != 0)
    return; /* we're already waiting */

  g_debug ("Waiting for up to 1 second for QueryEndResponse calls");

  query_end_timeout = g_timeout_add (1000, query_end_response, proxy);
  
  global_set_pending_query_end_response (TRUE);
}

static void
maybe_send_quit_response (void)
{
  if (query_end_timeout == 0)
    return;

  if (!client)
    return;

  if (global_get_pending_query_end_response ())
    return;

  g_debug ("No more pending QueryEndResponse calls");

  stop_waiting_for_query_end_response (TRUE);
}

static void
client_proxy_signal (GDBusProxy  *proxy,
                     const gchar *sender_name,
                     const gchar *signal_name,
                     GVariant    *parameters,
                     gpointer     user_data)
{
  if (g_str_equal (signal_name, "QueryEndSession"))
    {
      g_debug ("Received QueryEndSession");
      wait_for_query_end_response (proxy); 
      set_session_state (QUERY_END);
      maybe_send_quit_response ();
    }
  else if (g_str_equal (signal_name, "CancelEndSession"))
    {
      g_debug ("Received CancelEndSession");
      stop_waiting_for_query_end_response (FALSE);
      set_session_state (RUNNING);
    }
  else if (g_str_equal (signal_name, "EndSession"))
    {
      g_debug ("Received EndSession");
      stop_waiting_for_query_end_response (FALSE);
      set_session_state (ENDING);
      send_quit_response (client, TRUE, NULL);
    }
  else if (g_str_equal (signal_name, "Stop"))
    {
      g_debug ("Received Stop");
    }
}

static OrgFreedesktopScreenSaver *fdo_screensaver;

static void
uninhibit_done_fdo (GObject *source,
                    GAsyncResult *result,
                    gpointer data)
{
  g_autoptr(GError) error = NULL;

  if (!org_freedesktop_screen_saver_call_un_inhibit_finish (fdo_screensaver,
                                                            result,
                                                            &error))
    g_warning ("Backend call failed: %s", error->message);
}

static gboolean
handle_close_fdo (XdpImplRequest *object,
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
    org_freedesktop_screen_saver_call_un_inhibit (fdo_screensaver,
                                                  cookie,
                                                  NULL,
                                                  uninhibit_done_fdo,
                                                  NULL);
  else
    g_object_set_data (G_OBJECT (request), "closed", GINT_TO_POINTER (1));

  if (request->exported)
    request_unexport (request);

  xdp_impl_request_complete_close (object, invocation);

  return TRUE;
}

static void
inhibit_done_fdo (GObject *source,
                  GAsyncResult *result,
                  gpointer data)
{
  g_autoptr(Request) request = data;
  guint cookie = 0;
  gboolean closed;
  g_autoptr(GError) error = NULL;

  if (!org_freedesktop_screen_saver_call_inhibit_finish (fdo_screensaver,
                                                         &cookie,
                                                         result,
                                                         &error))
    g_warning ("Backend call failed: %s", error->message);

  closed = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (request), "closed"));

  if (closed)
    org_freedesktop_screen_saver_call_un_inhibit (fdo_screensaver,
                                                  cookie,
                                                  NULL,
                                                  uninhibit_done_fdo,
                                                  NULL);
  else
    g_object_set_data (G_OBJECT (request), "cookie", GUINT_TO_POINTER (cookie));
}

static gboolean
handle_inhibit_fdo (XdpImplInhibit *object,
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

  if ((arg_flags & ~INHIBIT_IDLE) != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             XDG_DESKTOP_PORTAL_ERROR,
                                             XDG_DESKTOP_PORTAL_ERROR_FAILED,
                                             "Inhibiting other than idle not supported");
      return TRUE;
    }

  sender = g_dbus_method_invocation_get_sender (invocation);

  request = request_new (sender, arg_app_id, arg_handle);

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close_fdo), NULL);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  if (!g_variant_lookup (arg_options, "reason", "&s", &reason))
    reason = "";

  org_freedesktop_screen_saver_call_inhibit (fdo_screensaver,
                                             arg_app_id,
                                             reason,
                                             NULL,
                                             inhibit_done_fdo,
                                             g_object_ref (request));

  xdp_impl_inhibit_complete_inhibit (object, invocation);

  return TRUE;
}

static GList *active_sessions = NULL;

static void
emit_state_changed (Session *session)
{
  GVariantBuilder state;

  g_debug ("Emitting StateChanged for session %s", session->id);

  g_variant_builder_init (&state, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&state, "{sv}", "screensaver-active", g_variant_new_boolean (screensaver_active));
  g_variant_builder_add (&state, "{sv}", "session-state", g_variant_new_uint32 (session_state));
  g_signal_emit_by_name (inhibit, "state-changed", session->id, g_variant_builder_end (&state)); 
}

static void
global_emit_state_changed (void)
{
  GList *l;

  for (l = active_sessions; l; l = l->next)
    emit_state_changed ((Session *)l->data);
}

typedef struct
{
  Session parent;
  gboolean pending_query_end_response;
} InhibitSession;

typedef struct _InhibitSessionClass
{
  SessionClass parent_class;
} InhibitSessionClass;

GType inhibit_session_get_type (void);
G_DEFINE_TYPE (InhibitSession, inhibit_session, session_get_type ())

static void
global_set_pending_query_end_response (gboolean pending)
{
  GList *l;

  for (l = active_sessions; l; l = l->next)
    {
      InhibitSession *session = (InhibitSession *)l->data;
      session->pending_query_end_response = pending;
    }
}

static gboolean
global_get_pending_query_end_response (void)
{
  GList *l;

  for (l = active_sessions; l; l = l->next)
    {
      InhibitSession *session = (InhibitSession *)l->data;
      if (session->pending_query_end_response)
        return TRUE;
    }

  return FALSE;
}

static void
inhibit_session_close (Session *session)
{
  InhibitSession *inhibit_session = (InhibitSession *)session;

  g_debug ("Closing inhibit session %s", ((Session *)inhibit_session)->id);

  active_sessions = g_list_remove (active_sessions, session);
}

static void
inhibit_session_finalize (GObject *object)
{
  G_OBJECT_CLASS (inhibit_session_parent_class)->finalize (object);
}

static void
inhibit_session_init (InhibitSession *inhibit_session)
{
}

static void
inhibit_session_class_init (InhibitSessionClass *klass)
{
  GObjectClass *gobject_class;
  SessionClass *session_class;

  gobject_class = (GObjectClass *)klass;
  gobject_class->finalize = inhibit_session_finalize;

  session_class = (SessionClass *)klass;
  session_class->close = inhibit_session_close;
}

static InhibitSession *
inhibit_session_new (const char *app_id,
                     const char *session_handle)
{
  InhibitSession *inhibit_session;

  g_debug ("Creating inhibit session %s", session_handle);

  inhibit_session = g_object_new (inhibit_session_get_type (),
                                  "id", session_handle,
                                  NULL);

  active_sessions = g_list_prepend (active_sessions, inhibit_session);

  return inhibit_session;
}

static gboolean
handle_create_monitor (XdpImplInhibit *object,
                       GDBusMethodInvocation *invocation,
                       const char *arg_handle,
                       const char *arg_session_handle,
                       const char *arg_app_id,
                       const char *arg_window)
{
  g_autoptr(GError) error = NULL;
  int response;
  Session *session;

  session = (Session *)inhibit_session_new (arg_app_id, arg_session_handle);

  if (!session_export (session, g_dbus_method_invocation_get_connection (invocation), &error))
    {
      g_clear_object (&session);
      g_warning ("Failed to create inhibit session: %s", error->message);
      response = 2;
      goto out;
    }

  response = 0;

out:
  xdp_impl_inhibit_complete_create_monitor (object, invocation, response);
  if (session)
    emit_state_changed (session);

  return TRUE;
}

static gboolean
handle_query_end_response (XdpImplInhibit *object,
                           GDBusMethodInvocation *invocation,
                           const char *arg_session_handle)
{
  InhibitSession *session = (InhibitSession *)lookup_session (arg_session_handle);

  g_debug ("Handle QueryEndSessionResponse for session %s", arg_session_handle);

  if (session)
    {
      session->pending_query_end_response = FALSE;
      maybe_send_quit_response ();
    }
 
  xdp_impl_inhibit_complete_query_end_response (object, invocation);

  return TRUE;
}

static void
global_active_changed_cb (GObject *object,
                          gboolean active)
{
  g_debug ("Screensaver %s", active  ? "active" : "inactive");

  screensaver_active = active;

  global_emit_state_changed ();
}

gboolean
inhibit_init (GDBusConnection *bus,
              GError **error)
{
  g_autofree char *owner = NULL;
  g_autofree char *owner2 = NULL;
  gboolean active;

  inhibit = G_DBUS_INTERFACE_SKELETON (xdp_impl_inhibit_skeleton_new ());

  sessionmanager = org_gnome_session_manager_proxy_new_sync (bus,
                                                             G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                             "org.gnome.SessionManager",
                                                             "/org/gnome/SessionManager",
                                                             NULL,
                                                             NULL);
  owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (sessionmanager));

  if (owner)
    {
      screensaver = org_gnome_screen_saver_proxy_new_sync (bus,
                                                           G_DBUS_PROXY_FLAGS_NONE,
                                                           "org.gnome.ScreenSaver",
                                                           "/org/gnome/ScreenSaver",
                                                           NULL,
                                                           NULL);
      owner2 = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (screensaver));

      if (owner2)
        {
          g_autofree char *client_path = NULL;
          g_autofree char *owner3 = NULL;

          g_signal_connect (inhibit, "handle-inhibit", G_CALLBACK (handle_inhibit_gnome), NULL);
          g_signal_connect (inhibit, "handle-create-monitor", G_CALLBACK (handle_create_monitor), NULL);
          g_signal_connect (inhibit, "handle-query-end-response", G_CALLBACK (handle_query_end_response), NULL);

          g_signal_connect (screensaver, "active-changed", G_CALLBACK (global_active_changed_cb), NULL);
          org_gnome_screen_saver_call_get_active_sync (screensaver, &active, NULL, NULL);
          g_object_set_data (G_OBJECT (screensaver), "active", GINT_TO_POINTER (active));

          g_debug ("Using org.gnome.SessionManager for inhibit");
          g_debug ("Using org.gnome.Screensaver for screensaver state");

          if (org_gnome_session_manager_call_register_client_sync (sessionmanager,
                                                                   "org.freedesktop.portal",
                                                                   "",
                                                                   &client_path,
                                                                   NULL,
                                                                   NULL))
            {
              client = g_dbus_proxy_new_sync (bus, 0,
                                              NULL,
                                              "org.gnome.SessionManager",
                                              client_path,
                                              "org.gnome.SessionManager.ClientPrivate",
                                              NULL,
                                              NULL);

              owner3 = g_dbus_proxy_get_name_owner (client);
              if (owner3)
                {
                  g_signal_connect (client, "g-signal", G_CALLBACK (client_proxy_signal), NULL);

                  g_debug ("Using org.gnome.SessionManager for session state");
                }
            }
        }
      else
        {
          g_clear_object (&sessionmanager);
          g_clear_object (&screensaver);
        }
    }
  else
    {
      g_clear_object (&sessionmanager);
    }

  if (!screensaver)
    {
      g_signal_connect (inhibit, "handle-inhibit", G_CALLBACK (handle_inhibit_fdo), NULL);
      g_signal_connect (inhibit, "handle-create-monitor", G_CALLBACK (handle_create_monitor), NULL);

      fdo_screensaver = org_freedesktop_screen_saver_proxy_new_sync (bus,
                                                                  G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                                  "org.freedesktop.ScreenSaver",
                                                                  "/org/freedesktop/ScreenSaver",
                                                                  NULL,
                                                                  NULL);

      g_signal_connect (fdo_screensaver, "active-changed", G_CALLBACK (global_active_changed_cb), NULL);
      org_freedesktop_screen_saver_call_get_active_sync (fdo_screensaver, &active, NULL, NULL);
      g_object_set_data (G_OBJECT (fdo_screensaver), "active", GINT_TO_POINTER (active));

      g_debug ("Using org.freedesktop.ScreenSaver for inhibit");
      g_debug ("Using org.freedesktop.ScreenSaver for screensaver state");
    }

  if (!g_dbus_interface_skeleton_export (inhibit, bus, "/org/freedesktop/portal/desktop", error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (inhibit)->name);

  return TRUE;
}
