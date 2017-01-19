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

#pragma once

#include <glib-object.h>
#include <gtk/gtk.h>


#define EXTERNAL_TYPE_WINDOW (external_window_get_type ())
#define EXTERNAL_WINDOW(object) (G_TYPE_CHECK_INSTANCE_CAST (object, EXTERNAL_TYPE_WINDOW, ExternalWindow))
#define EXTERNAL_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST (klass, EXTERNAL_TYPE_WINDOW, ExternalWindowClass))
#define EXTERNAL_WINDOW_GET_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS (klass, EXTERNAL_TYPE_WINDOW, ExternalWindowClass))

typedef struct _ExternalWindow ExternalWindow;
typedef struct _ExternalWindowClass ExternalWindowClass;

struct _ExternalWindow
{
  GObject parent_instance;
};

struct _ExternalWindowClass
{
  GObjectClass parent_class;

  void (*set_parent_of) (ExternalWindow *external_window,
                         GdkWindow      *child_window);
};

GType external_window_get_type (void);
ExternalWindow *create_external_window_from_handle (const char *handle_str);

void external_window_set_parent_of (ExternalWindow *external_window,
                                    GdkWindow      *child_window);

GdkDisplay *external_window_get_display (ExternalWindow *external_window);
