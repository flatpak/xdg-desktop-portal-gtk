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

#include "shell-dbus.h"
#include "displaystatetracker.h"

enum
{
  MONITORS_CHANGED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct _Monitor
{
  char *connector;
  char *display_name;
} Monitor;

typedef struct _LogicalMonitor
{
  gboolean is_primary;
  GList *monitors;
} LogicalMonitor;

struct _DisplayStateTracker
{
  GObject parent;

  guint display_config_watch_name_id;
  GCancellable *cancellable;

  OrgGnomeMutterDisplayConfig *proxy;

  GHashTable *monitors;
  GList *logical_monitors;
};

G_DEFINE_TYPE (DisplayStateTracker, display_state_tracker, G_TYPE_OBJECT)

static DisplayStateTracker *_display_state_tracker;

static void
monitor_free (Monitor *monitor)
{
  g_free (monitor->connector);
  g_free (monitor->display_name);
  g_free (monitor);
}

const char *
monitor_get_connector (Monitor *monitor)
{
  return monitor->connector;
}

const char *
monitor_get_display_name (Monitor *monitor)
{
  return monitor->display_name;
}

GList *
logical_monitor_get_monitors (LogicalMonitor *logical_monitor)
{
  return logical_monitor->monitors;
}

gboolean
logical_monitor_is_primary (LogicalMonitor *logical_monitor)
{
  return logical_monitor->is_primary;
}

GList *
display_state_tracker_get_logical_monitors (DisplayStateTracker *tracker)
{
  return tracker->logical_monitors;
}

static void
generate_monitors (DisplayStateTracker *tracker,
                   GVariant *monitors)
{
  GVariantIter monitors_iter;
  GVariant *monitor_variant;

  g_variant_iter_init (&monitors_iter, monitors);
  while ((monitor_variant = g_variant_iter_next_value (&monitors_iter)))
    {
      Monitor *monitor;
      char *connector;
      char *display_name;
      GVariant *properties;

      g_variant_get (monitor_variant, "((ssss)a(siiddada{sv})@a{sv})",
                     &connector,
                     NULL /* vendor */,
                     NULL /* product */,
                     NULL /* serial */,
                     NULL /* modes */,
                     &properties);

      if (!g_variant_lookup (properties, "display-name", "s", &display_name))
        display_name = g_strdup (connector);

      monitor = g_new0 (Monitor, 1);
      *monitor = (Monitor) {
        .connector = connector,
        .display_name = display_name
      };

      g_hash_table_insert (tracker->monitors, connector, monitor);

      g_variant_unref (monitor_variant);
    }
}

static void
generate_logical_monitors (DisplayStateTracker *tracker,
                           GVariant *logical_monitors)
{
  GVariantIter logical_monitors_iter;
  GVariant *logical_monitor_variant;

  g_variant_iter_init (&logical_monitors_iter, logical_monitors);
  while ((logical_monitor_variant = g_variant_iter_next_value (&logical_monitors_iter)))
    {
      LogicalMonitor *logical_monitor;
      gboolean is_primary;
      g_autoptr(GVariantIter) monitors_iter;
      GVariant *monitor_variant;

      g_variant_get (logical_monitor_variant, "(iiduba(ssss)a{sv})",
                     NULL /* x */,
                     NULL /* y */,
                     NULL /* scale */,
                     NULL /* transform */,
                     &is_primary,
                     &monitors_iter,
                     NULL /* properties */);

      logical_monitor = g_new0 (LogicalMonitor, 1);
      *logical_monitor = (LogicalMonitor) {
        .is_primary = is_primary
      };

      while ((monitor_variant = g_variant_iter_next_value (monitors_iter)))
        {
          g_autofree char *connector = NULL;
          Monitor *monitor;

          g_variant_get (monitor_variant, "(ssss)",
                         &connector,
                         NULL /* vendor */,
                         NULL /* product */,
                         NULL /* serial */);

          monitor = g_hash_table_lookup (tracker->monitors, connector);

          logical_monitor->monitors = g_list_append (logical_monitor->monitors, monitor);

          g_variant_unref (monitor_variant);
        }

      tracker->logical_monitors = g_list_append (tracker->logical_monitors,
                                                 logical_monitor);

      g_variant_unref (logical_monitor_variant);
    }
}

static void
get_current_state_cb (GObject *source_object,
                      GAsyncResult *res,
                      gpointer user_data)
{
  DisplayStateTracker *tracker = user_data;
  g_autoptr(GVariant) monitors = NULL;
  g_autoptr(GVariant) logical_monitors = NULL;
  g_autoptr(GError) error = NULL;

  if (!org_gnome_mutter_display_config_call_get_current_state_finish (tracker->proxy,
                                                                      NULL,
                                                                      &monitors,
                                                                      &logical_monitors,
                                                                      NULL /* props */,
                                                                      res,
                                                                      &error))
    {
      g_warning ("Failed to get current display state: %s", error->message);
      return;
    }

  g_list_free_full (tracker->logical_monitors, g_free);
  tracker->logical_monitors = NULL;
  g_hash_table_remove_all (tracker->monitors);
  generate_monitors (tracker, monitors);
  generate_logical_monitors (tracker, logical_monitors);

  g_signal_emit (tracker, signals[MONITORS_CHANGED], 0);
}

static void
sync_state (DisplayStateTracker *tracker)
{
  org_gnome_mutter_display_config_call_get_current_state (tracker->proxy,
                                                          tracker->cancellable,
                                                          get_current_state_cb,
                                                          tracker);
}

static void
on_monitors_changed (OrgGnomeMutterDisplayConfig *proxy,
                     DisplayStateTracker *tracker)
{
  sync_state (tracker);
}

static void
on_display_config_proxy_acquired (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  DisplayStateTracker *tracker = user_data;
  OrgGnomeMutterDisplayConfig *proxy;
  g_autoptr(GError) error = NULL;

  proxy = org_gnome_mutter_display_config_proxy_new_for_bus_finish (result, &error);
  if (!proxy)
    {
      g_warning ("Failed to acquire org.gnome.Mutter.DisplayConfig proxy: %s",
                 error->message);
      return;
    }

  tracker->proxy = proxy;

  g_signal_connect (proxy, "monitors-changed",
                    G_CALLBACK (on_monitors_changed),
                    tracker);

  sync_state (tracker);
}

static void
on_display_config_name_appeared (GDBusConnection *connection,
                                 const char *name,
                                 const char *name_owner,
                                 gpointer user_data)
{
  DisplayStateTracker *tracker = user_data;

  org_gnome_mutter_display_config_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                                     G_DBUS_PROXY_FLAGS_NONE,
                                                     "org.gnome.Mutter.DisplayConfig",
                                                     "/org/gnome/Mutter/DisplayConfig",
                                                     tracker->cancellable,
                                                     on_display_config_proxy_acquired,
                                                     tracker);
}

