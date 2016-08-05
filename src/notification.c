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
#include "utils.h"

/* org.gtk.Notifications support. This is easy, since we can
 * just pass the calls through unseen, and gnome-shell does
 * the right thing.
 */
static OrgGtkNotifications *gtk_notifications;

static gboolean
handle_add_notification_gtk (XdpImplNotification *object,
                             GDBusMethodInvocation *invocation,
                             const char *arg_sender,
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
                                                 NULL,
                                                 NULL);

  xdp_impl_notification_complete_add_notification (object, invocation);

  return TRUE;
}

static gboolean
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

  xdp_impl_notification_complete_remove_notification (object, invocation);

  return TRUE;
}

/* org.freedesktop.Notifications support.
 * This code is adapted from the GFdoNotificationBackend in GIO.
 */

static guint fdo_notify_subscription;
static GSList *fdo_notifications;

typedef struct
{
  char *sender;
  char *app_id;
  char *id;
  guint32 notify_id;
  char *default_action;
  GVariant *default_action_target;
} FdoNotification;

static void
fdo_notification_free (gpointer data)
{
  FdoNotification *n = data;

  g_free (n->sender);
  g_free (n->app_id);
  g_free (n->id);
  g_free (n->default_action);
  if (n->default_action_target)
    g_variant_unref (n->default_action_target);

  g_slice_free (FdoNotification, n);
}

static FdoNotification *
fdo_find_notification (const char *app_id,
                       const char *id)
{
  GSList *l;

  for (l = fdo_notifications; l != NULL; l = l->next)
    {
      FdoNotification *n = l->data;
      if (g_str_equal (n->app_id, app_id) &&
          g_str_equal (n->id, id))
        return n;
    }

  return NULL;
}

static FdoNotification *
fdo_find_notification_by_notify_id (guint32 id)
{
  GSList *l;

  for (l = fdo_notifications; l != NULL; l = l->next)
    {
      FdoNotification *n = l->data;
      if (n->notify_id == id)
        return n;
    }

  return NULL;
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
                 const char *sender,
                 const char *app_id,
                 const char *id,
                 const char *name,
                 GVariant *parameter)
{
  g_autofree char *object_path = NULL;
  GVariantBuilder pdata;

  object_path = app_path_for_id (app_id);
  g_variant_builder_init (&pdata, G_VARIANT_TYPE_VARDICT);

  if (name && g_str_has_prefix (name, "app."))
    {
      g_dbus_connection_call (connection,
                              app_id,
                              object_path,
                              "org.freedesktop.Application",
                              "ActivateAction",
                              g_variant_new ("(sav@a{sv})",
                                             name + 4,
                                             parameter,
                                             g_variant_builder_end (&pdata)),
                              NULL,
                              G_DBUS_CALL_FLAGS_NONE,
                              -1, NULL, NULL, NULL);
    }
  else
    {
      g_autoptr(GVariant) ret = NULL;
      GVariantBuilder parms;

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

      g_variant_builder_init (&parms, G_VARIANT_TYPE ("av"));
      if (parameter)
        g_variant_builder_add (&parms, "v", parameter);

      g_dbus_connection_emit_signal (connection,
                                     sender,
                                     "/org/freedesktop/portal/desktop",
                                     "org.freedesktop.portal.Notification",
                                     "ActionInvoked",
                                     g_variant_new ("(ss@av)",
                                                    id, name,
                                                    g_variant_builder_end (&parms)),
                                     NULL);
    }
}

