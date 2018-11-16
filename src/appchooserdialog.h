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

#define APP_TYPE_CHOOSER_DIALOG (app_chooser_dialog_get_type ())
#define APP_CHOOSER_DIALOG(object) (G_TYPE_CHECK_INSTANCE_CAST (object, APP_TYPE_CHOOSER_DIALOG, AppChooserDialog))

typedef struct _AppChooserDialog AppChooserDialog;
typedef struct _AppChooserDialogClass AppChooserDialogClass;

GType              app_chooser_dialog_get_type (void) G_GNUC_CONST;

AppChooserDialog * app_chooser_dialog_new (const char **app_ids,
                                           const char  *default_id,
                                           const char  *content_type,
                                           const char  *filename);

void      app_chooser_dialog_update_choices (AppChooserDialog *dialog,
                                             const char       **app_ids);

GAppInfo *app_chooser_dialog_get_info (AppChooserDialog *dialog);
