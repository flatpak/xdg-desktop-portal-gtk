#define _GNU_SOURCE 1

#include "config.h"

#include <errno.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gtk/gtk.h>

#include <glib/gi18n.h>

#include "xdg-desktop-portal-dbus.h"
#include "shell-dbus.h"

#include "background.h"
#include "fdonotification.h"
#include "request.h"
#include "utils.h"

static OrgGnomeShellIntrospect *shell;

typedef enum { BACKGROUND, RUNNING, ACTIVE } AppState;

static gboolean
handle_get_app_state (XdpImplBackground *object,
                      GDBusMethodInvocation *invocation)
{
  g_autoptr(GVariant) windows = NULL;
  g_autoptr(GHashTable) app_states = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_autoptr(GError) error = NULL;
  GVariantBuilder builder;
  GHashTableIter iter;
  const char *key;
  gpointer value;

  g_debug ("background: handle GetAppState");

  if (!org_gnome_shell_introspect_call_get_windows_sync (shell, &windows, NULL, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             XDG_DESKTOP_PORTAL_ERROR,
                                             XDG_DESKTOP_PORTAL_ERROR_FAILED,
                                             "Could not get window list: %s", error->message);
      return TRUE;
    }

  if (windows)
    {
      g_autoptr(GVariantIter) iter = g_variant_iter_new (windows);
      GVariant *dict;

      while (g_variant_iter_loop (iter, "{t@a{sv}}", NULL, &dict))
        {
          const char *app_id = NULL;
          char *app;
          gboolean hidden = FALSE;
          gboolean focus = FALSE;
          AppState state = BACKGROUND;

          g_variant_lookup (dict, "app-id", "&s", &app_id);
          g_variant_lookup (dict, "is-hidden", "b", &hidden);
          g_variant_lookup (dict, "has-focus", "b", &focus);

          if (app_id == NULL)
            continue;

          app = g_strdup (app_id);
          if (g_str_has_suffix (app, ".desktop"))
            app[strlen (app) - strlen (".desktop")] = '\0';

          state = GPOINTER_TO_INT (g_hash_table_lookup (app_states, app));
          if (!hidden)
            state = MAX (state, RUNNING);
          if (focus)
            state = MAX (state, ACTIVE);

          g_hash_table_insert (app_states, app, GINT_TO_POINTER (state));
        }
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_hash_table_iter_init (&iter, app_states);
  while (g_hash_table_iter_next (&iter, (gpointer *)&key, (gpointer *)&value))
    {
      g_variant_builder_add (&builder, "{sv}", key, g_variant_new_uint32 (GPOINTER_TO_UINT (value)));
    }

  xdp_impl_background_complete_get_app_state (object,
                                              invocation,
                                              g_variant_builder_end (&builder));

  return TRUE;
}

typedef struct {
  XdpImplBackground *impl;
  GDBusMethodInvocation *invocation;
  Request *request;
  char *id;
  gboolean allow;
} BackgroundHandle;

static void
background_handle_free (gpointer data)
{
  BackgroundHandle *handle = data;

  g_object_unref (handle->request);
  g_free (handle->id);

  g_free (handle);
}

static void
background_handle_close (BackgroundHandle *handle)
{
  GDBusConnection *connection;

  connection = g_dbus_interface_skeleton_get_connection (G_DBUS_INTERFACE_SKELETON (handle->impl));

  fdo_remove_notification (connection, handle->request->app_id, handle->id);
  background_handle_free (handle);
}

static void
send_response (BackgroundHandle *handle)
{
  GVariantBuilder opt_builder;
  int response = 0;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&opt_builder, "{sv}", "allow", g_variant_new_boolean (handle->allow));

  if (handle->request->exported)
    request_unexport (handle->request);

  xdp_impl_background_complete_notify_background (handle->impl,
                                                  handle->invocation,
                                                  response,
                                                  g_variant_builder_end (&opt_builder));

  background_handle_close (handle);
}