static void
on_display_config_name_vanished (GDBusConnection *connection,
                                 const char *name,
                                 gpointer user_data)
{
  DisplayStateTracker *tracker = user_data;

  if (tracker->cancellable)
    {
      g_cancellable_cancel (tracker->cancellable);
      g_clear_object (&tracker->cancellable);
    }
}

DisplayStateTracker *
display_state_tracker_get (void)
{
  DisplayStateTracker *tracker;

  if (_display_state_tracker)
    return _display_state_tracker;

  tracker = g_object_new (display_state_tracker_get_type (), NULL);
  tracker->display_config_watch_name_id =
    g_bus_watch_name (G_BUS_TYPE_SESSION,
                      "org.gnome.Mutter.DisplayConfig",
                      G_BUS_NAME_WATCHER_FLAGS_NONE,
                      on_display_config_name_appeared,
                      on_display_config_name_vanished,
                      tracker, NULL);

  _display_state_tracker = tracker;
  return tracker;
}

static void
display_state_tracker_init (DisplayStateTracker *tracker)
{
  tracker->monitors = g_hash_table_new_full (g_str_hash, g_str_equal,
                                             NULL, (GDestroyNotify) monitor_free);
}

static void
display_state_tracker_class_init (DisplayStateTrackerClass *klass)
{
  signals[MONITORS_CHANGED] = g_signal_new ("monitors-changed",
                                            G_TYPE_FROM_CLASS (klass),
                                            G_SIGNAL_RUN_LAST,
                                            0,
                                            NULL, NULL, NULL,
                                            G_TYPE_NONE, 0);
}
