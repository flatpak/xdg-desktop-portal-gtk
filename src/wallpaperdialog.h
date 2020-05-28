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

#pragma once

#include <gtk/gtk.h>

#define WALLPAPER_TYPE_DIALOG (wallpaper_dialog_get_type ())
#define WALLPAPER_DIALOG(object) (G_TYPE_CHECK_INSTANCE_CAST (object, WALLPAPER_TYPE_DIALOG, WallpaperDialog))

typedef struct _WallpaperDialog WallpaperDialog;
typedef struct _WallpaperDialogClass WallpaperDialogClass;

GType             wallpaper_dialog_get_type (void) G_GNUC_CONST;

WallpaperDialog * wallpaper_dialog_new (const char *picture_uri,
                                        const char *app_id);

const gchar     * wallpaper_dialog_get_uri (WallpaperDialog *dialog);
