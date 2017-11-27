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

#include <string.h>

#include "externalwindow.h"
#ifdef HAVE_GTK_X11
#include "externalwindow-x11.h"
#endif
#ifdef HAVE_GTK_WAYLAND
#include "externalwindow-wayland.h"
#endif

enum
{
  PROP_0,

  PROP_DISPLAY,
};

typedef struct _ExternalWindowPrivate
{
  GdkDisplay *display;
} ExternalWindowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ExternalWindow, external_window, G_TYPE_OBJECT)

ExternalWindow *
create_external_window_from_handle (const char *handle_str)
{
#ifdef HAVE_GTK_X11
    {
      const char x11_prefix[] = "x11:";
      if (g_str_has_prefix (handle_str, x11_prefix))
        {
          ExternalWindowX11 *external_window_x11;
          const char *x11_handle_str = handle_str + strlen (x11_prefix);

          external_window_x11 = external_window_x11_new (x11_handle_str);
          return EXTERNAL_WINDOW (external_window_x11);
        }
    }
#endif
#ifdef HAVE_GTK_WAYLAND
    {
      const char wayland_prefix[] = "wayland:";
      if (g_str_has_prefix (handle_str, wayland_prefix))
        {
          ExternalWindowWayland *external_window_wayland;
          const char *wayland_handle_str = handle_str + strlen (wayland_prefix);

          external_window_wayland =
            external_window_wayland_new (wayland_handle_str);
          return EXTERNAL_WINDOW (external_window_wayland);
        }
    }
#endif

  g_warning ("Unhandled parent window type %s\n", handle_str);
  return NULL;
}

void
external_window_set_parent_of (ExternalWindow *external_window,
                               GdkWindow      *child_window)
{
  EXTERNAL_WINDOW_GET_CLASS (external_window)->set_parent_of (external_window,
                                                              child_window);
}

GdkDisplay *
external_window_get_display (ExternalWindow *external_window)
{
  ExternalWindowPrivate *priv =
    external_window_get_instance_private (external_window);

  return priv->display;
}

static void
external_window_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  ExternalWindow *external_window = EXTERNAL_WINDOW (object);
  ExternalWindowPrivate *priv =
    external_window_get_instance_private (external_window);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_set_object (&priv->display, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
external_window_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  ExternalWindow *external_window = EXTERNAL_WINDOW (object);
  ExternalWindowPrivate *priv =
    external_window_get_instance_private (external_window);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, priv->display);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
external_window_init (ExternalWindow *external_window)
{
}

static void
external_window_class_init (ExternalWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = external_window_get_property;
  object_class->set_property = external_window_set_property;

  g_object_class_install_property (object_class,
                                   PROP_DISPLAY,
                                   g_param_spec_object ("display",
                                                        "GdkDisplay",
                                                        "The GdkDisplay instance",
                                                        GDK_TYPE_DISPLAY,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}
