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

#include <gio/gio.h>
#include <glib/gi18n.h>

#include "wallpaperdialog.h"
#include "wallpaperpreview.h"

struct _WallpaperDialog {
  GtkWindow parent;

  GtkWidget *stack;
  WallpaperPreview *desktop_preview;
  WallpaperPreview *lockscreen_preview;

  gchar *picture_uri;
  SetWallpaperOn set_on;
};

struct _WallpaperDialogClass {
  GtkWindowClass parent_class;

  void (* response) (WallpaperDialog *dialog);
};

enum {
  RESPONSE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (WallpaperDialog, wallpaper_dialog, GTK_TYPE_WINDOW)

static void
wallpaper_dialog_apply (WallpaperDialog *self)
{
  g_signal_emit (self, signals[RESPONSE], 0, GTK_RESPONSE_APPLY);
}

static void
wallpaper_dialog_cancel (WallpaperDialog *self)
{
  g_signal_emit (self, signals[RESPONSE], 0, GTK_RESPONSE_CANCEL);
}

static void
wallpaper_dialog_finalize (GObject *object)
{
  WallpaperDialog *self = WALLPAPER_DIALOG (object);

  g_clear_pointer (&self->picture_uri, g_free);

  G_OBJECT_CLASS (wallpaper_dialog_parent_class)->finalize (object);
}

static void
wallpaper_dialog_init (WallpaperDialog *self)
{
  volatile GType type G_GNUC_UNUSED;

  /* Register types that the builder needs */
  type = wallpaper_preview_get_type ();

  gtk_widget_init_template (GTK_WIDGET (self));
}

static void
wallpaper_dialog_class_init (WallpaperDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = wallpaper_dialog_finalize;

  signals[RESPONSE] = g_signal_new ("response",
                                    G_TYPE_FROM_CLASS (klass),
                                    G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST,
                                    0,
                                    NULL, NULL,
                                    NULL,
                                    G_TYPE_NONE, 1, G_TYPE_INT);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/freedesktop/portal/desktop/gtk/wallpaperdialog.ui");
  gtk_widget_class_bind_template_child (widget_class, WallpaperDialog, stack);
  gtk_widget_class_bind_template_child (widget_class, WallpaperDialog, desktop_preview);
  gtk_widget_class_bind_template_child (widget_class, WallpaperDialog, lockscreen_preview);

  gtk_widget_class_bind_template_callback (widget_class, wallpaper_dialog_cancel);
  gtk_widget_class_bind_template_callback (widget_class, wallpaper_dialog_apply);
}

static void
set_wallpaper_on_background (WallpaperDialog *self)
{
  wallpaper_preview_set_image (self->desktop_preview,
                               self->picture_uri, FALSE);
  gtk_window_set_title (GTK_WINDOW (self), _("Set Background"));
  gtk_stack_set_visible_child (GTK_STACK (self->stack),
                               GTK_WIDGET (self->desktop_preview));
}

static void
set_wallpaper_on_lockscreen (WallpaperDialog *self)
{
  wallpaper_preview_set_image (self->lockscreen_preview,
                               self->picture_uri, TRUE);
  gtk_window_set_title (GTK_WINDOW (self), _("Set Lock Screen"));
  gtk_stack_set_visible_child (GTK_STACK (self->stack),
                               GTK_WIDGET (self->lockscreen_preview));
}

static void
on_image_loaded_cb (GObject *source_object,
                    GAsyncResult *result,
                    gpointer data)
{
  WallpaperDialog *self = data;
  GFileIOStream *stream = NULL;
  GFile *image_file = G_FILE (source_object);
  GFile *tmp = g_file_new_tmp ("XXXXXX", &stream, NULL);
  g_autoptr(GError) error = NULL;
  gchar *contents = NULL;
  gsize length = 0;

  g_object_unref (stream);

  if (!g_file_load_contents_finish (image_file, result, &contents, &length, NULL, &error))
    {
      g_warning ("Failed to load image: %s", error->message);

      return;
    }

  g_file_replace_contents (tmp, contents, length, NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION, NULL, NULL, &error);

  self->picture_uri = g_strdup (g_file_get_uri (tmp));

  switch (self->set_on)
    {
      case BACKGROUND:
        set_wallpaper_on_background (self);
        break;
      case LOCKSCREEN:
        set_wallpaper_on_lockscreen (self);
        break;
      default:
        set_wallpaper_on_background (self);
        set_wallpaper_on_lockscreen (self);

        gtk_window_set_title (GTK_WINDOW (self),
                              _("Set Background & Lock Screen"));
    }
}

WallpaperDialog *
wallpaper_dialog_new (const gchar *picture_uri,
                      const gchar *app_id,
                      SetWallpaperOn set_on)
{
  WallpaperDialog *self;
  g_autoptr(GFile) image_file = g_file_new_for_uri (picture_uri);

  self = g_object_new (wallpaper_dialog_get_type (), NULL);

  self->set_on = set_on;
  g_file_load_contents_async (image_file,
                              NULL,
                              on_image_loaded_cb,
                              self);

  return self;
}

const gchar *
wallpaper_dialog_get_uri (WallpaperDialog *dialog)
{
  return dialog->picture_uri;
}
