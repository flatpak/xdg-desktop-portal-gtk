/*
 * Copyright © 2019 Alberto Fanjul <albfan@gnome.org>
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

typedef struct _Window
{
  guint64 id;
  gchar *title;
  gchar *app_id;
} Window;

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

const guint64
window_get_id (Window *window)
{
  return window->id;
}

GList *
get_available_windows ()
{
  GList *list = NULL;
  Window *window;

  GDBusProxy *proxy;
  GDBusConnection *conn;

  GVariant *result;
  GVariant *params;
  GVariantIter *iter;
  gchar *app_id;
  gchar *title;
  guint64 id;
  GError *error = NULL;

  conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error(error);

  proxy = g_dbus_proxy_new_sync(conn,
                                G_DBUS_PROXY_FLAGS_NONE,
                                NULL,
                                "org.gnome.Shell",
                                "/org/gnome/Shell/Introspect",
                                "org.gnome.Shell.Introspect",
                                NULL,
                                &error);
  g_assert_no_error(error);

  result = g_dbus_proxy_call_sync(proxy,
              "GetWindows",
              NULL,
              G_DBUS_CALL_FLAGS_NONE,
              -1,
              NULL,
              &error);
  g_assert_no_error(error);

  g_variant_get (result, "(a{ta{sv}})", &iter);
  while (g_variant_iter_loop (iter, "{t@a{sv}}",&id, &params)) {
     g_variant_lookup (params, "app-id", "s", &app_id);
     g_variant_lookup (params, "title", "s", &title);
     window = g_new0 (Window, 1);
     *window = (Window) {
       .id = id,
       .title = title,
       .app_id = app_id
     };
     list = g_list_append(list, window);
  }
  g_variant_iter_free (iter);
  g_variant_unref(result);
  g_object_unref(proxy);
  g_object_unref(conn);

  return list;
}

