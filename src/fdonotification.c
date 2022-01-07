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

/* org.freedesktop.Notifications support.
 * This code is adapted from the GFdoNotificationBackend in GIO.
 */

static guint fdo_notify_subscription;
static GSList *fdo_notifications;

typedef struct
{
  char *app_id;
  char *id;
  guint32 notify_id;
  char *default_action;
  GVariant *default_action_target;
  ActivateAction activate_action;
  gpointer data;
} FdoNotification;

static void
fdo_notification_free (gpointer data)
{
  FdoNotification *n = data;

  g_free (n->app_id);
  g_free (n->id);
  g_free (n->default_action);
  if (n->default_action_target)
    g_variant_unref (n->default_action_target);

  g_slice_free (FdoNotification, n);
}

FdoNotification *
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

FdoNotification *
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
          n->activate_action (connection,
                              n->app_id,
                              n->id,
                              n->default_action,
                              n->default_action_target,
                              n->data);
        }
      else
        {
          gchar *name;
          GVariant *target;

          if (g_action_parse_detailed_name (action, &name, &target, NULL))
            {
              n->activate_action (connection,
                                  n->app_id,
                                  n->id,
                                  name,
                                  target,
                                  n->data);
              g_free (name);
              if (target)
                g_variant_unref (target);
            }
          else
            g_debug ("Could not parse action name %s", action);
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
call_notify (GDBusConnection *connection,
             FdoNotification *fdo,
             GVariant *notification)
{
  GVariantBuilder action_builder;
  guint i;
  GVariantBuilder hints_builder;
  GVariant *icon;
  const char *body;
  const char *title;
  g_autofree char *icon_name = NULL;
  guchar urgency;
  const char *dummy;
  g_autoptr(GVariant) buttons = NULL;
  const char *priority;

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
        target = g_variant_lookup_value (button, "target", NULL);
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
  g_variant_builder_add (&hints_builder, "{sv}", "desktop-entry", g_variant_new_string (fdo->app_id));
  if (g_variant_lookup (notification, "priority", "&s", &priority))
    urgency = urgency_from_priority (priority);
  else
    urgency = 1;
  g_variant_builder_add (&hints_builder, "{sv}", "urgency", g_variant_new_byte (urgency));

  icon = g_variant_lookup_value (notification, "icon", NULL);
  if (icon != NULL)
    {
      g_autoptr(GIcon) gicon = g_icon_deserialize (icon);
      if (G_IS_FILE_ICON (gicon))
        {
           GFile *file;

           file = g_file_icon_get_file (G_FILE_ICON (gicon));
           icon_name = g_file_get_path (file);
        }
      else if (G_IS_THEMED_ICON (gicon))
        {
           const gchar* const* icon_names = g_themed_icon_get_names (G_THEMED_ICON (gicon));
           icon_name = g_strdup (icon_names[0]);
        }
      else if (G_IS_BYTES_ICON (gicon))
        {
           g_autoptr(GInputStream) istream = NULL;
           g_autoptr(GdkPixbuf) pixbuf = NULL;
           int width, height, rowstride, n_channels, bits_per_sample;
           GVariant *image;
           gsize image_len;

           istream = g_loadable_icon_load (G_LOADABLE_ICON (gicon),
                                           -1 /* unused */,
                                           NULL /* type */,
                                           NULL,
                                           NULL);
           pixbuf = gdk_pixbuf_new_from_stream (istream, NULL, NULL);
           g_input_stream_close (istream, NULL, NULL);

           g_object_get (pixbuf,
                         "width", &width,
                         "height", &height,
                         "rowstride", &rowstride,
                         "n-channels", &n_channels,
                         "bits-per-sample", &bits_per_sample,
                         NULL);

           image_len = (height - 1) * rowstride + width *
                       ((n_channels * bits_per_sample + 7) / 8);

           image = g_variant_new ("(iiibii@ay)",
                                  width,
                                  height,
                                  rowstride,
                                  gdk_pixbuf_get_has_alpha (pixbuf),
                                  bits_per_sample,
                                  n_channels,
                                  g_variant_new_from_data (G_VARIANT_TYPE ("ay"),
                                                           gdk_pixbuf_get_pixels (pixbuf),
                                                           image_len,
                                                           TRUE,
                                                           (GDestroyNotify) g_object_unref,
                                                           g_object_ref (pixbuf)));
           g_variant_builder_add (&hints_builder, "{sv}", "image-data", image);
        }
    }

  if (icon_name == NULL)
    icon_name = g_strdup ("");

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
                                         fdo->notify_id,
                                         icon_name,
                                         title,
                                         body,
                                         &action_builder,
                                         &hints_builder,
                                         -1), /* expire_timeout */
                          G_VARIANT_TYPE ("(u)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1, NULL,
                          notification_sent, fdo);
}

static void
call_close (GDBusConnection *connection,
            guint32 id)
{
  g_dbus_connection_call (connection,
                          "org.freedesktop.Notifications",
                          "/org/freedesktop/Notifications",
                          "org.freedesktop.Notifications",
                          "CloseNotification",
                          g_variant_new ("(u)", id),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1, NULL, NULL, NULL);
}

gboolean
fdo_remove_notification (GDBusConnection *connection,
                         const char *app_id,
                         const char *id)
{
  FdoNotification *n;

  n = fdo_find_notification (app_id, id);
  if (n)
    {
      if (n->notify_id > 0)
        call_close (connection, n->notify_id);

      fdo_notifications = g_slist_remove (fdo_notifications, n);
      fdo_notification_free (n);

      return TRUE;
    }

  return FALSE;
}

void
fdo_add_notification (GDBusConnection *connection,
                      const char *app_id,
                      const char *id,
                      GVariant *notification,
                      ActivateAction activate_action,
                      gpointer data)
{
  FdoNotification *n;

  n = fdo_find_notification (app_id, id);
  if (n == NULL)
    {
      n = g_slice_new0 (FdoNotification);
      n->app_id = g_strdup (app_id);
      n->id = g_strdup (id);
      n->notify_id = 0;
      n->activate_action = activate_action;
      n->data = data;
    }
  else
    {
      /* Only clear default action. All other fields are still valid */
      g_clear_pointer (&n->default_action, g_free);
      g_clear_pointer (&n->default_action_target, g_variant_unref);
    }

  g_variant_lookup (notification, "default-action", "s", &n->default_action);
  n->default_action_target = g_variant_lookup_value (notification, "default-action-target", NULL);

  fdo_notifications = g_slist_prepend (fdo_notifications, n);

  call_notify (connection, n, notification);
}

