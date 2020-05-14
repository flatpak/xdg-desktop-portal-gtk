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
#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-bg.h>

#include "wallpaperpreview.h"

struct _WallpaperPreview {
  GtkBox parent;

  GtkStack *stack;
  GtkWidget *desktop_preview;
  GtkWidget *animated_background_icon;
  GtkLabel *desktop_clock_label;
  GtkWidget *drawing_area;

  GnomeDesktopThumbnailFactory *thumbs;
  GnomeBG *bg;

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

  gtk_widget_get_allocation (GTK_WIDGET (self), &allocation);
  pixbuf = gnome_bg_create_thumbnail (self->bg,
                                      self->thumbs,
                                      gdk_screen_get_default (),
                                      allocation.width,
                                      allocation.height);
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
  g_clear_object (&self->thumbs);

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
  gtk_widget_init_template (GTK_WIDGET (self));

  self->desktop_settings = g_settings_new ("org.gnome.desktop.interface");

  g_signal_connect_object (self->desktop_settings,
                           "changed::clock-format",
                           G_CALLBACK (update_clock_format),
                           self,
                           G_CONNECT_SWAPPED);
  update_clock_format (self);

  self->clock_time_timeout_id = g_timeout_add_seconds (1, update_clock_cb, self);

  self->bg = gnome_bg_new ();
  gnome_bg_set_placement (self->bg, G_DESKTOP_BACKGROUND_STYLE_ZOOM);
  self->thumbs = gnome_desktop_thumbnail_factory_new (GNOME_DESKTOP_THUMBNAIL_SIZE_LARGE);
}

static void
wallpaper_preview_map (GtkWidget *widget)
{
  static GtkCssProvider *provider;

  GTK_WIDGET_CLASS (wallpaper_preview_parent_class)->map (widget);

  if (provider == NULL)
    {
      provider = gtk_css_provider_new ();
      gtk_css_provider_load_from_resource (provider, "/org/freedesktop/portal/desktop/gtk/wallpaperpreview.css");
      gtk_style_context_add_provider_for_screen (gtk_widget_get_screen (widget),
                                                 GTK_STYLE_PROVIDER (provider),
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
}

static void
wallpaper_preview_class_init (WallpaperPreviewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = wallpaper_preview_finalize;
  widget_class->map = wallpaper_preview_map;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/freedesktop/portal/desktop/gtk/wallpaperpreview.ui");

  gtk_widget_class_bind_template_child (widget_class, WallpaperPreview, stack);
  gtk_widget_class_bind_template_child (widget_class, WallpaperPreview, desktop_preview);
  gtk_widget_class_bind_template_child (widget_class, WallpaperPreview, animated_background_icon);
  gtk_widget_class_bind_template_child (widget_class, WallpaperPreview, drawing_area);
  gtk_widget_class_bind_template_child (widget_class, WallpaperPreview, desktop_clock_label);
  gtk_widget_class_bind_template_callback (widget_class, on_preview_draw_cb);
}

WallpaperPreview *
wallpaper_preview_new ()
{
  return g_object_new (wallpaper_preview_get_type (), NULL);
}

void
wallpaper_preview_set_image (WallpaperPreview *self,
                             const gchar *image_uri)
{
  g_autofree char *path = NULL;
  g_autoptr(GFile) image_file = NULL;

  image_file = g_file_new_for_uri (image_uri);
  path = g_file_get_path (image_file);
  gnome_bg_set_filename (self->bg, path);

  gtk_widget_set_visible (self->animated_background_icon,
                          gnome_bg_changes_with_time (self->bg));
  gtk_stack_set_visible_child (GTK_STACK (self->stack), self->desktop_preview);

  gtk_widget_queue_draw (self->drawing_area);
}