static void
notify_signal (GDBusConnection *connection,
               const char *sender_name,
               const char *object_path,
               const char *interface_name,
               const char *signal_name,
               GVariant *parameters,
               gpointer user_data)
{
  guint32 id = 0;
  const char *action = NULL;
  FdoNotification *n;

  if (g_str_equal (signal_name, "NotificationClosed") &&
      g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(uu)")))
    {
      g_variant_get (parameters, "(uu)", &id, NULL);
    }
  else if (g_str_equal (signal_name, "ActionInvoked") &&
           g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(us)")))
    {
      g_variant_get (parameters, "(u&s)", &id, &action);
    }
  else
    return;

  n = fdo_find_notification_by_notify_id (id);
  if (n == NULL)
    return;

  if (action)
    {
      if (g_str_equal (action, "default"))
        {
          activate_action (connection,
                           n->sender,
                           n->app_id,
                           n->id,
                           n->default_action,
                           n->default_action_target);
        }
      else
        {
          gchar *name;
          GVariant *target;

          if (g_action_parse_detailed_name (action, &name, &target, NULL))
            {
              activate_action (connection,
                               n->sender,
                               n->app_id,
                               n->id,
                               name,
                               target);
              g_free (name);
              if (target)
                g_variant_unref (target);
            }
        }
    }

  fdo_notifications = g_slist_remove (fdo_notifications, n);
  fdo_notification_free (n);
}

static guchar
urgency_from_priority (const char *priority)
{
  if (strcmp (priority, "low") == 0)
    return 0;
  else if (strcmp (priority, "normal") == 0)
    return 1;
  else
    return 2;
}

static void
call_notify (GDBusConnection *connection,
             const char *app_id,
             guint32 replace_id,
             GVariant *notification,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
  GVariantBuilder action_builder;
  guint i;
  GVariantBuilder hints_builder;
  GVariant *icon;
  const char *body;
  const char *title;
  guchar urgency;
  const char *dummy;
  g_autoptr(GVariant) buttons = NULL;
  const char *priority;

  g_variant_builder_init (&action_builder, G_VARIANT_TYPE_STRING_ARRAY);
  if (g_variant_lookup (notification, "default-action", "&s", &dummy))
    {
      g_variant_builder_add (&action_builder, "s", "default");
      g_variant_builder_add (&action_builder, "s", "");
    }

  buttons = g_variant_lookup_value (notification, "buttons", G_VARIANT_TYPE("aa{sv}"));
  if (buttons)
    for (i = 0; i < g_variant_n_children (buttons); i++)
      {
        g_autoptr(GVariant) button = NULL;
        const char *label;
        const char *action;
        g_autoptr(GVariant) target = NULL;
        g_autofree char *detailed_name = NULL;

        button = g_variant_get_child_value (buttons, i);
        g_variant_lookup (button, "label", "&s", &label);
        g_variant_lookup (button, "action", "&s", &action);
        target = g_variant_lookup_value (button, "target", G_VARIANT_TYPE_VARIANT);
        detailed_name = g_action_print_detailed_name (action, target);

        /* Actions named 'default' collide with libnotify's naming of the
         * default action. Rewriting them to something unique is enough,
         * because those actions can never be activated (they aren't
         * prefixed with 'app.').
         */
        if (g_str_equal (detailed_name, "default"))
          {
            g_free (detailed_name);
            detailed_name = g_dbus_generate_guid ();
          }

        g_variant_builder_add_value (&action_builder, g_variant_new_string (detailed_name));
        g_variant_builder_add_value (&action_builder, g_variant_new_string (label));
      }

  g_variant_builder_init (&hints_builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&hints_builder, "{sv}", "desktop-entry", g_variant_new_string (app_id));
  if (g_variant_lookup (notification, "priority", "&s", &priority))
    urgency = urgency_from_priority (priority);
  else
    urgency = 1;
  g_variant_builder_add (&hints_builder, "{sv}", "urgency", g_variant_new_byte (urgency));

  icon = g_variant_lookup_value (notification, "icon", G_VARIANT_TYPE_VARIANT);
  if (icon != NULL)
    {
      g_autoptr(GIcon) gicon = g_icon_deserialize (icon);
      if (G_IS_FILE_ICON (gicon))
        {
           GFile *file;

           file = g_file_icon_get_file (G_FILE_ICON (gicon));
           g_variant_builder_add (&hints_builder, "{sv}", "image-path",
                                  g_variant_new_take_string (g_file_get_path (file)));
        }
      else if (G_IS_THEMED_ICON (gicon))
        {
           const gchar* const* icon_names = g_themed_icon_get_names (G_THEMED_ICON (gicon));
           /* Take first name from GThemedIcon */
           g_variant_builder_add (&hints_builder, "{sv}", "image-path",
                                  g_variant_new_string (icon_names[0]));
        }
    }

  if (!g_variant_lookup (notification, "body", "&s", &body))
    body = "";
  if (!g_variant_lookup (notification, "title", "&s", &title))
    title= "";

  g_dbus_connection_call (connection,
                          "org.freedesktop.Notifications",
                          "/org/freedesktop/Notifications",
                          "org.freedesktop.Notifications",
                          "Notify",
                          g_variant_new ("(susssasa{sv}i)",
                                         "", /* app name */
                                         replace_id,
                                         "", /* app icon */
                                         title,
                                         body,
                                         &action_builder,
                                         &hints_builder,
                                         -1), /* expire_timeout */
                          G_VARIANT_TYPE ("(u)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1, NULL,
                          callback, user_data);
}

static void
notification_sent (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  FdoNotification *n = user_data;
  GVariant *val;
  GError *error = NULL;
  static gboolean warning_printed = FALSE;

  val = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object), result, &error);
  if (val)
    {
      g_variant_get (val, "(u)", &n->notify_id);
      g_variant_unref (val);
    }
  else
    {
      if (!warning_printed)
        {
          g_warning ("Unable to send notifications through org.freedesktop.Notifications: %s",
                     error->message);
          warning_printed = TRUE;
        }

      fdo_notifications = g_slist_remove (fdo_notifications, n);
      fdo_notification_free (n);

      g_error_free (error);
    }
}

static void
handle_add_notification_fdo (XdpImplNotification *object,
                             GDBusMethodInvocation *invocation,
                             const gchar *arg_sender,
                             const gchar *arg_app_id,
                             const gchar *arg_id,
                             GVariant *arg_notification)
{
  FdoNotification *n;
  GDBusConnection *connection;

  connection = g_dbus_method_invocation_get_connection (invocation);

  if (fdo_notify_subscription == 0)
    {
      fdo_notify_subscription =
        g_dbus_connection_signal_subscribe (connection,
                                            "org.freedesktop.Notifications",
                                            "org.freedesktop.Notifications", NULL,
                                            "/org/freedesktop/Notifications", NULL,
                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                            notify_signal, NULL, NULL);
    }

  n = fdo_find_notification (arg_app_id, arg_id);
  if (n == NULL)
    {
      n = g_slice_new0 (FdoNotification);
      n->app_id = g_strdup (arg_app_id);
      n->id = g_strdup (arg_id);
      n->sender = g_strdup (arg_sender);
      n->notify_id = 0;

      fdo_notifications = g_slist_prepend (fdo_notifications, n);
    }
  else
    {
      /* Only clear default action. All other fields are still valid */
      g_clear_pointer (&n->default_action, g_free);
      g_clear_pointer (&n->default_action_target, g_variant_unref);
    }

  g_variant_lookup (arg_notification, "default-action", "s", &n->default_action);
  n->default_action_target = g_variant_lookup_value (arg_notification, "default-action-target", G_VARIANT_TYPE_VARIANT);

  call_notify (connection,
               arg_app_id,
               n->notify_id,
               arg_notification,
               notification_sent, n);
}

static void
handle_remove_notification_fdo (XdpImplNotification *object,
                                GDBusMethodInvocation *invocation,
                                const gchar *arg_app_id,
                                const gchar *arg_id)
{
  FdoNotification *n;

  n = fdo_find_notification (arg_app_id, arg_id);
  if (n)
    {
      if (n->notify_id > 0)
        {
          g_dbus_connection_call (g_dbus_method_invocation_get_connection (invocation),
                                  "org.freedesktop.Notifications",
                                  "/org/freedesktop/Notifications",
                                  "org.freedesktop.Notifications",
                                  "CloseNotification",
                                  g_variant_new ("(u)", n->id),
                                  NULL,
                                  G_DBUS_CALL_FLAGS_NONE,
                                  -1, NULL, NULL, NULL);
        }

      fdo_notifications = g_slist_remove (fdo_notifications, n);
      fdo_notification_free (n);
    }
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

static void
handle_add_notification (XdpImplNotification *object,
                         GDBusMethodInvocation *invocation,
                         const gchar *arg_sender,
                         const gchar *arg_app_id,
                         const gchar *arg_id,
                         GVariant *arg_notification)
{
  if (gtk_notifications == NULL ||
      g_dbus_proxy_get_name_owner (G_DBUS_PROXY (gtk_notifications)) == NULL ||
      has_unprefixed_action (arg_notification))
    handle_add_notification_fdo (object, invocation, arg_sender, arg_app_id, arg_id, arg_notification);
  else
    handle_add_notification_gtk (object, invocation, arg_sender, arg_app_id, arg_id, arg_notification);
}

static void
handle_remove_notification (XdpImplNotification *object,
                            GDBusMethodInvocation *invocation,
                            const gchar *arg_app_id,
                            const gchar *arg_id)
{
  FdoNotification *n;

  n = fdo_find_notification (arg_app_id, arg_id);
  if (n)
    handle_remove_notification_fdo (object, invocation, arg_app_id, arg_id);
  else
    handle_remove_notification_gtk (object, invocation, arg_app_id, arg_id);
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
