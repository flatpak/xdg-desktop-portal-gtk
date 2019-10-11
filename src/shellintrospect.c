/*
 * Copyright © 2019 Alberto Fanjul <albfan@gnome.org>
 * Copyright © 2019 Red Hat, Inc
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

#include "shell-dbus.h"
#include "shellintrospect.h"

enum
{
  WINDOWS_CHANGED,
  ANIMATIONS_ENABLED_CHANGED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _Window
{
  uint64_t id;
  char *title;
  char *app_id;
};

struct _ShellIntrospect
{
  GObject parent;

  guint shell_introspect_watch_name_id;
  GCancellable *cancellable;

  OrgGnomeShellIntrospect *proxy;

  unsigned int version;

  GList *windows;

  int num_listeners;

  gboolean animations_enabled;
  gboolean animations_enabled_valid;
};

G_DEFINE_TYPE (ShellIntrospect, shell_introspect, G_TYPE_OBJECT)

static ShellIntrospect *_shell_introspect;

static void
window_free (Window *window)
{
  g_free (window->title);
  g_free (window->app_id);
  g_free (window);
}

const char *
window_get_title (Window *window)
{
  return window->title;
}

const char *
window_get_app_id (Window *window)
{
  return window->app_id;
}

const uint64_t
window_get_id (Window *window)
{
  return window->id;
}

static void
get_windows_cb (GObject *source_object,
                GAsyncResult *res,
                gpointer user_data)
{
  ShellIntrospect *shell_introspect = user_data;
  g_autoptr(GVariant) windows_variant = NULL;
  g_autoptr(GError) error = NULL;
  GVariantIter iter;
  uint64_t id;
  GVariant *params = NULL;
  GList *windows = NULL;

  g_list_free_full (shell_introspect->windows, (GDestroyNotify) window_free);
  shell_introspect->windows = NULL;

  if (!org_gnome_shell_introspect_call_get_windows_finish (shell_introspect->proxy,
                                                           &windows_variant,
                                                           res,
                                                           &error))
    {
      g_warning ("Failed to get window list: %s", error->message);
      return;
    }

  g_variant_iter_init (&iter, windows_variant);
  while (g_variant_iter_loop (&iter, "{t@a{sv}}", &id, &params))
    {
      char *app_id = NULL;
      char *title = NULL;
      unsigned int time_since_user_time = UINT_MAX;
      Window *window;

      g_variant_lookup (params, "app-id", "s", &app_id);
      g_variant_lookup (params, "title", "s", &title);
      g_variant_lookup (params, "time-since-user-time", "u", &time_since_user_time);

      window = g_new0 (Window, 1);
      *window = (Window) {
        .id = id,
        .title = title,
        .app_id = app_id
      };
      windows = g_list_prepend (windows, window);

      g_clear_pointer (&params, g_variant_unref);
    }

  shell_introspect->windows = windows;
  g_signal_emit (shell_introspect, signals[WINDOWS_CHANGED], 0);
}

static void
sync_state (ShellIntrospect *shell_introspect)
{
  org_gnome_shell_introspect_call_get_windows (shell_introspect->proxy,
                                               shell_introspect->cancellable,
                                               get_windows_cb,
                                               shell_introspect);
}

GList *
shell_introspect_get_windows (ShellIntrospect *shell_introspect)
{
  return shell_introspect->windows;
}

void
shell_introspect_ref_listeners (ShellIntrospect *shell_introspect)
{
  shell_introspect->num_listeners++;

  if (shell_introspect->proxy)
    sync_state (shell_introspect);
}

void
shell_introspect_unref_listeners (ShellIntrospect *shell_introspect)
{
  g_return_if_fail (shell_introspect->num_listeners > 0);

  shell_introspect->num_listeners--;
  if (shell_introspect->num_listeners == 0)
    {
      g_list_free_full (shell_introspect->windows,
                        (GDestroyNotify) window_free);
      shell_introspect->windows = NULL;
    }
}

gboolean
shell_introspect_are_animations_enabled (ShellIntrospect *shell_introspect,
                                         gboolean        *out_animations_enabled)
{
  if (!shell_introspect->animations_enabled_valid)
    return FALSE;

  *out_animations_enabled = shell_introspect->animations_enabled;
  return TRUE;
}

static void
sync_animations_enabled (ShellIntrospect *shell_introspect)
{
  gboolean animations_enabled;

  animations_enabled =
    org_gnome_shell_introspect_get_animations_enabled (shell_introspect->proxy);
  if (shell_introspect->animations_enabled_valid &&
      animations_enabled == shell_introspect->animations_enabled)
    return;

  shell_introspect->animations_enabled_valid = TRUE;
  shell_introspect->animations_enabled = animations_enabled;
  g_signal_emit (shell_introspect, signals[ANIMATIONS_ENABLED_CHANGED], 0);
}

static void
on_animations_enabled_changed (GObject         *object,
                               GParamSpec      *pspec,
                               ShellIntrospect *shell_introspect)
{
  sync_animations_enabled (shell_introspect);
}

static void
on_shell_introspect_proxy_acquired (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  ShellIntrospect *shell_introspect = user_data;
  OrgGnomeShellIntrospect *proxy;
  g_autoptr(GError) error = NULL;

  proxy = org_gnome_shell_introspect_proxy_new_for_bus_finish (result,
                                                               &error);
  if (!proxy)
    {
      g_warning ("Failed to acquire org.gnome.Shell.Introspect proxy: %s",
                 error->message);
      return;
    }

  shell_introspect->proxy = proxy;

  if (shell_introspect->num_listeners > 0)
    sync_state (shell_introspect);

  shell_introspect->version =
    org_gnome_shell_introspect_get_version (shell_introspect->proxy);

  if (shell_introspect->version >= 2)
    {
      g_signal_connect (proxy, "notify::animations-enabled",
                        G_CALLBACK (on_animations_enabled_changed),
                        shell_introspect);
      sync_animations_enabled (shell_introspect);
    }
}

static void
on_shell_introspect_name_appeared (GDBusConnection *connection,
                                   const char *name,
                                   const char *name_owner,
                                   gpointer user_data)
{
  ShellIntrospect *shell_introspect = user_data;

  org_gnome_shell_introspect_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                                G_DBUS_PROXY_FLAGS_NONE,
                                                "org.gnome.Shell.Introspect",
                                                "/org/gnome/Shell/Introspect",
                                                shell_introspect->cancellable,
                                                on_shell_introspect_proxy_acquired,
                                                shell_introspect);
}

static void
on_shell_introspect_name_vanished (GDBusConnection *connection,
                                   const char *name,
                                   gpointer user_data)
{
  ShellIntrospect *shell_introspect = user_data;

  if (shell_introspect->cancellable)
    {
      g_cancellable_cancel (shell_introspect->cancellable);
      g_clear_object (&shell_introspect->cancellable);
    }
}

ShellIntrospect *
shell_introspect_get (void)
{
  ShellIntrospect *shell_introspect;

  if (_shell_introspect)
    return _shell_introspect;

  shell_introspect = g_object_new (shell_introspect_get_type (), NULL);
  shell_introspect->shell_introspect_watch_name_id =
    g_bus_watch_name (G_BUS_TYPE_SESSION,
                      "org.gnome.Shell.Introspect",
                      G_BUS_NAME_WATCHER_FLAGS_NONE,
                      on_shell_introspect_name_appeared,
                      on_shell_introspect_name_vanished,
                      shell_introspect, NULL);
  _shell_introspect = shell_introspect;
  return shell_introspect;
}

static void
shell_introspect_init (ShellIntrospect *shell_introspect)
{
}

static void
shell_introspect_class_init (ShellIntrospectClass *klass)
{
  signals[WINDOWS_CHANGED] = g_signal_new ("windows-changed",
                                           G_TYPE_FROM_CLASS (klass),
                                           G_SIGNAL_RUN_LAST,
                                           0,
                                           NULL, NULL, NULL,
                                           G_TYPE_NONE, 0);
  signals[ANIMATIONS_ENABLED_CHANGED] =
    g_signal_new ("animations-enabled-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}
