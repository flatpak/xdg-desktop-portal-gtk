/*
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
 * Authors:
 *       Felipe Borges <feborges@redhat.com>
 */

#define _GNU_SOURCE 1

#include "config.h"

#include <string.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "wallpaperpreview.h"

struct _WallpaperPreview {
  GtkBox parent;

  GtkStack *stack;
  GtkLabel *lockscreen_clock_label;
  GtkLabel *desktop_clock_label;
  GtkWidget *drawing_area;

  GFile *image_file;
  GSettings *desktop_settings;
  gboolean is_24h_format;
  GDateTime *previous_time;
  guint clock_time_timeout_id;
};

struct _WallpaperPreviewClass {
  GtkBoxClass parent_class;
};

G_DEFINE_TYPE (WallpaperPreview, wallpaper_preview, GTK_TYPE_BOX)

static gboolean
on_preview_draw_cb (GtkWidget *widget,
                    cairo_t   *cr,
                    WallpaperPreview *self)
{
  g_autoptr(GdkPixbuf) pixbuf = NULL;
  GtkAllocation allocation;
  const gchar *image_path;

  if (!self->image_file)
    return FALSE;

  image_path = g_file_get_path (self->image_file);
  if (!image_path)
    return FALSE;

  gtk_widget_get_allocation (GTK_WIDGET (self), &allocation);
  pixbuf = gdk_pixbuf_new_from_file_at_scale (image_path,
                                              allocation.width,
                                              allocation.height,
                                              FALSE, NULL);
  gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, 0);
  cairo_paint (cr);

  return TRUE;
}

static void
update_clock_label (WallpaperPreview *self,
                    gboolean          force)
{
  g_autoptr(GDateTime) now = NULL;
  g_autofree gchar *label = NULL;

  now = g_date_time_new_now_local ();

  if (!force && self->previous_time &&
      g_date_time_get_hour (now) == g_date_time_get_hour (self->previous_time) &&
      g_date_time_get_minute (now) == g_date_time_get_minute (self->previous_time))
    {
      return;
    }

  if (self->is_24h_format)
    label = g_date_time_format (now, "%R");
  else
    label = g_date_time_format (now, "%I:%M %p");

  gtk_label_set_label (self->desktop_clock_label, label);
  gtk_label_set_label (self->lockscreen_clock_label, label);

  g_clear_pointer (&self->previous_time, g_date_time_unref);
  self->previous_time = g_steal_pointer (&now);
}

static void
update_clock_format (WallpaperPreview *self)
{
  g_autofree gchar *clock_format = NULL;
  gboolean is_24h_format;

  clock_format = g_settings_get_string (self->desktop_settings, "clock-format");
  is_24h_format = g_strcmp0 (clock_format, "24h") == 0;

  if (is_24h_format != self->is_24h_format)
    {
      self->is_24h_format = is_24h_format;
      update_clock_label (self, TRUE);
    }
}

static gboolean
update_clock_cb (gpointer data)
{
  WallpaperPreview *self = WALLPAPER_PREVIEW (data);

  update_clock_label (self, FALSE);

  return G_SOURCE_CONTINUE;
}

static void
wallpaper_preview_finalize (GObject *object)
{
  WallpaperPreview *self = WALLPAPER_PREVIEW (object);

  g_clear_object (&self->desktop_settings);
  g_clear_object (&self->image_file);

  g_clear_pointer (&self->previous_time, g_date_time_unref);

  if (self->clock_time_timeout_id > 0)
    {
      g_source_remove (self->clock_time_timeout_id);
      self->clock_time_timeout_id = 0;
    }

  G_OBJECT_CLASS (wallpaper_preview_parent_class)->finalize (object);
}

static void
wallpaper_preview_init (WallpaperPreview *self)
{
  g_autoptr(GtkCssProvider) provider = NULL;
  g_autofree gchar *css;

  gtk_widget_init_template (GTK_WIDGET (self));

  css = g_strdup ("frame.desktop-preview { min-height: 10px; padding: 0 4px; background-color: black; }\n"
                  "frame.desktop-preview image { color: white; }\n"
                  "frame.desktop-preview label { color: white; font-weight: bold; font-size: 6px; }\n"
                  "frame.lockscreen-preview { border: solid rgba(0, 0, 0, 0.33); border-width: 10px 0 0 0; }\n"
                  "frame.lockscreen-preview label { color: white; font-weight: bold; text-shadow: 0 1px 2px rgba(0, 0, 0, 0.6); font-size: 1.2em; }\n");
  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (provider, css, -1, NULL);
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  self->desktop_settings = g_settings_new ("org.gnome.desktop.interface");

  g_signal_connect_object (self->desktop_settings,
                           "changed::clock-format",
                           G_CALLBACK (update_clock_format),
                           self,
                           G_CONNECT_SWAPPED);
  update_clock_format (self);

  self->clock_time_timeout_id = g_timeout_add_seconds (1, update_clock_cb, self);
}

static void
wallpaper_preview_class_init (WallpaperPreviewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = wallpaper_preview_finalize;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/freedesktop/portal/desktop/gtk/wallpaperpreview.ui");

  gtk_widget_class_bind_template_child (widget_class, WallpaperPreview, stack);
  gtk_widget_class_bind_template_child (widget_class, WallpaperPreview, drawing_area);
  gtk_widget_class_bind_template_child (widget_class, WallpaperPreview, desktop_clock_label);
  gtk_widget_class_bind_template_child (widget_class, WallpaperPreview, lockscreen_clock_label);
  gtk_widget_class_bind_template_callback (widget_class, on_preview_draw_cb);
}

WallpaperPreview *
wallpaper_preview_new ()
{
  return g_object_new (wallpaper_preview_get_type (), NULL);
}

void
wallpaper_preview_set_image (WallpaperPreview *self,
                             const gchar *image_uri,
                             gboolean is_lockscreen)
{
  g_clear_object (&self->image_file);
  self->image_file = g_file_new_for_uri (image_uri);

  if (is_lockscreen)
    {
      gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "lockscreen");
    }

  gtk_widget_queue_draw (self->drawing_area);
}
