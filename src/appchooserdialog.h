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

G_DECLARE_FINAL_TYPE (AppChooserDialog, app_chooser_dialog, APP, CHOOSER_DIALOG, GtkWindow)

AppChooserDialog * app_chooser_dialog_new (const char **app_ids,
                                           const char *default_id,
                                           const char *cancel_label,
                                           const char *accept_label,
                                           const char *title,
                                           const char *heading);
void app_chooser_dialog_set_selected (AppChooserDialog *dialog,
                                      const char *choice_id);
