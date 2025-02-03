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

static gboolean
handle_add_notification (XdpImplNotification *object,
                         GDBusMethodInvocation *invocation,
#ifdef HAVE_XDP_1_19_1
                         GUnixFDList *fds G_GNUC_UNUSED,
#endif
                         const gchar *arg_app_id,
                         const gchar *arg_id,
                         GVariant *arg_notification)
{
  GDBusConnection *connection;

  connection = g_dbus_method_invocation_get_connection (invocation);

  fdo_add_notification (connection, arg_app_id, arg_id, arg_notification, activate_action, NULL);

#ifdef HAVE_XDP_1_19_1
  xdp_impl_notification_complete_add_notification (object, invocation, NULL);
#else
  xdp_impl_notification_complete_add_notification (object, invocation);
#endif

  return TRUE;
}

static gboolean
handle_remove_notification (XdpImplNotification *object,
                            GDBusMethodInvocation *invocation,
                            const gchar *arg_app_id,
                            const gchar *arg_id)
{
  GDBusConnection *connection;

  connection = g_dbus_method_invocation_get_connection (invocation);

  fdo_remove_notification (connection, arg_app_id, arg_id);

  xdp_impl_notification_complete_remove_notification (object, invocation);

  return TRUE;
}

gboolean
notification_init (GDBusConnection *bus,
                   GError **error)
{
  GDBusInterfaceSkeleton *helper;

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
