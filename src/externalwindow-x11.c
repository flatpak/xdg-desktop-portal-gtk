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

#include <errno.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>
#include <stdlib.h>

#include "externalwindow-x11.h"


static GdkDisplay *x11_display;

struct _ExternalWindowX11
{
  ExternalWindow parent;

  GdkWindow *foreign_gdk_window;
};

struct _ExternalWindowX11Class
{
  ExternalWindowClass parent_class;
};

G_DEFINE_TYPE (ExternalWindowX11, external_window_x11,
               EXTERNAL_TYPE_WINDOW)

static GdkDisplay *
get_x11_display (void)
{
  if (x11_display)
    return x11_display;

  gdk_set_allowed_backends ("x11");
  x11_display = gdk_display_open (NULL);
  gdk_set_allowed_backends (NULL);
  if (!x11_display)
    g_warning ("Failed to open X11 display");

  return x11_display;
}

ExternalWindowX11 *
external_window_x11_new (const char *handle_str)
{
  ExternalWindowX11 *external_window_x11;
  GdkDisplay *display;
  int xid;
  GdkWindow *foreign_gdk_window;

  display = get_x11_display ();
  if (!display)
    {
      g_warning ("No X display connection, ignoring X11 parent");
      return NULL;
    }

  errno = 0;
  xid = strtol (handle_str, NULL, 16);
  if (errno != 0)
    {
      g_warning ("Failed to reference external X11 window, invalid XID %s", handle_str);
      return NULL;
    }

  foreign_gdk_window = gdk_x11_window_foreign_new_for_display (display, xid);
  if (!foreign_gdk_window)
    {
      g_warning ("Failed to create foreign window for XID %d", xid);
      return NULL;
    }

  external_window_x11 = g_object_new (EXTERNAL_TYPE_WINDOW_X11,
                                      "display", display,
                                      NULL);
  external_window_x11->foreign_gdk_window = foreign_gdk_window;

  return external_window_x11;
}

static void
external_window_x11_set_parent_of (ExternalWindow *external_window,
                                   GdkWindow      *child_window)
{
  ExternalWindowX11 *external_window_x11 =
    EXTERNAL_WINDOW_X11 (external_window);

  gdk_window_set_transient_for (child_window,
                                external_window_x11->foreign_gdk_window);
}

static void
external_window_x11_dispose (GObject *object)
{
  ExternalWindowX11 *external_window_x11 = EXTERNAL_WINDOW_X11 (object);

  g_clear_object (&external_window_x11->foreign_gdk_window);

  G_OBJECT_CLASS (external_window_x11_parent_class)->dispose (object);
}

static void
external_window_x11_init (ExternalWindowX11 *external_window_x11)
{
}

static void
external_window_x11_class_init (ExternalWindowX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ExternalWindowClass *external_window_class = EXTERNAL_WINDOW_CLASS (klass);

  object_class->dispose = external_window_x11_dispose;

  external_window_class->set_parent_of = external_window_x11_set_parent_of;
}
