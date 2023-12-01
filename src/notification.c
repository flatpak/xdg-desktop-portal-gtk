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

#include "notification.h"
#include "fdonotification.h"
#include "request.h"
#include "utils.h"

/* org.gtk.Notifications support. This is easy, since we can
 * just pass the calls through unseen, and gnome-shell does
 * the right thing.
 */
static OrgGtkNotifications *gtk_notifications;

static void
notification_added (GObject      *source,
                    GAsyncResult *result,
                    gpointer      data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = NULL;

  if (!org_gtk_notifications_call_add_notification_finish (gtk_notifications, result, &error))
    g_warning ("Error from gnome-shell: %s", error->message);
}

static void
handle_add_notification_gtk (XdpImplNotification *object,
                             GDBusMethodInvocation *invocation,
                             const char *arg_app_id,
                             const char *arg_id,
                             GVariant *arg_notification)
{
  if (gtk_notifications)
    org_gtk_notifications_call_add_notification (gtk_notifications,
                                                 arg_app_id,
                                                 arg_id,
                                                 arg_notification,
                                                 NULL,
                                                 notification_added,
                                                 NULL);

  g_debug ("handle add-notification from %s using the gtk implementation", arg_app_id);

  xdp_impl_notification_complete_add_notification (object, invocation);
}

static void
handle_remove_notification_gtk (XdpImplNotification *object,
                                GDBusMethodInvocation *invocation,
                                const char *arg_app_id,
                                const char *arg_id)
{
  if (gtk_notifications)
    org_gtk_notifications_call_remove_notification (gtk_notifications,
                                                    arg_app_id,
                                                    arg_id,
                                                    NULL,
                                                    NULL,
                                                    NULL);

  g_debug ("handle remove-notification from %s using the gtk implementation", arg_app_id);

  xdp_impl_notification_complete_remove_notification (object, invocation);
}

static char *
app_path_for_id (const gchar *app_id)
{
  char *path;
  gint i;

  path = g_strconcat ("/", app_id, NULL);
  for (i = 0; path[i]; i++)
    {
      if (path[i] == '.')
        path[i] = '/';
      if (path[i] == '-')
        path[i] = '_';
    }

  return path;
}

static void
activate_action (GDBusConnection *connection,
                 const char *app_id,
                 const char *id,
                 const char *name,
                 GVariant *parameter,
                 gpointer data)
{
  g_autofree char *object_path = NULL;
  GVariantBuilder pdata, parms;

  object_path = app_path_for_id (app_id);
  g_variant_builder_init (&pdata, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_init (&parms, G_VARIANT_TYPE ("av"));
  if (parameter)
    g_variant_builder_add (&parms, "v", parameter);

  if (name && g_str_has_prefix (name, "app."))
    {
      g_dbus_connection_call (connection,
                              app_id,
                              object_path,
                              "org.freedesktop.Application",
                              "ActivateAction",
                              g_variant_new ("(s@av@a{sv})",
                                             name + 4,
                                             g_variant_builder_end (&parms),
                                             g_variant_builder_end (&pdata)),
                              NULL,
                              G_DBUS_CALL_FLAGS_NONE,
                              -1, NULL, NULL, NULL);
    }
  else
    {
      g_autoptr(GVariant) ret = NULL;

      g_dbus_connection_call (connection,
                              app_id,
                              object_path,
                              "org.freedesktop.Application",
                              "Activate",
                              g_variant_new ("(@a{sv})",
                                             g_variant_builder_end (&pdata)),
                              NULL,
                              G_DBUS_CALL_FLAGS_NONE,
                              -1, NULL, NULL, NULL);

      g_dbus_connection_emit_signal (connection,
                                     NULL,
                                     "/org/freedesktop/portal/desktop",
                                     "org.freedesktop.impl.portal.Notification",
                                     "ActionInvoked",
                                     g_variant_new ("(sss@av)",
                                                    app_id, id, name,
                                                    g_variant_builder_end (&parms)),
                                     NULL);
    }
}

static void
handle_add_notification_fdo (XdpImplNotification *object,
                             GDBusMethodInvocation *invocation,
                             const gchar *arg_app_id,
                             const gchar *arg_id,
                             GVariant *arg_notification)
{
  GDBusConnection *connection;

  g_debug ("handle add-notification from %s using the freedesktop implementation", arg_app_id);

  connection = g_dbus_method_invocation_get_connection (invocation);

  fdo_add_notification (connection, arg_app_id, arg_id, arg_notification, activate_action, NULL);

  xdp_impl_notification_complete_add_notification (object, invocation);
}

static gboolean
handle_remove_notification_fdo (XdpImplNotification *object,
                                GDBusMethodInvocation *invocation,
                                const gchar *arg_app_id,
                                const gchar *arg_id)
{
  GDBusConnection *connection;

  connection = g_dbus_method_invocation_get_connection (invocation);
  if (fdo_remove_notification (connection, arg_app_id, arg_id))
    {
      g_debug ("handle remove-notification from %s using the freedesktop implementation", arg_app_id);
      xdp_impl_notification_complete_remove_notification (object, invocation);
      return TRUE;
    }
  return FALSE;
}

static gboolean
has_unprefixed_action (GVariant *notification)
{
  const char *action;
  g_autoptr(GVariant) buttons = NULL;
  int i;

  if (g_variant_lookup (notification, "default-action", "&s", &action) &&
      !g_str_has_prefix (action, "app."))
    return TRUE;

  buttons = g_variant_lookup_value (notification, "buttons", G_VARIANT_TYPE("aa{sv}"));
  if (buttons)
    for (i = 0; i < g_variant_n_children (buttons); i++)
      {
        g_autoptr(GVariant) button = NULL;

        button = g_variant_get_child_value (buttons, i);
        if (g_variant_lookup (button, "action", "&s", &action) &&
            !g_str_has_prefix (action, "app."))
          return TRUE;
      }

  return FALSE;
}

static gboolean
handle_add_notification (XdpImplNotification *object,
                         GDBusMethodInvocation *invocation,
                         const gchar *arg_app_id,
                         const gchar *arg_id,
                         GVariant *arg_notification)
{
  g_autofree char* owner = gtk_notifications != NULL ?
    g_dbus_proxy_get_name_owner (G_DBUS_PROXY (gtk_notifications)) :
    NULL;
  if (owner == NULL ||
      !g_application_id_is_valid (arg_app_id) ||
      has_unprefixed_action (arg_notification))
    handle_add_notification_fdo (object, invocation, arg_app_id, arg_id, arg_notification);
  else
    handle_add_notification_gtk (object, invocation, arg_app_id, arg_id, arg_notification);

  return TRUE;
}

static gboolean
handle_remove_notification (XdpImplNotification *object,
                            GDBusMethodInvocation *invocation,
                            const gchar *arg_app_id,
                            const gchar *arg_id)
{
  if (!handle_remove_notification_fdo (object, invocation, arg_app_id, arg_id))
    handle_remove_notification_gtk (object, invocation, arg_app_id, arg_id);
  return TRUE;
}

gboolean
notification_init (GDBusConnection *bus,
                   GError **error)
{
  GDBusInterfaceSkeleton *helper;

  gtk_notifications = org_gtk_notifications_proxy_new_sync (bus,
                                                            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                            "org.gtk.Notifications",
                                                            "/org/gtk/Notifications",
                                                            NULL,
                                                            NULL);

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_notification_skeleton_new ());

  g_signal_connect (helper, "handle-add-notification", G_CALLBACK (handle_add_notification), NULL);
  g_signal_connect (helper, "handle-remove-notification", G_CALLBACK (handle_remove_notification), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
