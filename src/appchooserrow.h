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
 *       Matthias Clasen <mclasen@redhat.com>
 */

#pragma once

#include <gtk/gtk.h>

#define APP_TYPE_CHOOSER_ROW (app_chooser_row_get_type ())
#define APP_CHOOSER_ROW(object) (G_TYPE_CHECK_INSTANCE_CAST (object, APP_TYPE_CHOOSER_ROW, AppChooserRow))

typedef struct _AppChooserRow AppChooserRow;
typedef struct _AppChooserRowClass AppChooserRowClass;

GType app_chooser_row_get_type (void);
AppChooserRow *app_chooser_row_new (GAppInfo *info);
GAppInfo *app_chooser_row_get_info (AppChooserRow *row);
void app_chooser_row_set_selected (AppChooserRow *row,
                                   gboolean       selected);
