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
#include "request.h"

/* We use org.gtk.Notifications here, since that lets us just pass through
 * directly. It should be possible to support org.freedesktop.Notifications
 * as well, but for that we need to intercept actions and translate them
 * back to GAction activations on the app.
 */
static OrgGtkNotifications *shell;

static gboolean
handle_add_notification (XdpImplNotification *object,
                         GDBusMethodInvocation *invocation,
                         const char *arg_app_id,
                         const char *arg_id,
                         GVariant *notification)
{
  org_gtk_notifications_call_add_notification (shell,
                                               arg_app_id,
                                               arg_id,
                                               notification,
                                               NULL,
                                               NULL,
                                               NULL);

  xdp_impl_notification_complete_add_notification (object, invocation);

  return TRUE;
}

static gboolean
handle_remove_notification (XdpImplNotification *object,
                            GDBusMethodInvocation *invocation,
                            const char *arg_app_id,
                            const char *arg_id)
{
  org_gtk_notifications_call_remove_notification (shell,
                                                  arg_app_id,
                                                  arg_id,
                                                  NULL,
                                                  NULL,
                                                  NULL);

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
                                         "/org/freedesktop/portal/desktop",
                                         error))
    return FALSE;

  shell = org_gtk_notifications_proxy_new_sync (bus,
                                                G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                "org.gtk.Notifications",
                                                "/org/gtk/Notifications",
                                                NULL,
                                                error);
  if (shell == NULL)
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
