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

G_DECLARE_FINAL_TYPE (AppChooserRow, app_chooser_row, APP, CHOOSER_ROW, GtkListBox)

AppChooserRow *app_chooser_row_new (GAppInfo *info);
void app_chooser_row_set_selected (AppChooserRow *row,
                                   gboolean selected);
GAppInfo *app_chooser_row_get_info (AppChooserRow *row);
