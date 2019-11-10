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

WallpaperDialog *
wallpaper_dialog_new (const gchar *picture_uri,
                      const gchar *app_id,
                      SetWallpaperOn set_on)
{
  WallpaperDialog *self;

  self = g_object_new (wallpaper_dialog_get_type (), NULL);

  if (picture_uri)
    {
      self->picture_uri = g_strdup (picture_uri);

      switch (set_on)
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

  return self;
}
