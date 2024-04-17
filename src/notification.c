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
                 const char *activation_token,
                 gpointer data)
{
  g_autofree char *object_path = NULL;
  GVariantBuilder pdata, parms;

  object_path = app_path_for_id (app_id);
  g_variant_builder_init (&pdata, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_init (&parms, G_VARIANT_TYPE ("av"));
  if (parameter)
    g_variant_builder_add (&parms, "v", parameter);

  if (activation_token)
    {
      /* Used by  `GTK` < 4.10 */
      g_variant_builder_add (&pdata, "{sv}",
                             "desktop-startup-id", g_variant_new_string (activation_token));
      /* Used by `GTK` and `QT` */
      g_variant_builder_add (&pdata, "{sv}",
                             "activation-token", g_variant_new_string (activation_token));
    }

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

      /* The application may not implement the `org.freedesktop.Application`, so
       * also add the platform data containing the activation-token
       * to the `ActionInvoked` signal */
      if (activation_token)
        {
          g_variant_builder_init (&pdata, G_VARIANT_TYPE_VARDICT);
          g_variant_builder_add (&pdata, "{sv}",
                                 "activation-token", g_variant_new_string (activation_token));
          g_variant_builder_add (&parms, "v", g_variant_builder_end (&pdata));
        }

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
handle_add_notification (XdpImplNotification *object,
                         GDBusMethodInvocation *invocation,
                         const gchar *arg_app_id,
                         const gchar *arg_id,
                         GVariant *arg_notification)
{
  const char *desktop_file_id = NULL;
  g_autofree char *no_dot_desktop = NULL;
  g_autofree char* owner = gtk_notifications != NULL ?
    g_dbus_proxy_get_name_owner (G_DBUS_PROXY (gtk_notifications)) :
    NULL;

  if (g_variant_lookup (arg_notification, "desktop-file-id", "&s", &desktop_file_id))
    no_dot_desktop = g_strndup (desktop_file_id, strlen(desktop_file_id) - strlen (".desktop"));

  if (owner == NULL)
    handle_add_notification_fdo (object,
                                 invocation,
                                 no_dot_desktop ? no_dot_desktop : arg_app_id,
                                 arg_id,
                                 arg_notification);
  else
    handle_add_notification_gtk (object,
                                 invocation,
                                 no_dot_desktop ? no_dot_desktop : arg_app_id,
                                 arg_id,
                                 arg_notification);

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

static void
handle_gtk_action_invoked (OrgGtkNotifications *object,
                           const gchar         *arg_app_id,
                           const gchar         *arg_notification_id,
                           const gchar         *arg_name,
                           GVariant            *arg_parameter)
{
  GDBusConnection *connection = NULL;
  g_autofree char *object_path = NULL;
  gboolean hasTarget = FALSE;
  g_autoptr(GVariant) arg_pdata = NULL;
  g_autoptr(GVariant) activation_token = NULL;
  GVariantBuilder pdata, parms;

  g_print ("Stuff %s %s %s %s\n", arg_app_id, arg_notification_id, arg_name, g_variant_print (arg_parameter, TRUE));

  connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (object));
  object_path = app_path_for_id (arg_app_id);
  g_variant_builder_init (&pdata, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_init (&parms, G_VARIANT_TYPE ("av"));

  hasTarget = g_variant_n_children (arg_parameter) >= 2;

  if (hasTarget)
    {
      g_autoptr(GVariant) target = NULL;

      target = g_variant_get_child_value (arg_parameter, 0);
      g_variant_builder_add_value (&parms, target);
    }

  arg_pdata = g_variant_get_child_value (arg_parameter, hasTarget ? 1 : 0);
  activation_token = g_variant_lookup_value (arg_pdata, "activation-token", G_VARIANT_TYPE_STRING);

  if (activation_token)
    {
      /* Used by  `GTK` < 4.10 */
      g_variant_builder_add (&pdata, "{sv}",
                             "desktop-startup-id", activation_token);
      /* Used by `GTK` and `QT` */
      g_variant_builder_add (&pdata, "{sv}",
                             "activation-token", activation_token);
    }

  g_dbus_connection_call (connection,
                          arg_app_id,
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
                                                arg_app_id,
                                                arg_notification_id,
                                                arg_name,
                                                arg_parameter),
                                 NULL);
}

GVariant *
build_supported_options ()
{
    const char *supported_button_purposes[] = { "", NULL };
    const char *supported_action_purposes[] = { "", NULL };
    const char *supported_content_types[] = { "", NULL };
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_open (&builder, G_VARIANT_TYPE ("{sv}"));

    g_variant_builder_add (&builder, "{sv}", "button-purposes",
                                             g_variant_new_variant (g_variant_new_strv (supported_button_purposes, -1)));

    g_variant_builder_add (&builder, "{sv}", "action-purposes",
                                             g_variant_new_variant (g_variant_new_strv (supported_action_purposes, -1)));

    g_variant_builder_add (&builder, "{sv}", "content-type",
                                             g_variant_new_variant (g_variant_new_strv (supported_content_types, -1)));

    g_variant_builder_close (&builder);
    return g_variant_builder_end (&builder);
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

  g_signal_connect (gtk_notifications, "action-invoked", G_CALLBACK (handle_gtk_action_invoked), NULL);

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_notification_skeleton_new ());

  xdp_impl_notification_set_supported_options (XDP_IMPL_NOTIFICATION (helper),
                                                build_supported_options ());

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
