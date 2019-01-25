/*
 * Copyright © 2018 Red Hat, Inc
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
 */

#pragma once

#include <gtk/gtk.h>

#include "screencast.h"

#define REMOTE_DESKTOP_TYPE_DIALOG (remote_desktop_dialog_get_type ())
G_DECLARE_FINAL_TYPE (RemoteDesktopDialog, remote_desktop_dialog,
                      REMOTE_DESKTOP, DIALOG, GtkWindow)

RemoteDesktopDialog * remote_desktop_dialog_new (const char *app_id,
                                                 RemoteDesktopDeviceType device_types,
                                                 ScreenCastSelection *screen_cast_select);
