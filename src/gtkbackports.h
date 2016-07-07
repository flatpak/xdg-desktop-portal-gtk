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
#include <gtk/gtkunixprint.h>

#if !GTK_CHECK_VERSION(3,22,0)

GVariant         *gtk_file_filter_to_gvariant          (GtkFileFilter     *filter);
GtkFileFilter    *gtk_file_filter_new_from_gvariant    (GVariant          *variant);

GVariant         *gtk_print_settings_to_gvariant       (GtkPrintSettings  *settings);
GtkPrintSettings *gtk_print_settings_new_from_gvariant (GVariant          *variant);

GVariant          *gtk_page_setup_to_gvariant           (GtkPageSetup     *setup);
GtkPageSetup      *gtk_page_setup_new_from_gvariant     (GVariant         *variant);

gboolean           gtk_print_job_set_source_fd          (GtkPrintJob      *job,
                                                         int               fd,
                                                         GError          **error);


#endif
