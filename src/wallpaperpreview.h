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

#define WALLPAPER_TYPE_PREVIEW (wallpaper_preview_get_type ())
#define WALLPAPER_PREVIEW(object) (G_TYPE_CHECK_INSTANCE_CAST (object, WALLPAPER_TYPE_PREVIEW, WallpaperPreview))

typedef struct _WallpaperPreview WallpaperPreview;
typedef struct _WallpaperPreviewClass WallpaperPreviewClass;

GType              wallpaper_preview_get_type  (void) G_GNUC_CONST;

void               wallpaper_preview_set_image (WallpaperPreview *self,
                                                const gchar *image_uri);