static void
activate_action (GDBusConnection *connection,
                 const char *app_id,
                 const char *id,
                 const char *name,
                 GVariant *parameter,
                 gpointer data)
{
  BackgroundHandle *handle = data;

  if (g_str_equal (name, "allow"))
    {
      g_debug ("Allowing app %s to run in background", handle->request->app_id);
      handle->allow = TRUE;
    }
  else if (g_str_equal (name, "forbid"))
    {
      g_debug ("Forbid app %s to run in background", handle->request->app_id);
      handle->allow = FALSE;
    }
  else
    {
      g_debug ("Unexpected action for app %s", handle->request->app_id);
      handle->allow = FALSE;
    }

  send_response (handle);
}

static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              BackgroundHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);

  xdp_impl_background_complete_notify_background (handle->impl,
                                                  handle->invocation,
                                                  2,
                                                  g_variant_builder_end (&opt_builder));

  if (handle->request->exported)
    request_unexport (handle->request);

  background_handle_close (handle);

  xdp_impl_request_complete_close (object, invocation);

  return TRUE;
}

static int count;

static gboolean
handle_notify_background (XdpImplBackground *object,
                          GDBusMethodInvocation *invocation,
                          const char *arg_handle,
                          const char *arg_app_id,
                          const char *arg_name)
{
  g_autofree char *body = NULL;
  GVariantBuilder builder;
  GVariantBuilder bbuilder;
  GVariantBuilder button;
  GDBusConnection *connection;
  const char *sender;
  g_autoptr (Request) request = NULL;
  BackgroundHandle *handle;

  g_debug ("background: handle NotifyBackground");

  sender = g_dbus_method_invocation_get_sender (invocation);
  request = request_new (sender, arg_app_id, arg_handle);

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "title", g_variant_new_string (_("Background activity")));

  body = g_strdup_printf (_("%s is running in the background."), arg_name);
  g_variant_builder_add (&builder, "{sv}", "body", g_variant_new_string (body));

  g_variant_builder_init (&bbuilder, G_VARIANT_TYPE ("aa{sv}"));
  g_variant_builder_init (&button, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&button, "{sv}", "label", g_variant_new_string (_("Allow")));
  g_variant_builder_add (&button, "{sv}", "action", g_variant_new_string ("allow"));
  g_variant_builder_add (&button, "{sv}", "target", g_variant_new_string (arg_app_id));

  g_variant_builder_add (&bbuilder, "@a{sv}", g_variant_builder_end (&button));

  g_variant_builder_init (&button, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&button, "{sv}", "label", g_variant_new_string (_("Forbid")));
  g_variant_builder_add (&button, "{sv}", "action", g_variant_new_string ("forbid"));
  g_variant_builder_add (&button, "{sv}", "target", g_variant_new_string (arg_app_id));

  g_variant_builder_add (&bbuilder, "@a{sv}", g_variant_builder_end (&button));

  g_variant_builder_add (&builder, "{sv}", "buttons", g_variant_builder_end (&bbuilder));

  handle = g_new0 (BackgroundHandle, 1);
  handle->impl = object;
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->id = g_strdup_printf ("notify_background_%d", count++);

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

  connection = g_dbus_method_invocation_get_connection (invocation);

  fdo_add_notification (connection, "", handle->id, g_variant_builder_end (&builder), activate_action, handle);

  request_export (request, connection);

  return TRUE;
}

gboolean
background_init (GDBusConnection *bus,
                 GError **error)
{
  GDBusInterfaceSkeleton *helper;

  shell = org_gnome_shell_introspect_proxy_new_sync (bus,
                                                     G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                     "org.gnome.Shell",
                                                     "/org/gnome/Shell/Introspect",
                                                     NULL,
                                                     NULL);

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_background_skeleton_new ());

  g_signal_connect (helper, "handle-get-app-state", G_CALLBACK (handle_get_app_state), NULL);
  g_signal_connect (helper, "handle-notify-background", G_CALLBACK (handle_notify_background), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
