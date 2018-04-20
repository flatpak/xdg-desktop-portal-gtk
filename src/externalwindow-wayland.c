/*
 * Copyright © 2016 Red Hat, Inc
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
 * Authors:
 *       Jonas Ådahl <jadahl@redhat.com>
 */

#include "config.h"

#include <gdk/gdk.h>
#include <gdk/gdkwayland.h>

#include "externalwindow-wayland.h"

static GdkDisplay *wayland_display;

struct _ExternalWindowWayland
{
  ExternalWindow parent;

  char *handle_str;
};

struct _ExternalWindowWaylandClass
{
  ExternalWindowClass parent_class;
};

G_DEFINE_TYPE (ExternalWindowWayland, external_window_wayland,
               EXTERNAL_TYPE_WINDOW)

static GdkDisplay *
get_wayland_display (void)
{
  if (wayland_display)
    return wayland_display;

  gdk_set_allowed_backends ("wayland");
  wayland_display = gdk_display_open (NULL);
  gdk_set_allowed_backends (NULL);
  if (!wayland_display)
    g_warning ("Failed to open Wayland display");

  return wayland_display;
}

ExternalWindowWayland *
external_window_wayland_new (const char *handle_str)
{
  ExternalWindowWayland *external_window_wayland;
  GdkDisplay *display;

  display = get_wayland_display ();
  if (!display)
    {
      g_warning ("No Wayland display connection, ignoring Wayland parent");
      return NULL;
    }

  external_window_wayland = g_object_new (EXTERNAL_TYPE_WINDOW_WAYLAND,
                                          "display", display,
                                          NULL);
  external_window_wayland->handle_str = g_strdup (handle_str);

  return external_window_wayland;
}

static void
external_window_wayland_set_parent_of (ExternalWindow *external_window,
                                       GdkWindow      *child_window)
{
  ExternalWindowWayland *external_window_wayland =
    EXTERNAL_WINDOW_WAYLAND (external_window);
  char *handle_str = external_window_wayland->handle_str;

  if (!gdk_wayland_window_set_transient_for_exported (child_window, handle_str))
    g_warning ("Failed to set portal window transient for external parent");
}

static void
external_window_wayland_dispose (GObject *object)
{
  ExternalWindowWayland *external_window_wayland =
    EXTERNAL_WINDOW_WAYLAND (object);

  g_free (external_window_wayland->handle_str);

  G_OBJECT_CLASS (external_window_wayland_parent_class)->dispose (object);
}

static void
external_window_wayland_init (ExternalWindowWayland *external_window_wayland)
{
}

static void
external_window_wayland_class_init (ExternalWindowWaylandClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ExternalWindowClass *external_window_class = EXTERNAL_WINDOW_CLASS (klass);

  object_class->dispose = external_window_wayland_dispose;

  external_window_class->set_parent_of = external_window_wayland_set_parent_of;
}
